// Auto Deco - editor glue.
//
// Pause the editor (or press the Auto Deco button), pick a style, adjust its
// colours, preview it on the level, then keep it. The decoration is built by
// deco_build.hpp from the level's solid blocks; song pulses come from the
// level's guidelines or from analysing the song (beats.hpp).
#include <Geode/Geode.hpp>
#include <Geode/fmod/fmod.hpp>
#include <Geode/modify/EditorPauseLayer.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/ui/ColorPickPopup.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/Popup.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "beats.hpp"
#include "styles.hpp"

using namespace geode::prelude;

namespace {

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

int settingInt(char const* key) {
    return static_cast<int>(Mod::get()->getSettingValue<int64_t>(key));
}
bool settingBool(char const* key) {
    return Mod::get()->getSettingValue<bool>(key);
}
double settingFloat(char const* key) {
    return Mod::get()->getSettingValue<double>(key);
}

int decoGroup() { return std::clamp(settingInt("deco-group"), 1, 9999); }

std::vector<deco::Style> const& styles() {
    static auto list = deco::builtinStyles();
    return list;
}

ccColor3B toCC(deco::RGB c) { return {c.r, c.g, c.b}; }

void notify(std::string const& text, NotificationIcon icon = NotificationIcon::Info) {
    Notification::create(text, icon)->show();
}

// ---------------------------------------------------------------------------
// Object probing: what a piece looks like and whether it is safe to use
// ---------------------------------------------------------------------------

struct Probe {
    bool exists = false;
    bool safe = false; // no gameplay effect once marked No Touch
    int type = -1;
    deco::Shape shape;
};

Probe const& probe(int objectID) {
    static std::unordered_map<int, Probe> cache;
    if (auto it = cache.find(objectID); it != cache.end()) return it->second;

    Probe p;
    constexpr int SIZE = 160; // points; big enough for 2x decoration pieces
    if (auto obj = GameObject::createWithKey(objectID)) {
        p.exists = true;
        p.type = static_cast<int>(obj->m_objectType);
        // Plain blocks and decoration only: never hazards, portals, pads, orbs,
        // coins, triggers or slopes. Everything placed is No Touch, so a
        // block-type decoration piece has no collision either.
        p.safe = obj->m_objectType == GameObjectType::Solid || obj->m_objectType == GameObjectType::Decoration;
        if (auto rt = CCRenderTexture::create(SIZE, SIZE)) {
            obj->setPosition({SIZE / 2.f, SIZE / 2.f});
            obj->setRotation(0.f);
            obj->setScale(1.f);
            obj->setVisible(true);
            rt->beginWithClear(0.f, 0.f, 0.f, 0.f);
            obj->visit();
            rt->end();
            if (auto img = rt->newCCImage(false)) {
                p.shape = deco::measure(img->getData(), img->getWidth(), img->getHeight(), SIZE, SIZE);
                img->release();
            }
        }
    }
    if (!p.exists) log::warn("object {} does not exist in this version of the game", objectID);
    else if (!p.safe) log::info("object {} has gameplay type {}; Auto Deco will not use it", objectID, p.type);
    return cache.emplace(objectID, p).first->second;
}

deco::EdgeAnchor itemFoot(int id) {
    auto const& p = probe(id);
    if (!p.shape.valid) return {deco::Down, deco::HALF};
    return {deco::Down, -p.shape.minY};
}

// Drops details that are missing or not pure decoration (never adds hitboxes).
deco::Style safeStyle(deco::Style s) {
    auto keep = [](std::vector<deco::Item>& items) {
        std::erase_if(items, [](deco::Item const& it) {
            auto const& p = probe(it.id);
            return !p.exists || !p.safe;
        });
    };
    keep(s.top.items);
    keep(s.hang.items);
    keep(s.side.items);
    keep(s.ground.items);
    keep(s.background);
    return s;
}

deco::Anchors buildAnchors(deco::Style const& style) {
    deco::Anchors fallback;
    fallback.outlineEdge = {deco::Up, deco::HALF};
    fallback.outlineInner = {deco::TopLeft, {-deco::HALF, deco::HALF}};
    fallback.glowEdge = {deco::Down, deco::HALF};
    fallback.glowOuter = {deco::BottomLeft, {-deco::HALF, -deco::HALF}};
    fallback.glowInner = {deco::BottomLeft, {-deco::HALF, -deco::HALF}};
    if (!settingBool("auto-orient")) return fallback;

    auto const [edge, inner] = deco::outlineIDs(style.outline);
    auto const glow = deco::glowIDs(style.glow);
    deco::Anchors a = fallback;
    if (edge) a.outlineEdge = deco::edgeAnchor(probe(edge).shape, fallback.outlineEdge);
    if (inner) a.outlineInner = deco::cornerAnchor(probe(inner).shape, fallback.outlineInner);
    if (glow.edge) a.glowEdge = deco::edgeAnchor(probe(glow.edge).shape, fallback.glowEdge);
    if (glow.outer) a.glowOuter = deco::cornerAnchor(probe(glow.outer).shape, fallback.glowOuter);
    if (glow.inner) a.glowInner = deco::cornerAnchor(probe(glow.inner).shape, fallback.glowInner);
    return a;
}

// ---------------------------------------------------------------------------
// Level helpers
// ---------------------------------------------------------------------------

bool inGroup(GameObject* obj, int group) {
    if (!obj || !obj->m_groups) return false;
    for (int k = 0; k < obj->m_groupCount && k < 10; ++k) {
        if ((*obj->m_groups)[k] == group) return true;
    }
    return false;
}

std::vector<GameObject*> taggedObjects(LevelEditorLayer* lel) {
    std::vector<GameObject*> out;
    if (!lel || !lel->m_objects) return out;
    const int group = decoGroup();
    for (auto obj : CCArrayExt<GameObject*>(lel->m_objects)) {
        if (inGroup(obj, group)) out.push_back(obj);
    }
    return out;
}

// Layout blocks: solid objects with a real hitbox, not No Touch, not ours.
std::vector<deco::Rect> solidRects(CCArray* objects) {
    std::vector<deco::Rect> rects;
    if (!objects) return rects;
    const int group = decoGroup();
    int solidType = 0, noHitbox = 0, skipped = 0, rotated = 0;
    std::map<std::string, int> sizes;
    for (auto obj : CCArrayExt<GameObject*>(objects)) {
        if (!obj || obj->m_objectType != GameObjectType::Solid) continue;
        ++solidType;
        if (inGroup(obj, group) || obj->m_isNoTouch) {
            ++skipped;
            continue;
        }
        const float rot = std::fmod(std::fabs(obj->getRotation()), 90.f);
        if (rot > 0.5f && rot < 89.5f) {
            ++rotated;
            continue;
        }
        CCRect r = obj->getObjectRect();
        if (r.size.width < 1.f || r.size.height < 1.f) {
            ++noHitbox; // decoration-only blocks have no hitbox
            continue;
        }
        if (sizes.size() < 12) sizes[fmt::format("{:.0f}x{:.0f}", r.size.width, r.size.height)]++;
        rects.push_back({r.getMinX(), r.getMinY(), r.getMaxX(), r.getMaxY()});
    }
    std::string sizeList;
    for (auto const& [k, v] : sizes) sizeList += fmt::format(" {}:{}", k, v);
    log::info("Auto Deco: {} block-type objects: {} with a hitbox, {} without, {} No Touch/ours, {} rotated; sizes{}",
              solidType, rects.size(), noHitbox, skipped, rotated, sizeList);
    return rects;
}

std::string songPath(LevelEditorLayer* lel) {
    if (!lel || !lel->m_level) return "";
    std::error_code ec;
    if (lel->m_level->m_songID > 0) {
        std::string path = MusicDownloadManager::sharedState()->pathForSong(lel->m_level->m_songID);
        return std::filesystem::exists(path, ec) ? path : "";
    }
    std::string name = lel->m_level->getAudioFileName();
    if (name.empty()) return "";
    std::string full = CCFileUtils::sharedFileUtils()->fullPathForFilename(name.c_str(), false);
    return std::filesystem::exists(full, ec) ? full : "";
}

// ---------------------------------------------------------------------------
// Song analysis (on a worker thread, with its own FMOD instance)
// ---------------------------------------------------------------------------

struct SongAnalysis {
    enum class Status { Idle, Running, Done, Failed };
    std::mutex mutex;
    std::string path;
    Status status = Status::Idle;
    beats::Track track;
    std::string error;
};

SongAnalysis& song() {
    static SongAnalysis s;
    return s;
}

bool decodeMono(std::string const& path, std::vector<float>& out, double& rate, std::string& error) {
    FMOD::System* sys = nullptr;
    if (FMOD::System_Create(&sys) != FMOD_OK || !sys) {
        error = "could not start the audio decoder";
        return false;
    }
    sys->setOutput(FMOD_OUTPUTTYPE_NOSOUND_NRT);
    if (sys->init(1, FMOD_INIT_NORMAL, nullptr) != FMOD_OK) {
        sys->release();
        error = "could not start the audio decoder";
        return false;
    }
    FMOD::Sound* snd = nullptr;
    if (sys->createSound(path.c_str(), FMOD_OPENONLY | FMOD_ACCURATETIME, nullptr, &snd) != FMOD_OK || !snd) {
        sys->close();
        sys->release();
        error = "could not open the song file";
        return false;
    }

    FMOD_SOUND_FORMAT format = FMOD_SOUND_FORMAT_NONE;
    int channels = 0, bits = 0;
    float frequency = 0.f;
    snd->getFormat(nullptr, &format, &channels, &bits);
    snd->getDefaults(&frequency, nullptr);
    const int bytesPer = bits / 8;
    bool ok = channels > 0 && bytesPer > 0 && frequency > 0 &&
              (format == FMOD_SOUND_FORMAT_PCM8 || format == FMOD_SOUND_FORMAT_PCM16 ||
               format == FMOD_SOUND_FORMAT_PCM24 || format == FMOD_SOUND_FORMAT_PCM32 ||
               format == FMOD_SOUND_FORMAT_PCMFLOAT);
    if (!ok) error = "unsupported audio format";

    // decimate to ~11 kHz, which is plenty for beats
    const int decim = ok ? std::max(1, static_cast<int>(std::lround(frequency / 11025.f))) : 1;
    rate = frequency / decim;
    const size_t maxFrames = static_cast<size_t>(frequency) * 60 * 12; // 12 minutes
    std::vector<char> buf(1 << 16);
    double acc = 0.0;
    int accN = 0;
    size_t frames = 0;
    const size_t frameBytes = ok ? static_cast<size_t>(bytesPer) * channels : 1;
    while (ok && frames < maxFrames) {
        unsigned int read = 0;
        const unsigned int want = static_cast<unsigned int>(buf.size() / frameBytes * frameBytes);
        FMOD_RESULT r = snd->readData(buf.data(), want, &read);
        for (size_t at = 0; at + frameBytes <= read; at += frameBytes) {
            double sum = 0.0;
            for (int c = 0; c < channels; ++c) {
                const char* s = buf.data() + at + static_cast<size_t>(c) * bytesPer;
                double v = 0.0;
                switch (format) {
                    case FMOD_SOUND_FORMAT_PCM8: v = static_cast<signed char>(s[0]) / 128.0; break;
                    case FMOD_SOUND_FORMAT_PCM16: {
                        int16_t x;
                        std::memcpy(&x, s, 2);
                        v = x / 32768.0;
                        break;
                    }
                    case FMOD_SOUND_FORMAT_PCM24: {
                        int32_t x = static_cast<unsigned char>(s[0]) | (static_cast<unsigned char>(s[1]) << 8) |
                                    (static_cast<signed char>(s[2]) * 65536);
                        v = x / 8388608.0;
                        break;
                    }
                    case FMOD_SOUND_FORMAT_PCM32: {
                        int32_t x;
                        std::memcpy(&x, s, 4);
                        v = x / 2147483648.0;
                        break;
                    }
                    default: {
                        float x;
                        std::memcpy(&x, s, 4);
                        v = x;
                        break;
                    }
                }
                sum += v;
            }
            acc += sum / channels;
            if (++accN == decim) {
                out.push_back(static_cast<float>(acc / decim));
                acc = 0.0;
                accN = 0;
            }
            ++frames;
        }
        if (r != FMOD_OK || read == 0) break;
    }
    snd->release();
    sys->close();
    sys->release();
    if (ok && out.size() < static_cast<size_t>(rate * 4)) {
        ok = false;
        error = "the song is too short";
    }
    return ok;
}

void startAnalysis(std::string const& path) {
    auto& s = song();
    {
        std::lock_guard lock(s.mutex);
        if (s.path == path && s.status != SongAnalysis::Status::Idle) return;
        s.path = path;
        s.status = SongAnalysis::Status::Running;
        s.track = {};
        s.error.clear();
    }
    std::thread([path] {
        std::vector<float> mono;
        double rate = 0.0;
        std::string error;
        beats::Track track;
        const bool ok = decodeMono(path, mono, rate, error);
        if (ok) track = beats::track(mono.data(), mono.size(), rate);
        auto& s = song();
        std::lock_guard lock(s.mutex);
        if (s.path != path) return; // a newer request replaced this one
        if (ok) {
            s.track = std::move(track);
            s.status = SongAnalysis::Status::Done;
        } else {
            s.error = error;
            s.status = SongAnalysis::Status::Failed;
        }
    }).detach();
}

// Beats to pulse on, in level time, with a short description for the menu.
struct BeatPlan {
    std::vector<double> times;
    std::vector<bool> downbeat;
    float interval = 0.5f;
    std::string status;
    bool pending = false;
};

BeatPlan planBeats(LevelEditorLayer* lel) {
    BeatPlan plan;
    if (!lel || !lel->m_levelSettings) return plan;
    const std::string source = Mod::get()->getSettingValue<std::string>("beat-source");
    if (source == "Off") {
        plan.status = "Song pulses are off in the mod settings";
        return plan;
    }
    const double offset = lel->m_levelSettings->m_songOffset;

    auto finish = [&](std::vector<double> const& songTimes, std::vector<bool> const& down, double shift) {
        for (size_t k = 0; k < songTimes.size(); ++k) {
            const double t = songTimes[k] - shift;
            if (t < 0) continue;
            plan.times.push_back(t);
            plan.downbeat.push_back(k < down.size() ? static_cast<bool>(down[k]) : k % 4 == 0);
        }
        if (plan.times.size() >= 2) {
            std::vector<double> d;
            for (size_t k = 1; k < plan.times.size(); ++k) d.push_back(plan.times[k] - plan.times[k - 1]);
            std::nth_element(d.begin(), d.begin() + d.size() / 2, d.end());
            plan.interval = static_cast<float>(d[d.size() / 2]);
        }
    };

    // guidelines are already in level time
    auto guides = beats::parseGuidelines(lel->m_levelSettings->m_guidelineString);
    if (source == "Guidelines" || (source == "Auto" && guides.size() >= 8)) {
        if (guides.empty()) {
            plan.status = "No guidelines in this level";
            return plan;
        }
        std::vector<bool> down;
        for (size_t k = 0; k < guides.size(); ++k) down.push_back(k % 4 == 0);
        finish(guides, down, 0.0);
        plan.status = fmt::format("Pulses on {} guidelines", plan.times.size());
        return plan;
    }

    if (source == "Fixed tempo") {
        const double bpm = settingFloat("fixed-bpm");
        const double first = settingFloat("first-beat");
        auto t = beats::fromTempo(bpm, first, 60.0 * 15);
        finish(t.times, t.downbeat, offset);
        plan.status = fmt::format("Pulses at {:.0f} BPM (fixed tempo)", bpm);
        return plan;
    }

    // song analysis
    const std::string path = songPath(lel);
    if (path.empty()) {
        plan.status = "Song file not found (download the song first)";
        return plan;
    }
    startAnalysis(path);
    auto& s = song();
    std::lock_guard lock(s.mutex);
    switch (s.status) {
        case SongAnalysis::Status::Running:
            plan.pending = true;
            plan.status = "Listening to the song...";
            break;
        case SongAnalysis::Status::Failed:
            plan.status = "Could not read the song: " + s.error;
            break;
        case SongAnalysis::Status::Done:
            if (s.track.times.empty() || s.track.confidence < 2.0) {
                plan.status = "No clear beat in the song - pulses off";
            } else {
                finish(s.track.times, s.track.downbeat, offset);
                plan.status = fmt::format("Pulses on the song's beat ({:.0f} BPM)", s.track.bpm);
            }
            break;
        default: break;
    }
    return plan;
}

// ---------------------------------------------------------------------------
// Start colours
// ---------------------------------------------------------------------------

struct ColorSnapshot {
    int channel;
    ccColor3B from, to;
    float fromOpacity, toOpacity;
    bool blending;
    int playerColor;
    int copyID;
};

void refreshColors(LevelEditorLayer* lel) {
    lel->transferDefaultColors(lel->m_levelSettings->m_effectManager, lel->m_effectManager);
    lel->updateLevelColors();
    if (lel->m_objects) lel->updateObjectColors(lel->m_objects);
}

std::vector<ColorSnapshot> setStartColors(LevelEditorLayer* lel, std::vector<deco::ChannelColor> const& colors) {
    std::vector<ColorSnapshot> snaps;
    if (!lel || !lel->m_levelSettings || !lel->m_levelSettings->m_effectManager) return snaps;
    auto fx = lel->m_levelSettings->m_effectManager;
    for (auto const& c : colors) {
        auto action = fx->getColorAction(c.channel);
        if (!action) continue;
        snaps.push_back({c.channel, action->m_fromColor, action->m_toColor, action->m_fromOpacity, action->m_toOpacity,
                         action->m_blending, action->m_playerColor, action->m_copyID});
        action->m_fromColor = action->m_toColor = action->m_color = toCC(c.color);
        action->m_fromOpacity = action->m_toOpacity = action->m_currentOpacity = c.opacity;
        action->m_blending = false;
        action->m_playerColor = 0;
        action->m_copyID = 0;
    }
    refreshColors(lel);
    return snaps;
}

void restoreStartColors(LevelEditorLayer* lel, std::vector<ColorSnapshot> const& snaps) {
    if (!lel || !lel->m_levelSettings || !lel->m_levelSettings->m_effectManager || snaps.empty()) return;
    auto fx = lel->m_levelSettings->m_effectManager;
    for (auto const& s : snaps) {
        auto action = fx->getColorAction(s.channel);
        if (!action) continue;
        action->m_fromColor = s.from;
        action->m_toColor = s.to;
        action->m_color = s.from;
        action->m_fromOpacity = s.fromOpacity;
        action->m_toOpacity = s.toOpacity;
        action->m_currentOpacity = s.fromOpacity;
        action->m_blending = s.blending;
        action->m_playerColor = s.playerColor;
        action->m_copyID = s.copyID;
    }
    refreshColors(lel);
}

// ---------------------------------------------------------------------------
// Applying a style
// ---------------------------------------------------------------------------

struct Choice {
    int style = 0;
    deco::Palette palette;
    bool blocks = true, details = true, background = true, pulses = true, selectionOnly = false;
    unsigned seed = 1;
};

Choice& choice() {
    static Choice c = [] {
        Choice c;
        c.style = std::clamp(Mod::get()->getSavedValue<int>("style", 0), 0, static_cast<int>(styles().size()) - 1);
        c.palette = styles()[c.style].colors;
        c.blocks = Mod::get()->getSavedValue<bool>("blocks", true);
        c.details = Mod::get()->getSavedValue<bool>("details", true);
        c.background = Mod::get()->getSavedValue<bool>("background", true);
        c.pulses = Mod::get()->getSavedValue<bool>("pulses", true);
        return c;
    }();
    return c;
}

void saveChoice() {
    auto const& c = choice();
    Mod::get()->setSavedValue("style", c.style);
    Mod::get()->setSavedValue("blocks", c.blocks);
    Mod::get()->setSavedValue("details", c.details);
    Mod::get()->setSavedValue("background", c.background);
    Mod::get()->setSavedValue("pulses", c.pulses);
}

// What the last decoration changed, so a preview can be undone exactly.
struct Applied {
    int undoSteps = 0;
    std::vector<ColorSnapshot> colors;
};
std::optional<Applied> g_lastApplied;

bool deleteObjects(EditorUI* ui, std::vector<GameObject*> const& objs) {
    if (!ui || objs.empty()) return false;
    auto arr = CCArray::create();
    for (auto o : objs) arr->addObject(o);
    ui->deselectAll();
    ui->selectObjects(arr, true);
    ui->onDeleteSelected(nullptr); // one undo step
    ui->deselectAll();
    return true;
}

// Returns an error message, or empty on success. `created` receives how many
// objects actually made it into the level.
std::string applyChoice(EditorUI* ui, deco::Counts* counts, size_t* created) {
    auto lel = LevelEditorLayer::get();
    if (!ui || !lel) return "Open a level in the editor first.";
    auto const& c = choice();

    auto rects = solidRects(c.selectionOnly ? ui->getSelectedObjects() : lel->m_objects);
    if (rects.empty()) {
        return c.selectionOnly ? "Select some solid blocks first, or turn off \"Only selection\"."
                               : "This level has no solid blocks to decorate yet.";
    }
    const auto origin = deco::gridOrigin(rects);
    auto cells = deco::cellsFromRects(rects, origin);
    if (cells.empty()) return "No full-size blocks found (slopes, slabs and rotated blocks are skipped).";

    const deco::Style style = safeStyle(styles()[c.style]);
    deco::Params p;
    p.solid = std::move(cells);
    p.origin = origin;
    p.anchors = buildAnchors(style);
    p.itemFoot = itemFoot;
    p.seed = c.seed * 2654435761u + static_cast<unsigned>(c.style);
    p.fill = p.outline = p.glow = c.blocks;
    p.top = p.hang = p.side = p.ground = c.details;
    p.background = c.background;
    p.channels = deco::Channels(std::clamp(settingInt("channel-base"), 1, 994));
    p.colorTriggers = settingBool("color-triggers");
    p.pulses = c.pulses;
    if (c.pulses) {
        auto plan = planBeats(lel);
        p.beats = std::move(plan.times);
        p.downbeat = std::move(plan.downbeat);
        p.beatInterval = plan.interval;
        p.timeToX = [lel](double t) { return lel->posForTime(static_cast<float>(t)).x; };
    }

    auto objs = deco::build(style, c.palette, p);
    if (counts) *counts = deco::count(objs);
    const auto maxObjects = static_cast<size_t>(settingInt("max-objects"));
    if (objs.size() > maxObjects) {
        return fmt::format("This would add {} objects, over your limit of {} (see the mod settings).", objs.size(), maxObjects);
    }
    if (objs.empty()) return "Nothing to add: every part is switched off.";

    const std::string data = deco::toLevelString(objs, decoGroup(), settingInt("editor-layer"), true);
    log::info("Auto Deco: style '{}', {} solid cells from {} blocks, building {} objects ({} chars)", style.name,
              p.solid.size(), rects.size(), objs.size(), data.size());

    Applied applied;
    // replace any earlier Auto Deco
    if (deleteObjects(ui, taggedObjects(lel))) ++applied.undoSteps;

    // Create the objects directly: the editor's own paste refuses large
    // amounts, and a whole decorated level is large. One undo step.
    auto made = lel->createObjectsFromString(data, true, true);
    const size_t madeCount = made ? made->count() : 0;
    if (created) *created = madeCount;
    log::info("Auto Deco: {} of {} objects created", madeCount, objs.size());
    if (madeCount == 0) {
        g_lastApplied = std::move(applied); // so an earlier deco that was removed can still be undone
        return "The game did not create any objects. Please send your Geode log (geode/logs) so this can be fixed.";
    }
    lel->addToUndoList(UndoObject::createWithArray(made, UndoCommand::Paste), false);
    ++applied.undoSteps;

    applied.colors = setStartColors(lel, deco::channelColors(style, c.palette, p.channels));
    ui->deselectAll();
    ui->updateButtons();
    g_lastApplied = std::move(applied);
    return "";
}

void undoLastApply(EditorUI* ui) {
    if (!g_lastApplied || !ui) return;
    for (int k = 0; k < g_lastApplied->undoSteps; ++k) ui->undoLastAction(nullptr);
    restoreStartColors(LevelEditorLayer::get(), g_lastApplied->colors);
    g_lastApplied.reset();
    ui->updateButtons();
}

void openDecoPopup();

// ---------------------------------------------------------------------------
// Preview bar: Keep / Undo / Other style
// ---------------------------------------------------------------------------

void showPreviewBar(EditorUI* ui) {
    if (!ui) return;
    if (auto old = ui->getChildByID("preview-bar"_spr)) old->removeFromParent();
    const auto win = CCDirector::get()->getWinSize();

    auto bar = CCNode::create();
    bar->setID("preview-bar"_spr);
    bar->setPosition({win.width / 2.f, win.height - 100.f});

    auto bg = CCScale9Sprite::create("square02_small.png");
    bg->setContentSize({300.f, 44.f});
    bg->setOpacity(150);
    bar->addChild(bg);

    auto menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    bar->addChild(menu);

    auto keep = CCMenuItemExt::createSpriteExtra(ButtonSprite::create("Keep", "goldFont.fnt", "GJ_button_01.png", 0.7f),
                                                 [bar](auto) {
                                                     g_lastApplied.reset();
                                                     bar->removeFromParent();
                                                 });
    auto undo = CCMenuItemExt::createSpriteExtra(ButtonSprite::create("Undo", "goldFont.fnt", "GJ_button_06.png", 0.7f),
                                                 [bar, ui](auto) {
                                                     undoLastApply(ui);
                                                     bar->removeFromParent();
                                                 });
    auto change = CCMenuItemExt::createSpriteExtra(
        ButtonSprite::create("Other style", "goldFont.fnt", "GJ_button_04.png", 0.7f), [bar, ui](auto) {
            undoLastApply(ui);
            bar->removeFromParent();
            openDecoPopup();
        });
    keep->setPosition({-100.f, 0.f});
    undo->setPosition({-20.f, 0.f});
    change->setPosition({85.f, 0.f});
    menu->addChild(keep);
    menu->addChild(undo);
    menu->addChild(change);
    ui->addChild(bar, 200);
}

// ---------------------------------------------------------------------------
// The style menu
// ---------------------------------------------------------------------------

class DecoPopup : public Popup {
protected:
    CCLabelBMFont* m_name = nullptr;
    CCLabelBMFont* m_blurb = nullptr;
    CCLabelBMFont* m_beat = nullptr;
    std::vector<CCLayerColor*> m_swatches;
    CCMenu* m_menu = nullptr;
    bool m_waitingForSong = false;

