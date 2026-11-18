// Frame Window Counter - game glue.
//
// Loads a macro, plays it back tick by tick, and shows a circle with the frame
// window at every input, plus a running tally. It can also count the windows
// itself (by replaying the level with one input moved at a time) and render the
// result to a video through ffmpeg. The logic lives in fwc_core.hpp.
#include <Geode/Geode.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include <cstdio>
#include <ctime>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "fwc_core.hpp"

using namespace geode::prelude;

#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_MACOS)
    #define FWC_CAN_RENDER 1
#else
    #define FWC_CAN_RENDER 0
#endif

namespace {

constexpr float TICK_DT = 1.f / 240.f;

enum class Mode { Off, Overlay, Play, Count, Render };

struct Config {
    Mode mode = Mode::Off;
    std::string macroPath;
    int offset = 0;

    bool markers = true;
    bool releases = true;
    int maxShown = 10;
    float markerSeconds = 1.5f;
    float markerSize = 1.f;
    bool hud = true;
    int hudMax = 4;

    int maxWindow = 10;
    int horizon = 48;
    bool checkpoints = true;
    bool countReleases = true;
    int countSpeed = 400;

    std::string ffmpeg;
    int fps = 60;
    bool song = true;

    void load() {
        auto mod = Mod::get();
        auto modeName = mod->getSettingValue<std::string>("mode");
        if (modeName == "Overlay only") mode = Mode::Overlay;
        else if (modeName == "Play macro") mode = Mode::Play;
        else if (modeName == "Count windows") mode = Mode::Count;
        else if (modeName == "Render video") mode = Mode::Render;
        else mode = Mode::Off;

        macroPath = mod->getSettingValue<asp::fs::Path>("macro-file").string();
        offset = static_cast<int>(mod->getSettingValue<int64_t>("frame-offset"));

        markers = mod->getSettingValue<bool>("markers");
        releases = mod->getSettingValue<bool>("show-releases");
        maxShown = static_cast<int>(mod->getSettingValue<int64_t>("max-shown-window"));
        markerSeconds = static_cast<float>(mod->getSettingValue<double>("marker-seconds"));
        markerSize = static_cast<float>(mod->getSettingValue<double>("marker-size"));
        hud = mod->getSettingValue<bool>("hud");
        hudMax = static_cast<int>(mod->getSettingValue<int64_t>("hud-max-window"));

        maxWindow = static_cast<int>(mod->getSettingValue<int64_t>("max-window"));
        horizon = static_cast<int>(mod->getSettingValue<int64_t>("horizon"));
        checkpoints = mod->getSettingValue<bool>("use-checkpoints");
        countReleases = mod->getSettingValue<bool>("count-releases");
        countSpeed = static_cast<int>(mod->getSettingValue<int64_t>("count-speed"));

        ffmpeg = mod->getSettingValue<asp::fs::Path>("ffmpeg-path").string();
        fps = static_cast<int>(mod->getSettingValue<int64_t>("render-fps"));
        song = mod->getSettingValue<bool>("render-song");
    }
};

struct RenderState {
    bool active = false;
    FILE* pipe = nullptr;
    CCRenderTexture* texture = nullptr;
    int width = 0;
    int height = 0;
    long frames = 0;
    int tail = -1; // frames left to record after the run ended; -1 = still running
    std::string tempPath;
    std::string outPath;
};

struct Session {
    Mode mode = Mode::Off;
    Config cfg;

    std::vector<fwc::Input> inputs;
    std::vector<int> windows; // parallel to inputs, 0 = not counted
    long long levelId = 0;
    std::string sidecarPath;

    size_t next = 0;   // next input to send / mark
    double acc = 0.0;  // fractional ticks carried between frames

    std::vector<int> totals; // per window size
    std::vector<int> seen;
    double precision = 0.0;

    // counting
    std::unique_ptr<fwc::Counter> counter;
    unsigned int ownCheckpoints = 0;
    bool died = false;
    bool completed = false;

