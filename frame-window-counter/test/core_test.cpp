// Host-side tests for the core. No game needed:
//   c++ -std=c++20 -I src test/core_test.cpp -o core_test && ./core_test
#include "fwc_core.hpp"
#include "fixtures.hpp"

#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace fwc;

static int failures = 0;

#define CHECK(cond)                                             \
    do {                                                        \
        if (!(cond)) {                                          \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond); \
            ++failures;                                         \
        }                                                       \
    } while (0)

// ---------------------------------------------------------------------------
// A toy level: a "wave" that rises one unit per tick while the button is held
// and falls one unit per tick otherwise. It dies when it leaves a corridor.
// ---------------------------------------------------------------------------

struct ToyLevel {
    std::vector<int> lo, hi; // corridor per tick
    int endTick = 0;
};

struct ToySim : Sim {
    const ToyLevel& level;
    int t = 0;
    int y = 0;
    bool held = false;
    struct Saved {
        int t, y;
    };
    std::vector<Saved> saved;
    long steps = 0;
    int restoreError = 0; // simulates an unfaithful checkpoint

    explicit ToySim(const ToyLevel& l) : level(l) {}

    void restart() override {
        held = false;
        if (saved.empty()) {
            t = 0;
            y = 0;
        } else {
            t = saved.back().t;
            y = saved.back().y + restoreError;
        }
    }
    int tick() override { return t; }
    void input(const Input& in) override {
        if (!in.p2 && in.button == 1) held = in.down;
    }
    StepResult step() override {
        ++steps;
        y += held ? 1 : -1;
        ++t;
        if (t < static_cast<int>(level.lo.size()) && (y < level.lo[t] || y > level.hi[t])) return StepResult::Dead;
        if (t >= level.endTick) return StepResult::Complete;
        return StepResult::Alive;
    }
    void checkpoint() override { saved.push_back({t, y}); }
    void dropCheckpoint() override {
        if (!saved.empty()) saved.pop_back();
    }
};

static unsigned rngState = 12345;
static int rnd(int lo, int hi) {
    rngState = rngState * 1103515245u + 12345u;
    return lo + static_cast<int>((rngState >> 16) % static_cast<unsigned>(hi - lo + 1));
}

// Independent reference: replay from the very start with one input moved.
static bool survives(const ToyLevel& level, std::vector<Input> inputs, long index, int shift, int until) {
    if (index >= 0) inputs[static_cast<size_t>(index)].tick += shift;
    sortInputs(inputs);
    ToySim sim(level);
    sim.restart();
    size_t next = 0;
    while (sim.tick() < until) {
        while (next < inputs.size() && inputs[next].tick <= sim.tick()) sim.input(inputs[next++]);
        StepResult r = sim.step();
        if (r == StepResult::Dead) return false;
        if (r == StepResult::Complete) return true;
    }
    return true;
}

static std::vector<int> bruteForce(const ToyLevel& level, const std::vector<Input>& inputs, const CountOptions& opt) {
    std::vector<int> out(inputs.size(), 0);
    for (size_t i = 0; i < inputs.size(); ++i) {
        if (!inputs[i].down && !opt.countReleases) continue;
        const int f = inputs[i].tick;
        int prev = -1, next = INT_MAX, nextAny = f;
        for (size_t k = 0; k < i; ++k) {
            if (inputs[k].p2 == inputs[i].p2 && inputs[k].button == inputs[i].button) prev = inputs[k].tick;
        }
        for (size_t k = i + 1; k < inputs.size(); ++k) {
            if (inputs[k].p2 == inputs[i].p2 && inputs[k].button == inputs[i].button) {
                next = inputs[k].tick;
                break;
            }
        }
        for (size_t k = i + 1; k < inputs.size(); ++k) {
            if (inputs[k].tick > f) {
                nextAny = inputs[k].tick;
                break;
            }
        }
        auto horizon = [&](int d) { return std::max({f, f + d, nextAny}) + opt.horizon; };
        auto valid = [&](int d) { return f + d >= 0 && f + d > prev && f + d < next; };
        int total = 1;
        for (int d = 1; total < opt.maxWindow && valid(d) && survives(level, inputs, static_cast<long>(i), d, horizon(d)); ++d) ++total;
        for (int d = -1; total < opt.maxWindow && valid(d) && survives(level, inputs, static_cast<long>(i), d, horizon(d)); --d) ++total;
        out[i] = total;
    }
    return out;
}

