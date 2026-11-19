// Tests for the style builder.
//   c++ -std=c++20 -I src test/build_test.cpp -o build_test && ./build_test
#include "styles.hpp"

#include <cstdio>
#include <set>
#include <string>

using namespace deco;

static int failures = 0;
#define CHECK(cond)                                             \
    do {                                                        \
        if (!(cond)) {                                          \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond); \
            ++failures;                                         \
        }                                                       \
    } while (0)

static bool near(float a, float b, float eps = 0.01f) { return std::fabs(a - b) <= eps; }

static size_t countRole(const std::vector<Obj>& v, Role r) {
    size_t n = 0;
    for (const auto& o : v) n += o.role == r;
    return n;
}

int main() {
    const auto styles = builtinStyles();
    CHECK(styles.size() == 20);
    {
        std::set<std::string> names;
        for (const auto& s : styles) names.insert(s.name);
        CHECK(names.size() == styles.size());
    }

    // a level: a floor of 10 blocks on the ground, a 3-block platform floating
    // 5 cells up, and a 2-high pillar.  Node coordinates: ground top is y = 90.
    CellSet solid;
    for (int i = 0; i < 10; ++i) solid.insert({i, 3});           // y 90..120
    for (int i = 3; i < 6; ++i) solid.insert({i, 9});            // y 270..300
    solid.insert({8, 4});
    solid.insert({8, 5});

    Params p;
    p.solid = solid;
    p.origin = {0, 0};
    p.anchors.outlineEdge = {Up, HALF};
    p.anchors.outlineInner = {TopLeft, {-HALF, HALF}};
    p.anchors.glowEdge = {Down, HALF};
    p.anchors.glowOuter = {BottomLeft, {-HALF, -HALF}};
    p.anchors.glowInner = {BottomLeft, {-HALF, -HALF}};
    p.itemFoot = [](int) { return EdgeAnchor{Down, 6.f}; }; // feet 6 units below the object position
    p.seed = 42;
    p.channels = Channels(990);
    // 1 second = 311.58 units (normal speed), level time starts at x 0
    p.timeToX = [](double t) { return static_cast<float>(t * 311.58); };
    for (int k = 0; k < 40; ++k) {
        p.beats.push_back(k * 0.5);
        p.downbeat.push_back(k % 4 == 0);
    }
    p.beatInterval = 0.5f;

    // ---- Inferno: everything on ------------------------------------------------
    const Style* inferno = nullptr;
    for (const auto& s : styles) {
        if (s.name == "Inferno") inferno = &s;
    }
    CHECK(inferno);
    auto objs = build(*inferno, inferno->colors, p);
    Counts c = count(objs);
    std::printf("Inferno: fill %zu outline %zu glow %zu details %zu background %zu triggers %zu\n", c.fill, c.outline,
                c.glow, c.details, c.background, c.triggers);
    CHECK(c.fill == solid.size());
    CHECK(c.outline > 0 && c.glow > 0 && c.details > 0 && c.background > 0);
    CHECK(countRole(objs, Role::ColorTrigger) == 10);

    // top decoration stands on top faces: its foot touches the face
    for (const auto& o : objs) {
        if (o.role != Role::Top) continue;
        CHECK(o.rotation == 0);
        const float footY = o.y - 6.f * o.scale;
        const int i = static_cast<int>(std::floor(o.x / CELL));
        const int j = static_cast<int>(std::lround(footY / CELL)) - 1; // the block under the foot
        CHECK(near(std::fmod(footY, CELL), 0.f, 0.02f));
        CHECK(solid.count({i, j}) == 1 && solid.count({i, j + 1}) == 0);
    }
    // hanging decoration: rotated 180, foot at the underside of a ceiling with room below
    size_t hangs = 0;
    for (const auto& o : objs) {
        if (o.role != Role::Hang) continue;
        ++hangs;
        CHECK(o.rotation == 180);
        const float footY = o.y + 6.f * o.scale;
        const int i = static_cast<int>(std::floor(o.x / CELL));
        const int j = static_cast<int>(std::lround(footY / CELL));
        CHECK(solid.count({i, j}) == 1);
        CHECK(j - 3 >= 3); // clearance above the ground row
    }
    // only the floating platform (row 9) can carry hanging decoration
    for (const auto& o : objs) {
        if (o.role == Role::Hang) CHECK(o.y < 270.f && o.y > 240.f);
    }
    // side decoration points away from the wall
    for (const auto& o : objs) {
        if (o.role != Role::Side) continue;
        CHECK(o.rotation == 90 || o.rotation == 270);
    }
    // ground decoration sits on the ground line, never under a block
    for (const auto& o : objs) {
        if (o.role != Role::Ground) continue;
        CHECK(near(o.y - 6.f * o.scale, 90.f, 0.05f));
    }
    // background: behind, never inside a block
    for (const auto& o : objs) {
        if (o.role != Role::Background) continue;
        CHECK(o.zLayer == -3);
        const int ci = static_cast<int>(std::floor(o.x / CELL)), cj = static_cast<int>(std::floor(o.y / CELL));
        CHECK(solid.count({ci, cj}) == 0);
    }

    // pulses: Inferno pulses glow on every beat and BG + back on downbeats
    {
        size_t glowPulses = 0, bgPulses = 0;
        for (const auto& o : objs) {
            if (o.role != Role::PulseTrigger) continue;
            if (o.target == p.channels.glow) ++glowPulses;
            if (o.target == CH_BG) ++bgPulses;
            CHECK(o.x <= bounds(solid, p.origin).maxX + 300.f);
        }
        // beats run to 19.5 s = 6076 units, but only up to maxX + 300 = 600 count
        const size_t expectedBeats = static_cast<size_t>(std::floor(600.0 / 311.58 / 0.5)) + 1;
        CHECK(glowPulses == expectedBeats);
        CHECK(bgPulses == 1); // beats at 0, 0.5, 1.0, 1.5 -> only beat 0 is a downbeat in range
    }

    // ---- deterministic, and the seed changes the details ------------------------------
    {
        auto again = build(*inferno, inferno->colors, p);
        CHECK(toLevelString(again, 9999, 0, true) == toLevelString(objs, 9999, 0, true));
        Params q = p;
        q.seed = 43;
        CHECK(toLevelString(build(*inferno, inferno->colors, q), 9999, 0, true) != toLevelString(objs, 9999, 0, true));
    }

    // ---- toggles ------------------------------------------------------------------------
    {
        Params q = p;
        q.top = q.hang = q.side = q.ground = q.background = q.pulses = q.colorTriggers = false;
        Counts d = count(build(*inferno, inferno->colors, q));
        CHECK(d.details == 0 && d.background == 0 && d.triggers == 0 && d.fill == solid.size());
        q.fill = q.outline = q.glow = false;
        CHECK(build(*inferno, inferno->colors, q).empty());
    }

    // ---- level string -------------------------------------------------------------------
    {
        std::vector<Obj> v;
        Obj a{Role::Top, 126, 45.f, 126.f, 180, 0.8f, 993, 3, 0};
        v.push_back(a);
        Obj t{Role::ColorTrigger, 899, 15.f, 400.f};
        t.rgb = {1, 2, 3};
        t.target = 1000;
        t.opacity = 0.5f;
        v.push_back(t);
        Obj pu{Role::PulseTrigger, 1006, 311.58f, 700.f};
        pu.rgb = {255, 128, 0};
        pu.target = 992;
        pu.fadeOut = 0.275f;
        v.push_back(pu);
        const std::string s = toLevelString(v, 9999, 4, true);
        std::printf("%s\n", s.c_str());
        CHECK(s == "1,126,2,45.00,3,36.00,6,180,21,993,24,3,32,0.80,121,1,20,4,57,9999;"
                   "1,899,2,15.00,3,310.00,7,1,8,2,9,3,10,0,23,1000,35,0.50,20,4,57,9999;"
                   "1,1006,2,311.58,3,610.00,7,255,8,128,9,0,45,0,46,0,47,0.275,48,0,51,992,52,0,20,4,57,9999;");
        CHECK(toLevelString({}, 9999, 0, true).empty());
    }

    // ---- every style builds and only uses sane IDs ----------------------------------------
    for (const auto& s : styles) {
        auto o = build(s, s.colors, p);
        CHECK(!o.empty());
        for (const auto& x : o) CHECK(x.id > 0 && x.id < 5000);
        CHECK(!s.blurb.empty());
    }

    // ---- palette helpers ---------------------------------------------------------------------
    CHECK((mix({0, 0, 0}, {255, 255, 255}, 0.5f) == RGB{128, 128, 128}));
    CHECK((lighten({100, 0, 0}, 1.f) == RGB{255, 255, 255}));
    CHECK((darken({100, 50, 0}, 0.5f) == RGB{50, 25, 0}));

    if (failures == 0) std::printf("all build tests passed\n");
    return failures == 0 ? 0 : 1;
}