    RenderState render;
};

Session g;

int currentTick(GJBaseGameLayer* layer) {
    // level time advances one physics step at a time; the epsilon absorbs float drift
    return static_cast<int>(std::floor(layer->m_gameState.m_levelTime * fwc::TPS + 1e-4));
}

ccColor3B windowColor(int window) {
    static const ccColor3B colors[] = {
        {255, 60, 60},   // 1
        {255, 150, 40},  // 2
        {255, 225, 50},  // 3
        {160, 235, 70},  // 4
        {70, 220, 110},  // 5
        {60, 210, 190},  // 6
        {80, 180, 255},  // 7
        {110, 130, 255}, // 8
        {170, 120, 255}, // 9
    };
    if (window >= 1 && window <= 9) return colors[window - 1];
    return {190, 190, 190};
}

std::string inQuotes(std::string const& s) {
    return "\"" + s + "\"";
}

// cmd.exe strips the outer quotes of a command that starts with one, so the
// whole line needs an extra pair on Windows.
std::string shellLine(std::string const& command) {
#ifdef GEODE_IS_WINDOWS
    return "\"" + command + "\"";
#else
    return command;
#endif
}

void notify(std::string const& text, NotificationIcon icon = NotificationIcon::Info) {
    Notification::create(text, icon)->show();
}

} // namespace

class $modify(FwcPlayLayer, PlayLayer) {
    struct Marker {
        CCNode* node;
        int bornTick;
        int expireTick;
    };

    struct Fields {
        CCNode* markers = nullptr;
        CCNode* hud = nullptr;
        std::vector<CCLabelBMFont*> hudRows; // index = window size
        CCLabelBMFont* hudFooter = nullptr;
        CCLabelBMFont* status = nullptr;
        std::vector<Marker> live;
    };

    // ----- setup -----------------------------------------------------------

    bool init(GJGameLevel* level, bool dontCreateObjects) {
        this->closeRender(false);
        g = Session{};
        if (!PlayLayer::init(level, dontCreateObjects)) return false;
        this->setupSession();
        return true;
    }

    void setupSession() {
        g.cfg.load();
        if (g.cfg.mode == Mode::Off) return;
        if (g.cfg.macroPath.empty()) {
            notify("Frame Window Counter: no macro file selected", NotificationIcon::Warning);
            return;
        }

        auto bytes = file::readBinary(asp::fs::PathView(g.cfg.macroPath));
        if (!bytes) {
            notify("Frame Window Counter: could not open the macro file", NotificationIcon::Error);
            return;
        }
        auto data = std::move(bytes).unwrap();
        auto parsed = fwc::parseMacro(data.data(), data.size());
        if (!parsed.ok) {
            notify("Frame Window Counter: " + parsed.error, NotificationIcon::Error);
            return;
        }

        // don't hijack a level the macro was not recorded on
        const long long here = m_level ? static_cast<long long>(m_level->m_levelID.value()) : 0;
        if (parsed.macro.levelId > 0 && here > 0 && parsed.macro.levelId != here) {
            notify(
                fmt::format("Frame Window Counter: this macro is for level {}, not this one", parsed.macro.levelId),
                NotificationIcon::Warning
            );
            return;
        }

        g.inputs = std::move(parsed.macro.inputs);
        g.levelId = parsed.macro.levelId > 0 ? parsed.macro.levelId : here;
        for (auto& in : g.inputs) in.tick = std::max(in.tick + g.cfg.offset, 0);
        fwc::sortInputs(g.inputs);

        // windows come from the file itself (.fwc.json) or from the file saved next to the macro
        g.sidecarPath = parsed.macro.format == "FWC" ? g.cfg.macroPath : g.cfg.macroPath + ".fwc.json";
        g.windows.assign(g.inputs.size(), 0);
        if (parsed.macro.format == "FWC") {
            fwc::Macro saved;
            saved.inputs = g.inputs;
            saved.windows = parsed.macro.windows;
            fwc::applySavedWindows(saved, g.inputs, g.windows);
        } else if (auto text = file::readBinary(asp::fs::PathView(g.sidecarPath))) {
            auto raw = std::move(text).unwrap();
            auto saved = fwc::parseMacro(raw.data(), raw.size());
            if (saved.ok) {
                for (auto& in : saved.macro.inputs) in.tick = std::max(in.tick, 0);
                fwc::applySavedWindows(saved.macro, g.inputs, g.windows);
            }
        }

        g.mode = g.cfg.mode;
#if !FWC_CAN_RENDER
        if (g.mode == Mode::Render) {
            notify("Frame Window Counter: video rendering is only available on Windows and macOS", NotificationIcon::Warning);
            g.mode = Mode::Play;
        }
#endif
        if (g.mode == Mode::Render) g.render.active = true;

        this->buildOverlay();
        this->recomputeTotals();

        bool counted = false;
        for (int w : g.windows) counted = counted || w > 0;
        if (!counted && g.mode != Mode::Count) {
            this->setStatus("No frame windows counted for this macro yet - set Mode to \"Count windows\"");
        }
    }