static std::vector<int> runCounter(ToySim& sim, const std::vector<Input>& inputs, CountOptions opt, Counter** keep = nullptr) {
    static Counter* last = nullptr;
    delete last;
    last = new Counter(inputs, opt);
    int guard = 0;
    while (last->advance(sim, 97) && ++guard < 10000000) {} // odd budget: runs get split across calls
    if (keep) *keep = last;
    return last->windows();
}

// Parses a fixture written by a reference library and compares every input.
static void checkFixture(
    const char* name, const std::uint8_t* bytes, size_t size, const fixtures::Expected* expected, size_t count,
    double tps, const char* format
) {
    ParseResult r = parseMacro(bytes, size);
    if (!r.ok) {
        std::printf("FAIL %s: %s\n", name, r.error.c_str());
        ++failures;
        return;
    }
    bool same = r.macro.format == format && r.macro.inputs.size() == count;
    for (size_t k = 0; same && k < count; ++k) {
        const Input& in = r.macro.inputs[k];
        same = in.tick == frameToTick(static_cast<double>(expected[k].frame), tps) && in.button == expected[k].button &&
               in.p2 == expected[k].p2 && in.down == expected[k].down;
        if (!same) std::printf("  %s: input %zu differs\n", name, k);
    }
    if (!same) {
        std::printf("FAIL %s: format %s, %zu inputs (wanted %s, %zu)\n", name, r.macro.format.c_str(),
                    r.macro.inputs.size(), format, count);
        ++failures;
    }

    // damaged files must be rejected or read safely, never crash
    for (size_t cut = 0; cut < size; cut += (size > 400 ? 7 : 1)) parseMacro(bytes, cut);
    std::vector<std::uint8_t> copy(bytes, bytes + size);
    unsigned state = 99;
    for (int round = 0; round < 300; ++round) {
        state = state * 1103515245u + 12345u;
        size_t at = (state >> 8) % size;
        std::uint8_t old = copy[at];
        copy[at] = static_cast<std::uint8_t>(state >> 20);
        parseMacro(copy.data(), copy.size());
        copy[at] = old;
    }
}