    static constexpr float W = 420.f;
    static constexpr float H = 290.f;

public:
    static DecoPopup* create() {
        auto ret = new DecoPopup();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

protected:
    bool init() {
        if (!Popup::init(W, H)) return false;
        this->setTitle("Auto Deco");

        m_menu = CCMenu::create();
        m_menu->ignoreAnchorPointForPosition(false);
        m_menu->setAnchorPoint({0.f, 0.f});
        m_menu->setPosition({0.f, 0.f});
        m_menu->setContentSize({W, H});
        m_mainLayer->addChild(m_menu, 10);

        // style picker
        auto prev = CCMenuItemExt::createSpriteExtraWithFrameName("GJ_arrow_01_001.png", 0.7f, [this](auto) { this->step(-1); });
        auto nextSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
        nextSpr->setFlipX(true);
        nextSpr->setScale(0.7f);
        auto next = CCMenuItemExt::createSpriteExtra(nextSpr, [this](auto) { this->step(1); });
        prev->setPosition({40.f, H - 70.f});
        next->setPosition({W - 40.f, H - 70.f});
        m_menu->addChild(prev);
        m_menu->addChild(next);

        m_name = CCLabelBMFont::create("", "bigFont.fnt");
        m_name->setPosition({W / 2.f, H - 64.f});
        m_mainLayer->addChild(m_name);
        m_blurb = CCLabelBMFont::create("", "chatFont.fnt");
        m_blurb->setPosition({W / 2.f, H - 88.f});
        m_blurb->setScale(0.75f);
        m_mainLayer->addChild(m_blurb);

        // colours
        const char* names[] = {"Background", "Blocks", "Main", "Details"};
        for (int k = 0; k < 4; ++k) {
            auto holder = CCNode::create();
            holder->setContentSize({30.f, 30.f});
            auto border = CCLayerColor::create({0, 0, 0, 255}, 30.f, 30.f);
            holder->addChild(border);
            auto swatch = CCLayerColor::create({255, 255, 255, 255}, 26.f, 26.f);
            swatch->setPosition({2.f, 2.f});
            holder->addChild(swatch);
            m_swatches.push_back(swatch);
            auto btn = CCMenuItemExt::createSpriteExtra(holder, [this, k](auto) { this->pickColor(k); });
            const float x = W / 2.f + (k - 1.5f) * 80.f;
            btn->setPosition({x, H - 128.f});
            m_menu->addChild(btn);
            auto label = CCLabelBMFont::create(names[k], "bigFont.fnt");
            label->setScale(0.3f);
            label->setPosition({x, H - 152.f});
            m_mainLayer->addChild(label);
        }
        auto reset = CCMenuItemExt::createSpriteExtraWithFrameName("GJ_replayBtn_001.png", 0.45f, [this](auto) {
            choice().palette = styles()[choice().style].colors;
            this->refresh();
        });
        reset->setPosition({W / 2.f + 2.f * 80.f + 10.f, H - 128.f});
        m_menu->addChild(reset);

        // what to add
        auto& c = choice();
        this->addToggle("Blocks", &c.blocks, {60.f, 112.f});
        this->addToggle("Details", &c.details, {165.f, 112.f});
        this->addToggle("Background", &c.background, {270.f, 112.f});
        this->addToggle("Song pulses", &c.pulses, {60.f, 84.f});
        this->addToggle("Only selection", &c.selectionOnly, {165.f, 84.f});

        m_beat = CCLabelBMFont::create("", "chatFont.fnt");
        m_beat->setScale(0.65f);
        m_beat->setPosition({W / 2.f, 58.f});
        m_beat->setColor({190, 220, 255});
        m_mainLayer->addChild(m_beat);

        // actions
        auto shuffle = CCMenuItemExt::createSpriteExtra(ButtonSprite::create("Shuffle", "goldFont.fnt", "GJ_button_04.png", 0.6f),
                                                        [](auto) {
                                                            choice().seed += 1;
                                                            notify("New variation: press Preview to see it");
                                                        });
        auto remove = CCMenuItemExt::createSpriteExtra(ButtonSprite::create("Remove", "goldFont.fnt", "GJ_button_06.png", 0.6f),
                                                       [this](auto) { this->onRemove(); });
        auto preview = CCMenuItemExt::createSpriteExtra(ButtonSprite::create("Preview", "goldFont.fnt", "GJ_button_02.png", 0.75f),
                                                        [this](auto) { this->onApply(true); });
        auto decorate = CCMenuItemExt::createSpriteExtra(ButtonSprite::create("Decorate", "goldFont.fnt", "GJ_button_01.png", 0.75f),
                                                         [this](auto) { this->onApply(false); });
        shuffle->setPosition({55.f, 26.f});
        remove->setPosition({135.f, 26.f});
        preview->setPosition({240.f, 26.f});
        decorate->setPosition({345.f, 26.f});
        m_menu->addChild(shuffle);
        m_menu->addChild(remove);
        m_menu->addChild(preview);
        m_menu->addChild(decorate);

        auto settings = CCMenuItemExt::createSpriteExtraWithFrameName("GJ_optionsBtn_001.png", 0.5f,
                                                                      [](auto) { openSettingsPopup(Mod::get()); });
        settings->setPosition({W - 22.f, H - 22.f});
        m_menu->addChild(settings);

        this->refresh();
        this->updateBeatLabel();
        this->schedule(schedule_selector(DecoPopup::pollSong), 0.25f);
        return true;
    }

    void addToggle(const char* text, bool* value, CCPoint pos) {
        auto toggle = CCMenuItemExt::createTogglerWithStandardSprites(0.55f, [this, value](CCMenuItemToggler* t) {
            *value = !t->isToggled(); // the callback runs before the state flips
            saveChoice();
            if (value == &choice().pulses) this->updateBeatLabel();
        });
        toggle->toggle(*value);
        toggle->setPosition(pos);
        m_menu->addChild(toggle);
        auto label = CCLabelBMFont::create(text, "bigFont.fnt");
        label->setScale(0.32f);
        label->setAnchorPoint({0.f, 0.5f});
        label->setPosition({pos.x + 14.f, pos.y});
        m_mainLayer->addChild(label);
    }

    void step(int dir) {
        auto& c = choice();
        const int n = static_cast<int>(styles().size());
        c.style = ((c.style + dir) % n + n) % n;
        c.palette = styles()[c.style].colors;
        saveChoice();
        this->refresh();
    }

    void refresh() {
        auto const& c = choice();
        auto const& s = styles()[c.style];
        m_name->setString(fmt::format("{}  ({}/{})", s.name, c.style + 1, styles().size()).c_str());
        m_name->limitLabelWidth(W - 110.f, 0.6f, 0.2f);
        m_blurb->setString(s.blurb.c_str());
        const deco::RGB cols[] = {c.palette.background, c.palette.blocks, c.palette.primary, c.palette.secondary};
        for (size_t k = 0; k < m_swatches.size(); ++k) m_swatches[k]->setColor(toCC(cols[k]));
    }

    void pickColor(int which) {
        auto& p = choice().palette;
        deco::RGB* target = which == 0 ? &p.background : which == 1 ? &p.blocks : which == 2 ? &p.primary : &p.secondary;
        auto popup = ColorPickPopup::create(toCC(*target));
        popup->setCallback([this, target](ccColor4B const& col) {
            *target = {col.r, col.g, col.b};
            this->refresh();
        });
        popup->show();
    }

    void updateBeatLabel() {
        if (!choice().pulses) {
            m_beat->setString("Song pulses off");
            m_waitingForSong = false;
            return;
        }
        auto plan = planBeats(LevelEditorLayer::get());
        m_beat->setString(plan.status.c_str());
        m_beat->limitLabelWidth(W - 40.f, 0.65f, 0.3f);
        m_waitingForSong = plan.pending;
    }

    void pollSong(float) {
        if (m_waitingForSong) this->updateBeatLabel();
    }

    void onApply(bool preview) {
        if (choice().pulses && m_waitingForSong) {
            notify("Still listening to the song, one moment...", NotificationIcon::Loading);
            return;
        }
        auto ui = EditorUI::get();
        deco::Counts counts;
        size_t created = 0;
        const std::string error = applyChoice(ui, &counts, &created);
        if (!error.empty()) {
            notify(error, NotificationIcon::Error);
            return;
        }
        saveChoice();
        if (created < counts.total()) {
            notify(fmt::format("Auto Deco: only {} of {} objects were created", created, counts.total()), NotificationIcon::Warning);
        } else {
            notify(fmt::format("Auto Deco: added {} objects ({} details, {} background, {} triggers)", created, counts.details,
                               counts.background, counts.triggers),
                   NotificationIcon::Success);
        }
        this->onClose(nullptr);
        if (preview) {
            showPreviewBar(ui);
        } else {
            g_lastApplied.reset();
        }
    }

    void onRemove() {
        auto ui = EditorUI::get();
        auto tagged = taggedObjects(LevelEditorLayer::get());
        if (tagged.empty()) {
            notify("No Auto Deco in this level");
            return;
        }
        deleteObjects(ui, tagged);
        g_lastApplied.reset();
        notify(fmt::format("Removed {} Auto Deco objects (Undo brings them back)", tagged.size()), NotificationIcon::Success);
    }
};

void openDecoPopup() {
    if (!LevelEditorLayer::get() || !EditorUI::get()) return;
    if (auto old = EditorUI::get()->getChildByID("preview-bar"_spr)) {
        // an unconfirmed preview: keep it rather than lose track of it
        g_lastApplied.reset();
        old->removeFromParent();
    }
    if (auto popup = DecoPopup::create()) popup->show();
}

CCMenuItemSpriteExtra* makeEditorButton(float targetHeight) {
    auto label = CCLabelBMFont::create("Auto\nDeco", "bigFont.fnt");
    label->setAlignment(kCCTextAlignmentCenter);
    auto sprite = EditorButtonSprite::create(label, EditorBaseColor::Pink);
    auto button = CCMenuItemExt::createSpriteExtra(sprite, [](auto) { openDecoPopup(); });
    button->setID("auto-deco-button"_spr);
    const float have = button->getContentSize().height;
    if (targetHeight > 1.f && have > 1.f) button->setScale(targetHeight / have);
    button->m_baseScale = button->getScale();
    return button;
}

} // namespace

class $modify(AutoDecoUI, EditorUI) {
    bool init(LevelEditorLayer* editorLayer) {
        if (!EditorUI::init(editorLayer)) return false;
        g_lastApplied.reset();

        if (auto menu = this->getChildByID("editor-buttons-menu"); menu && menu->getLayout()) {
            float h = 0.f;
            if (auto sibling = menu->getChildByType<CCMenuItemSpriteExtra>(0)) h = sibling->getScaledContentSize().height;
            menu->addChild(makeEditorButton(h));
            menu->updateLayout();
        } else if (m_copyBtn && m_copyBtn->getParent()) {
            auto menu = m_copyBtn->getParent();
            auto button = makeEditorButton(m_copyBtn->getScaledContentSize().height);
            const CCPoint copyPos = m_copyBtn->getPosition();
            float leftEdge = copyPos.x - m_copyBtn->getScaledContentSize().width / 2.f;
            for (auto child : CCArrayExt<CCNode*>(menu->getChildren())) {
                if (!child->isVisible() || std::fabs(child->getPositionY() - copyPos.y) > 5.f) continue;
                leftEdge = std::min(leftEdge, child->getPositionX() - child->getScaledContentSize().width / 2.f);
            }
            button->setPosition({leftEdge - 4.f - button->getScaledContentSize().width / 2.f, copyPos.y});
            menu->addChild(button);
        } else {
            auto ownMenu = CCMenu::create();
            ownMenu->setID("auto-deco-menu"_spr);
            const auto win = CCDirector::get()->getWinSize();
            ownMenu->setPosition({win.width - 130.f, win.height - 25.f});
            ownMenu->addChild(makeEditorButton(30.f));
            this->addChild(ownMenu, 100);
        }
        return true;
    }
};

class $modify(AutoDecoPause, EditorPauseLayer) {
    void customSetup() {
        EditorPauseLayer::customSetup();
        auto menu = CCMenu::create();
        menu->setID("auto-deco-pause-menu"_spr);
        menu->setPosition({0.f, 0.f});
        auto button = CCMenuItemExt::createSpriteExtra(
            ButtonSprite::create("Auto Deco", "goldFont.fnt", "GJ_button_03.png", 0.7f), [this](auto) {
                this->onResume(nullptr);
                Loader::get()->queueInMainThread([] { openDecoPopup(); });
            });
        button->setPosition({70.f, 30.f});
        menu->addChild(button);
        this->addChild(menu, 50);
    }
};