    void buildOverlay() {
        auto f = m_fields.self();
        const auto win = CCDirector::get()->getWinSize();

        if (m_objectLayer) {
            f->markers = CCNode::create();
            f->markers->setID("markers"_spr);
            m_objectLayer->addChild(f->markers, 5000);
        }

        f->hud = CCNode::create();
        f->hud->setID("hud"_spr);
        f->hud->setPosition({6.f, win.height - 6.f});
        this->addChild(f->hud, 1000);

        f->hudRows.assign(static_cast<size_t>(std::max(g.cfg.hudMax, 1)) + 1, nullptr);
        for (int w = 1; w <= g.cfg.hudMax; ++w) {
            auto row = CCLabelBMFont::create("", "bigFont.fnt");
            row->setAnchorPoint({0.f, 1.f});
            row->setScale(0.35f);
            row->setColor(windowColor(w));
            f->hud->addChild(row);
            f->hudRows[static_cast<size_t>(w)] = row;
        }
        f->hudFooter = CCLabelBMFont::create("", "bigFont.fnt");
        f->hudFooter->setAnchorPoint({0.f, 1.f});
        f->hudFooter->setScale(0.28f);
        f->hudFooter->setOpacity(200);
        f->hud->addChild(f->hudFooter);

        f->status = CCLabelBMFont::create("", "bigFont.fnt");
        f->status->setID("status"_spr);
        f->status->setScale(0.35f);
        f->status->setPosition({win.width / 2.f, win.height - 28.f});
        this->addChild(f->status, 1001);
    }

    void recomputeTotals() {
        g.totals = fwc::tally(g.windows, g.cfg.maxWindow);
        g.seen.assign(g.totals.size(), 0);
        const double seconds = g.inputs.empty() ? 0.0 : g.inputs.back().tick / fwc::TPS + 1.0;
        g.precision = fwc::precisionEstimate(g.inputs, g.windows, g.cfg.maxWindow, seconds);
        this->refreshHud();
    }

    void setStatus(std::string const& text) {
        if (auto label = m_fields->status) label->setString(text.c_str());
    }

    void refreshHud() {
        auto f = m_fields.self();
        if (!f->hud) return;
        f->hud->setVisible(g.cfg.hud && g.mode != Mode::Count);

        float y = 0.f;
        for (int w = 1; w <= g.cfg.hudMax && static_cast<size_t>(w) < f->hudRows.size(); ++w) {
            auto row = f->hudRows[static_cast<size_t>(w)];
            if (!row) continue;
            const int total = static_cast<size_t>(w) < g.totals.size() ? g.totals[static_cast<size_t>(w)] : 0;
            const int seen = static_cast<size_t>(w) < g.seen.size() ? g.seen[static_cast<size_t>(w)] : 0;
            row->setVisible(total > 0);
            if (total <= 0) continue;
            row->setString(fmt::format("{}f  {}/{}", w, seen, total).c_str());
            row->setPosition({0.f, y});
            y -= 13.f;
        }
        if (f->hudFooter) {
            std::string footer = fmt::format("Inputs {}/{}", std::min(g.next, g.inputs.size()), g.inputs.size());
            if (g.precision > 0.0) footer += fmt::format("   Precision {:.1f}", g.precision);
            f->hudFooter->setString(footer.c_str());
            f->hudFooter->setPosition({0.f, y - 2.f});
        }
    }

    // ----- markers ---------------------------------------------------------