#define FIXTURE(name, format) \
    checkFixture(#name, fixtures::name##_bytes, sizeof(fixtures::name##_bytes), fixtures::name##_expected, \
                 fixtures::name##_count, fixtures::name##_tps, format)

static void appendVarint(std::vector<std::uint8_t>& out, std::uint64_t v) {
    do {
        std::uint8_t byte = v & 0x7f;
        v >>= 7;
        if (v) byte |= 0x80;
        out.push_back(byte);
    } while (v);
}
static void appendString(std::vector<std::uint8_t>& out, const std::string& s) {
    appendVarint(out, s.size());
    out.insert(out.end(), s.begin(), s.end());
}
static void appendDouble(std::vector<std::uint8_t>& out, double d) {
    std::uint64_t raw;
    std::memcpy(&raw, &d, 8);
    for (int k = 7; k >= 0; --k) out.push_back(static_cast<std::uint8_t>(raw >> (k * 8)));
}

int main() {
    // --- build a level whose corridor hugs a reference run ---------------------
    std::vector<Input> inputs;
    int t = 30;
    bool down = true;
    for (int k = 0; k < 70; ++k) {
        inputs.push_back({t, 1, false, down});
        down = !down;
        t += rnd(5, 45);
    }
    // a few player-2 inputs that the toy ignores, to exercise ordering
    inputs.push_back({200, 1, true, true});
    inputs.push_back({260, 1, true, false});
    sortInputs(inputs);

    ToyLevel level;
    level.endTick = inputs.back().tick + 300;
    {
        // trace the reference run
        ToyLevel open;
        open.endTick = level.endTick;
        ToySim trace(open);
        size_t next = 0;
        std::vector<int> ys(static_cast<size_t>(level.endTick) + 1, 0);
        while (trace.tick() < level.endTick) {
            while (next < inputs.size() && inputs[next].tick <= trace.tick()) trace.input(inputs[next++]);
            trace.step();
            ys[static_cast<size_t>(trace.tick())] = trace.y;
        }
        level.lo.resize(ys.size());
        level.hi.resize(ys.size());
        int below = 4, above = 4;
        for (size_t k = 0; k < ys.size(); ++k) {
            if (k % 20 == 0) {
                below = rnd(1, 14);
                above = rnd(1, 14);
            }
            level.lo[k] = ys[k] - below;
            level.hi[k] = ys[k] + above;
        }
    }

    CountOptions opt;
    opt.maxWindow = 10;
    opt.horizon = 48;
    opt.checkpointGap = 120;

    const std::vector<int> truth = bruteForce(level, inputs, opt);
    {
        // the scenario should actually contain a spread of windows
        std::vector<int> counts = tally(truth, opt.maxWindow);
        int distinct = 0;
        for (int w = 1; w <= opt.maxWindow; ++w) distinct += counts[static_cast<size_t>(w)] > 0;
        CHECK(distinct >= 4);
        CHECK(counts[1] + counts[2] + counts[3] > 0);
    }

    // --- counter with checkpoints matches the brute-force reference -----------
    long stepsWithCheckpoints = 0;
    {
        ToySim sim(level);
        Counter* c = nullptr;
        std::vector<int> got = runCounter(sim, inputs, opt, &c);
        CHECK(c->finished() && !c->failed());
        CHECK(got == truth);
        CHECK(c->checkpoints() > 3);
        CHECK(c->progress() == 1.0);
        stepsWithCheckpoints = sim.steps;
    }

    // --- counter without checkpoints matches too, and costs far more ----------
    {
        ToySim sim(level);
        CountOptions slow = opt;
        slow.useCheckpoints = false;
        Counter* c = nullptr;
        std::vector<int> got = runCounter(sim, inputs, slow, &c);
        CHECK(!c->failed());
        CHECK(got == truth);
        CHECK(sim.saved.empty());
        CHECK(sim.steps > stepsWithCheckpoints * 3);
    }

    // --- unfaithful checkpoints are detected and abandoned --------------------
    {
        ToySim sim(level);
        sim.restoreError = 60; // restoring puts the player far outside the corridor
        Counter* c = nullptr;
        std::vector<int> got = runCounter(sim, inputs, opt, &c);
        CHECK(!c->failed());
        CHECK(got == truth);
        CHECK(!c->usingCheckpoints());
        CHECK(sim.saved.empty());
    }

    // --- releases can be skipped ----------------------------------------------
    {
        ToySim sim(level);
        CountOptions pressOnly = opt;
        pressOnly.countReleases = false;
        std::vector<int> got = runCounter(sim, inputs, pressOnly);
        CHECK(got == bruteForce(level, inputs, pressOnly));
        for (size_t k = 0; k < inputs.size(); ++k) {
            if (!inputs[k].down) CHECK(got[k] == 0);
        }
    }

    // --- a macro that does not survive unchanged is reported -------------------
    {
        ToyLevel deadly = level;
        for (size_t k = 400; k < 420 && k < deadly.hi.size(); ++k) deadly.hi[k] = deadly.lo[k] - 1;
        ToySim sim(deadly);
        Counter* c = nullptr;
        runCounter(sim, inputs, opt, &c);
        CHECK(c->failed());
        CHECK(c->error().find("tick 400") != std::string::npos);
    }

    // --- empty macro ------------------------------------------------------------
    {
        ToySim sim(level);
        Counter c({}, opt);
        CHECK(!c.advance(sim, 10));
        CHECK(c.finished() && !c.failed());
    }

    // --- macro parsing: legacy GDR JSON ------------------------------------------
    {
        std::string json =
            "{\"author\":\"x\",\"bot\":{\"name\":\"b\",\"version\":\"1\"},\"framerate\":240.0,"
            "\"inputs\":[{\"frame\":100,\"btn\":1,\"2p\":false,\"down\":true},"
            "{\"frame\":90,\"btn\":1,\"2p\":true,\"down\":true,\"extra\":[1,2,{\"a\":null}]},"
            "{\"frame\":120,\"btn\":1,\"2p\":false,\"down\":false}],\"level\":{\"id\":1,\"name\":\"t\\u00e9st\"}}";
        ParseResult r = parseMacro(reinterpret_cast<const std::uint8_t*>(json.data()), json.size());
        CHECK(r.ok);
        CHECK(r.macro.format == "GDR JSON");
        CHECK(r.macro.inputs.size() == 3);
        CHECK(r.macro.inputs[0].tick == 90 && r.macro.inputs[0].p2);
        CHECK(r.macro.inputs[1].tick == 100 && r.macro.inputs[1].down && !r.macro.inputs[1].p2);
        CHECK(r.macro.inputs[2].tick == 120 && !r.macro.inputs[2].down);
        CHECK(r.macro.levelId == 1);
    }
    {
        // 480 fps macro: frames are halved onto 240 tps ticks
        std::string json = "{\"framerate\":480,\"inputs\":[{\"frame\":200,\"btn\":1,\"2p\":false,\"down\":true}]}";
        ParseResult r = parseMacro(reinterpret_cast<const std::uint8_t*>(json.data()), json.size());
        CHECK(r.ok && r.macro.inputs[0].tick == 100);
    }
    {
        std::string bad = "{\"inputs\": [ {\"frame\": 1, ";
        CHECK(!parseMacro(reinterpret_cast<const std::uint8_t*>(bad.data()), bad.size()).ok);
        std::string none = "{\"inputs\": []}";
        CHECK(!parseMacro(reinterpret_cast<const std::uint8_t*>(none.data()), none.size()).ok);
        std::string junk = "hello there";
        CHECK(!parseMacro(reinterpret_cast<const std::uint8_t*>(junk.data()), junk.size()).ok);
    }

    // --- macro parsing: legacy GDR MessagePack -----------------------------------
    {
        // {"framerate": 240.0 (float64), "inputs": [{"frame": 300 (uint16), "btn": 1, "2p": false, "down": true}]}
        std::vector<std::uint8_t> mp = {0x82, 0xa9, 'f', 'r', 'a', 'm', 'e', 'r', 'a', 't', 'e', 0xcb};
        appendDouble(mp, 240.0);
        std::vector<std::uint8_t> rest = {
            0xa6, 'i', 'n', 'p', 'u', 't', 's', 0x91, 0x84,
            0xa5, 'f', 'r', 'a', 'm', 'e', 0xcd, 0x01, 0x2c,
            0xa3, 'b', 't', 'n', 0x01,
            0xa2, '2', 'p', 0xc2,
            0xa4, 'd', 'o', 'w', 'n', 0xc3,
        };
        mp.insert(mp.end(), rest.begin(), rest.end());
        ParseResult r = parseMacro(mp.data(), mp.size());
        CHECK(r.ok);
        CHECK(r.macro.format == "GDR MessagePack");
        CHECK(r.macro.inputs.size() == 1 && r.macro.inputs[0].tick == 300 && r.macro.inputs[0].down);
    }

    // --- macro parsing: binary GDR ------------------------------------------------
    {
        std::vector<std::uint8_t> bin = {'G', 'D', 'R'};
        appendVarint(bin, 2);          // version
        appendString(bin, "");         // input tag
        appendString(bin, "author");
        appendString(bin, "description");
        for (int k = 0; k < 4; ++k) bin.push_back(0); // duration
        appendVarint(bin, 22091);      // game version
        appendDouble(bin, 240.0);
        appendVarint(bin, 7);          // seed
        appendVarint(bin, 0);          // coins
        appendVarint(bin, 0);          // ldm
        appendVarint(bin, 0);          // platformer
        appendString(bin, "bot");
        appendVarint(bin, 3);
        appendVarint(bin, 123456);     // level id
        appendString(bin, "level");
        appendVarint(bin, 0);          // extension size
        appendVarint(bin, 1);          // deaths
        appendVarint(bin, 55);
        appendVarint(bin, 4);          // inputs
        appendVarint(bin, 3);          // player 1 inputs
        // player 1: press 100, release 130, press 400 (delta << 1 | down)
        appendVarint(bin, (100u << 1) | 1);
        appendVarint(bin, (30u << 1) | 0);
        appendVarint(bin, (270u << 1) | 1);
        // player 2: press 120
        appendVarint(bin, (120u << 1) | 1);
        ParseResult r = parseMacro(bin.data(), bin.size());
        CHECK(r.ok);
        CHECK(r.macro.format == "GDR2");
        CHECK(r.macro.inputs.size() == 4);
        CHECK(r.macro.inputs[0].tick == 100 && r.macro.inputs[0].down && !r.macro.inputs[0].p2);
        CHECK(r.macro.inputs[1].tick == 120 && r.macro.inputs[1].p2);
        CHECK(r.macro.inputs[2].tick == 130 && !r.macro.inputs[2].down);
        CHECK(r.macro.inputs[3].tick == 400);
        CHECK(r.macro.levelId == 123456);

        std::vector<std::uint8_t> cut(bin.begin(), bin.begin() + 20);
        CHECK(!parseMacro(cut.data(), cut.size()).ok);
    }

    // --- files written by the reference GDR and SLC libraries --------------------------
    FIXTURE(gdr2_classic, "GDR2");
    FIXTURE(gdr2_platformer_480, "GDR2");
    FIXTURE(gdr2_extensions, "GDR2");
    FIXTURE(slc2_basic, "SLC2");
    FIXTURE(slc3_basic, "SLC3");
    {
        ParseResult r = parseMacro(fixtures::gdr2_classic_bytes, sizeof(fixtures::gdr2_classic_bytes));
        CHECK(r.ok && r.macro.levelId == 4242);
    }
    // tick-rate changes and restarts: only the last attempt is kept, timed in seconds
    for (int which = 0; which < 2; ++which) {
        ParseResult r = which == 0
            ? parseMacro(fixtures::slc2_tps_restart_bytes, sizeof(fixtures::slc2_tps_restart_bytes))
            : parseMacro(fixtures::slc3_tps_restart_bytes, sizeof(fixtures::slc3_tps_restart_bytes));
        CHECK(r.ok);
        CHECK(r.macro.format == (which == 0 ? "SLC2" : "SLC3"));
        CHECK(r.macro.inputs.size() == 3);
        if (r.macro.inputs.size() == 3) {
            CHECK((r.macro.inputs[0] == Input{100, 1, false, true}));
            CHECK((r.macro.inputs[1] == Input{480, 1, false, false}));
            CHECK((r.macro.inputs[2] == Input{481, 1, true, true}));
        }
    }

    // --- .fwc.json round trip -----------------------------------------------------
    {
        std::string saved = writeFwcJson(inputs, truth, opt.maxWindow, opt.horizon);
        ParseResult r = parseMacro(reinterpret_cast<const std::uint8_t*>(saved.data()), saved.size());
        CHECK(r.ok);
        CHECK(r.macro.format == "FWC");
        CHECK(r.macro.inputs == inputs);
        CHECK(r.macro.windows == truth);

        std::vector<int> restored;
        size_t matched = applySavedWindows(r.macro, inputs, restored);
        CHECK(matched == inputs.size());
        CHECK(restored == truth);

        // a different macro only picks up the inputs that still line up
        std::vector<Input> other = inputs;
        other[5].tick += 1;
        applySavedWindows(r.macro, other, restored);
        CHECK(restored[5] == 0 && restored[6] == truth[6]);
    }

    // --- precision estimate ---------------------------------------------------------
    {
        std::vector<Input> two = {{240, 1, false, true}, {480, 1, false, false}};
        double easy = precisionEstimate(two, {6, 6}, 10, 3.0);
        double hard = precisionEstimate(two, {1, 1}, 10, 3.0);
        CHECK(easy > 0 && hard > easy);
        // six times narrower windows need six times the precision
        CHECK(std::fabs(hard / easy - 6.0) < 0.05);
        // at the estimate, the expected time is the 24 hour target
        CHECK(std::fabs(expectedHours(two, {1, 1}, 10, 3.0, 1.0, hard) - 24.0) < 0.1);
        // free or uncounted timings don't matter
        CHECK(precisionEstimate(two, {0, 10}, 10, 3.0) == 0.0);
    }

    if (failures == 0) std::printf("all core tests passed\n");
    return failures == 0 ? 0 : 1;
}