    void spawnMarker(fwc::Input const& in, int window, int tick) {
        auto f = m_fields.self();
        auto player = in.p2 ? m_player2 : m_player1;
        if (!f->markers || !player) return;

        const auto color = windowColor(window);
        const ccColor4F line = {color.r / 255.f, color.g / 255.f, color.b / 255.f, 1.f};
        const float size = g.cfg.markerSize;
        const float radius = (in.down ? 15.f : 9.f) * size;

        auto node = CCNode::create();
        node->setPosition(player->getPosition());

        auto ring = CCDrawNode::create();
        ring->drawCircle({0.f, 0.f}, radius, {0.f, 0.f, 0.f, 0.f}, in.down ? 1.6f : 1.f, line, 48);
        node->addChild(ring);

        auto label = CCLabelBMFont::create(
            window >= g.cfg.maxWindow ? fmt::format("{}+", window).c_str() : fmt::format("{}", window).c_str(),
            "bigFont.fnt"
        );
        label->setScale((in.down ? 0.5f : 0.32f) * size);
        label->setColor(color);
        label->setPosition({0.f, radius + (in.down ? 9.f : 6.f) * size});
        node->addChild(label);

        f->markers->addChild(node);
        const int life = g.cfg.markerSeconds <= 0.f ? INT_MAX : static_cast<int>(g.cfg.markerSeconds * fwc::TPS);
        f->live.push_back({node, tick, life == INT_MAX ? INT_MAX : tick + life});
    }

    void expireMarkers() {
        auto f = m_fields.self();
        if (f->live.empty()) return;
        const int tick = currentTick(this);
        std::erase_if(f->live, [&](Marker const& m) {
            if (tick >= m.expireTick || tick < m.bornTick) {
                m.node->removeFromParent();
                return true;
            }
            return false;
        });
    }

    void clearMarkers() {
        auto f = m_fields.self();
        f->live.clear();
        if (f->markers) f->markers->removeAllChildren();
    }

    // Sends (when `press` is set) and marks every input that is due at `tick`.
    void dispatch(int tick, bool press) {
        bool changed = false;
        while (g.next < g.inputs.size() && g.inputs[g.next].tick <= tick) {
            const auto& in = g.inputs[g.next];
            const int window = g.windows[g.next];
            if (press) this->handleButton(in.down, in.button, !in.p2);

            if (window >= 1 && static_cast<size_t>(window) < g.seen.size()) g.seen[static_cast<size_t>(window)]++;
            if (g.cfg.markers && window >= 1 && window <= g.cfg.maxShown && (in.down || g.cfg.releases)) {
                this->spawnMarker(in, window, tick);
            }
            ++g.next;
            changed = true;
        }
        if (changed) this->refreshHud();
    }

    // ----- hooks -----------------------------------------------------------

    void resetLevel() {
        PlayLayer::resetLevel();
        if (g.mode == Mode::Off || g.counter) return; // the counter restarts constantly and keeps its own place

        const int tick = currentTick(this);
        auto it = std::lower_bound(g.inputs.begin(), g.inputs.end(), tick, [](fwc::Input const& in, int t) {
            return in.tick < t;
        });
        g.next = static_cast<size_t>(it - g.inputs.begin());
        g.acc = 0.0;
        this->clearMarkers();

        std::fill(g.seen.begin(), g.seen.end(), 0);
        for (size_t k = 0; k < g.next; ++k) {
            const int w = g.windows[k];
            if (w >= 1 && static_cast<size_t>(w) < g.seen.size()) g.seen[static_cast<size_t>(w)]++;
        }
        this->refreshHud();

        // resuming part-way (practice checkpoint): put held buttons back down
        if ((g.mode == Mode::Play || g.mode == Mode::Render) && g.next > 0) {
            for (int p2 = 0; p2 < 2; ++p2) {
                for (int button = 1; button <= 3; ++button) {
                    for (size_t k = g.next; k-- > 0;) {
                        const auto& in = g.inputs[k];
                        if (in.p2 == (p2 == 1) && in.button == button) {
                            if (in.down) this->handleButton(true, button, p2 == 0);
                            break;
                        }
                    }
                }
            }
        }
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (g.counter && object != m_anticheatSpike) {
            g.died = true; // the counter restarts the run itself
            return;
        }
        PlayLayer::destroyPlayer(player, object);
        if (g.render.active && object != m_anticheatSpike && g.render.tail < 0) {
            g.render.tail = g.cfg.fps * 3 / 2;
        }
    }

    void playEndAnimationToPos(CCPoint position) {
        if (g.counter) {
            g.completed = true;
            return;
        }
        PlayLayer::playEndAnimationToPos(position);
    }

    void playPlatformerEndAnimationToPos(CCPoint position, bool instant) {
        if (g.counter) {
            g.completed = true;
            return;
        }
        PlayLayer::playPlatformerEndAnimationToPos(position, instant);
    }

    void levelComplete() {
        if (g.counter) {
            g.completed = true;
            return;
        }
        PlayLayer::levelComplete();
        if (g.render.active && g.render.tail < 0) g.render.tail = g.cfg.fps * 3;
    }

    void onQuit() {
        this->closeRender(true);
        g.counter.reset();
        g.mode = Mode::Off;
        PlayLayer::onQuit();
    }

    // ----- counting ----------------------------------------------------------

    void beginCount() {
        if (!m_isPracticeMode) this->togglePracticeMode(true);
        fwc::CountOptions options;
        options.maxWindow = g.cfg.maxWindow;
        options.horizon = g.cfg.horizon;
        options.useCheckpoints = g.cfg.checkpoints;
        options.countReleases = g.cfg.countReleases;
        g.ownCheckpoints = 0;
        g.counter = std::make_unique<fwc::Counter>(g.inputs, options);
        this->clearMarkers();
        this->refreshHud();
    }

    void finishCount() {
        const bool failed = g.counter->failed();
        const std::string error = g.counter->error();
        if (!failed) g.windows = g.counter->windows();
        g.counter.reset();
        g.died = g.completed = false;

        // leaving practice mode also throws away the counter's checkpoints
        this->togglePracticeMode(false);

        if (failed) {
            g.mode = Mode::Off;
            this->setStatus(error);
            notify("Frame Window Counter: counting failed", NotificationIcon::Error);
            log::error("counting failed: {}", error);
        } else {
            const auto text = fwc::writeFwcJson(g.inputs, g.windows, g.cfg.maxWindow, g.cfg.horizon, g.levelId);
            const bool saved = file::writeString(asp::fs::PathView(g.sidecarPath), text).isOk();

            g.mode = Mode::Play;
            g.cfg.mode = Mode::Play;
            Mod::get()->setSettingValue<std::string>("mode", "Play macro");
            this->recomputeTotals();

            std::string summary;
            for (int w = 1; w <= 4 && static_cast<size_t>(w) < g.totals.size(); ++w) {
                summary += fmt::format("{}{}f: {}", w > 1 ? ", " : "", w, g.totals[static_cast<size_t>(w)]);
            }
            this->setStatus("");
            notify("Frame windows counted - " + summary, NotificationIcon::Success);
            if (!saved) notify("Could not save the windows next to the macro file", NotificationIcon::Warning);
        }
        this->resetLevel();
    }

    // ----- rendering -----------------------------------------------------------

    std::string songPath() {
        if (!m_level) return "";
        std::string name = m_level->getAudioFileName();
        if (name.empty()) return "";
        if (asp::fs::PathView(name).exists()) return name;
        // built-in songs are game resources
        std::string full = CCFileUtils::sharedFileUtils()->fullPathForFilename(name.c_str(), false);
        return asp::fs::PathView(full).exists() ? full : "";
    }

    bool openRender(int width, int height) {
#if FWC_CAN_RENDER
        auto& r = g.render;
        auto dir = Mod::get()->getSaveDir() / "renders";
        (void)file::createDirectoryAll(dir);

        std::string name = m_level ? std::string(m_level->m_levelName) : "level";
        for (auto& c : name) {
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) c = '_';
        }
        const auto stamp = static_cast<long long>(std::time(nullptr));
        r.outPath = (dir / fmt::format("{}-{}.mp4", name, stamp)).string();
        r.tempPath = (dir / fmt::format("{}-{}-video.mp4", name, stamp)).string();
        r.width = width;
        r.height = height;

        const std::string ffmpeg = g.cfg.ffmpeg.empty() ? "ffmpeg" : g.cfg.ffmpeg;
        const std::string command = fmt::format(
            "{} -y -loglevel error -f rawvideo -pix_fmt rgba -s {}x{} -r {} -i - "
            "-vf \"vflip,scale=trunc(iw/2)*2:trunc(ih/2)*2\" -c:v libx264 -preset veryfast -crf 18 -pix_fmt yuv420p {}",
            inQuotes(ffmpeg), width, height, g.cfg.fps, inQuotes(r.tempPath)
        );
    #ifdef GEODE_IS_WINDOWS
        r.pipe = _popen(shellLine(command).c_str(), "wb");
    #else
        r.pipe = popen(command.c_str(), "w");
    #endif
        return r.pipe != nullptr;
#else
        (void)width;
        (void)height;
        return false;
#endif
    }

    void captureFrame() {
        auto& r = g.render;
        const auto win = CCDirector::get()->getWinSize();
        if (!r.texture) {
            r.texture = CCRenderTexture::create(static_cast<int>(win.width), static_cast<int>(win.height));
            if (!r.texture) {
                this->abortRender("could not create the capture texture");
                return;
            }
            r.texture->retain();
        }

        r.texture->beginWithClear(0.f, 0.f, 0.f, 1.f);
        this->visit();
        r.texture->end();

        auto image = r.texture->newCCImage(false); // rows bottom-up; ffmpeg flips them
        if (!image) return;
        const int width = image->getWidth();
        const int height = image->getHeight();

        if (!r.pipe && r.frames == 0 && !this->openRender(width, height)) {
            image->release();
            this->abortRender("could not start ffmpeg - check the ffmpeg path setting");
            return;
        }
        if (r.pipe && width == r.width && height == r.height) {
            const size_t bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
            if (std::fwrite(image->getData(), 1, bytes, r.pipe) != bytes) {
                image->release();
                this->abortRender("ffmpeg stopped accepting frames - check the ffmpeg path setting");
                return;
            }
            ++r.frames;
        }
        image->release();

        if (r.frames % 30 == 0) {
            this->setStatus(fmt::format("Rendering video... {:.1f}s", static_cast<double>(r.frames) / g.cfg.fps));
        }
    }

    void abortRender(std::string const& why) {
        this->closeRender(false);
        g.mode = Mode::Play;
        this->setStatus("");
        notify("Frame Window Counter: " + why, NotificationIcon::Error);
    }

    // Stops recording. With `finish`, the song is added and the file is kept.
    void closeRender(bool finish) {
        auto& r = g.render;
        if (!r.active && !r.pipe && !r.texture) return;
#if FWC_CAN_RENDER
        if (r.pipe) {
    #ifdef GEODE_IS_WINDOWS
            _pclose(r.pipe);
    #else
            pclose(r.pipe);
    #endif
            r.pipe = nullptr;
        }
#endif
        if (r.texture) {
            r.texture->release();
            r.texture = nullptr;
        }
        const bool hadVideo = r.active && r.frames > 0;
        r.active = false;
        if (!finish || !hadVideo) return;

        const std::string ffmpeg = g.cfg.ffmpeg.empty() ? "ffmpeg" : g.cfg.ffmpeg;
        const std::string song = g.cfg.song ? this->songPath() : "";
        const float offset = m_levelSettings ? m_levelSettings->m_songOffset : 0.f;
        const std::string temp = r.tempPath;
        const std::string out = r.outPath;

        // adding the song can take a few seconds; keep it off the game thread
        std::thread([ffmpeg, song, offset, temp, out] {
            bool muxed = false;
            if (!song.empty()) {
                const std::string command = fmt::format(
                    "{} -y -loglevel error -i {} -ss {:.3f} -i {} -map 0:v -map 1:a -c:v copy -c:a aac -shortest {}",
                    inQuotes(ffmpeg), inQuotes(temp), offset, inQuotes(song), inQuotes(out)
                );
                muxed = std::system(shellLine(command).c_str()) == 0;
            }
            if (muxed) {
                std::remove(temp.c_str());
            } else {
                std::remove(out.c_str());
                std::rename(temp.c_str(), out.c_str());
            }
            queueInMainThread([out, muxed, song] {
                notify("Video saved: " + out, NotificationIcon::Success);
                if (!muxed && !song.empty()) notify("The song could not be added to the video", NotificationIcon::Warning);
            });
        }).detach();

        Mod::get()->setSettingValue<std::string>("mode", "Play macro");
    }

    void finishRender() {
        this->closeRender(true);
        g.mode = Mode::Play;
        g.cfg.mode = Mode::Play;
        this->setStatus("");
    }
};

class $modify(FwcGameLayer, GJBaseGameLayer) {
    // The counter's view of the game.
    struct GameSim final : fwc::Sim {
        FwcGameLayer* layer;
        FwcPlayLayer* play;
        GameSim(FwcGameLayer* l, FwcPlayLayer* p) : layer(l), play(p) {}

        void restart() override {
            g.died = g.completed = false;
            play->resetLevel();
        }
        int tick() override { return currentTick(layer); }
        void input(fwc::Input const& in) override { layer->handleButton(in.down, in.button, !in.p2); }
        fwc::StepResult step() override { return layer->simStep(play); }
        // the game's own list is the source of truth for how many checkpoints exist
        static unsigned int stored(FwcPlayLayer* p) {
            return p->m_checkpointArray ? p->m_checkpointArray->count() : 0u;
        }
        void checkpoint() override {
            play->markCheckpoint();
            g.ownCheckpoints = stored(play);
        }
        void dropCheckpoint() override {
            if (stored(play) > 0) play->removeCheckpoint(false);
            g.ownCheckpoints = stored(play);
        }
    };

    fwc::StepResult simStep(FwcPlayLayer* play) {
        g.died = false;
        play->m_tryPlaceCheckpoint = false;
        GJBaseGameLayer::update(TICK_DT);

        // automatic practice checkpoints would get in between the counter's own
        while (play->m_checkpointArray && play->m_checkpointArray->count() > g.ownCheckpoints) {
            play->removeCheckpoint(false);
        }
        if (g.died) return fwc::StepResult::Dead;
        if (g.completed) return fwc::StepResult::Complete;
        return fwc::StepResult::Alive;
    }

    // One physics tick with the macro's inputs for that tick sent first.
    void playTick(FwcPlayLayer* play) {
        if (!m_player1 || !m_player1->m_isDead) play->dispatch(currentTick(this), true);
        GJBaseGameLayer::update(TICK_DT);
    }

    void update(float dt) {
        auto current = PlayLayer::get();
        if (g.mode == Mode::Off || !current || static_cast<GJBaseGameLayer*>(current) != this) {
            GJBaseGameLayer::update(dt);
            return;
        }
        auto play = static_cast<FwcPlayLayer*>(current);

        switch (g.mode) {
            case Mode::Overlay:
                // another bot is playing: just follow along
                GJBaseGameLayer::update(dt);
                play->dispatch(currentTick(this), false);
                break;

            case Mode::Play: {
                g.acc += static_cast<double>(dt) * fwc::TPS;
                int ticks = static_cast<int>(g.acc);
                g.acc -= ticks;
                if (ticks > 24) { // don't spiral after a long hitch
                    ticks = 24;
                    g.acc = 0.0;
                }
                for (int k = 0; k < ticks; ++k) this->playTick(play);
                break;
            }

            case Mode::Render: {
                // fixed step: every video frame covers exactly 1/fps of level time
                g.acc += fwc::TPS / std::max(g.cfg.fps, 1);
                int ticks = static_cast<int>(g.acc);
                g.acc -= ticks;
                for (int k = 0; k < ticks; ++k) this->playTick(play);
                play->expireMarkers();
                if (m_started) play->captureFrame();
                if (g.render.active && g.render.tail > 0 && --g.render.tail == 0) play->finishRender();
                if (g.render.active && g.render.frames > static_cast<long>(g.cfg.fps) * 60 * 30) play->finishRender();
                return;
            }

            case Mode::Count: {
                if (!m_started) {
                    GJBaseGameLayer::update(dt);
                    return;
                }
                if (!g.counter) play->beginCount();
                GameSim sim(this, play);
                g.counter->advance(sim, std::max(g.cfg.countSpeed, 1));
                if (g.counter->finished()) {
                    play->finishCount();
                } else {
                    play->setStatus(fmt::format(
                        "Counting frame windows... {:.0f}%  (input {}/{})",
                        g.counter->progress() * 100.0, g.counter->current(), g.counter->inputs().size()
                    ));
                }
                return;
            }

            default:
                GJBaseGameLayer::update(dt);
                break;
        }
        play->expireMarkers();
    }
};

// While rendering, everything that runs on the scheduler (actions, particles,
// trails) advances by exactly one video frame, so the video plays at the right
// speed no matter how long each frame took to capture.
class $modify(FwcScheduler, CCScheduler) {
    void update(float dt) {
        if (g.mode == Mode::Render && g.render.active) dt = 1.f / static_cast<float>(std::max(g.cfg.fps, 1));
        CCScheduler::update(dt);
    }
};
