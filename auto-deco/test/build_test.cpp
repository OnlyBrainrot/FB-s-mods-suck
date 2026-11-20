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

    // the test level (node coordinates, ground top at y = 90, row 3 is the first row above it):
    //   a floor along the ground, i 0..19
    //   a corridor ceiling over i 4..11 at row 6 (two open rows under it)
    //   a 2-high pillar at i 16
    //   a single floating block at (14, 10)
    //   a 7x7 mass at i 30..36, rows 3..9
    CellSet solid;
    for (int i = 0; i < 20; ++i) solid.insert({i, 3});
    for (int i = 4; i < 12; ++i) solid.insert({i, 6});
    solid.insert({16, 4});
    solid.insert({16, 5});
    solid.insert({14, 10});
    for (int i = 30; i < 37; ++i)
        for (int j = 3; j < 10; ++j) solid.insert({i, j});

    Params p;
    p.solid = solid;
    p.origin = {0, 0};
    p.anchors.outlineEdge = {Up, HALF};
    p.anchors.outlineInner = {TopLeft, {-HALF, HALF}};
    p.anchors.glowEdge = {Down, HALF};
    p.anchors.glowOuter = {BottomLeft, {-HALF, -HALF}};
    p.anchors.glowInner = {BottomLeft, {-HALF, -HALF}};
    // every object measures 20 x 20 around its position, except a triangle
    // that points right (its ink is heavy on the left)
    p.shapeOf = [](int id) {
        if (id == prefab::shape::TRIANGLE) return Shape{true, -10, 10, -10, 10, -3, 0};
        return Shape{true, -10, 10, -10, 10, 0, 0};
    };
    p.seed = 42;
    p.channels = Channels(990);
    p.timeToX = [](double t) { return static_cast<float>(t * 311.58); };
    for (int k = 0; k < 40; ++k) {
        p.beats.push_back(k * 0.5);
        p.downbeat.push_back(k % 4 == 0);
    }
    p.beatInterval = 0.5f;
    for (int g = 0; g < ANIM_GROUPS; ++g) p.animGroups.push_back(100 + g);
    auto inSolid = [&](float x, float y) {
        return solid.count({static_cast<int>(std::floor(x / CELL)), static_cast<int>(std::floor(y / CELL))}) != 0;
    };

    // ---- placement rules, with single objects so positions are exact ----------------
    {
        Style s;
        s.name = "test";
        s.top = items::layer({{126}}, 1.f, 3);
        s.hang = items::layer({{127}}, 1.f, 3, 3, 3);
        s.side = items::layer({{128}}, 1.f, 3, 2, 2);
        Params q = p;
        q.ground = q.background = q.structures = q.inner = q.pulses = q.colorTriggers = false;
        q.fill = q.outline = q.glow = false;
        auto objs = build(s, s.colors, q);
        std::vector<Obj> tops, hangs, sides;
        for (const auto& o : objs) {
            if (o.role == Role::Top) tops.push_back(o);
            if (o.role == Role::Hang) hangs.push_back(o);
            if (o.role == Role::Side) sides.push_back(o);
        }
        // tops: the corridor ceiling (8 long: 2 pieces, evenly spaced and centred),
        // the floor outside the corridor (0..3 = 4 long: 1 piece; 12..15 and 17..19 are 4 and 3),
        // the mass top (7 long: 2). Nothing on the corridor floor, the single block or the pillar.
        std::set<int> topX;
        for (const auto& o : tops) {
            CHECK(near(o.rotation, 0.f));
            topX.insert(static_cast<int>(std::lround(o.x)));
            CHECK(!inSolid(o.x, o.y));
            if (near(o.y, 220.f)) CHECK(o.x > 120.f && o.x < 360.f); // on the ceiling
        }
        CHECK(topX.count(195) && topX.count(285)); // ceiling: cells 6 and 9
        for (const auto& o : tops) {
            CHECK(!(o.y < 180.f && o.x > 120.f && o.x < 360.f)); // corridor floor stays clear
            CHECK(!(o.x > 420.f && o.x < 450.f));                  // single block
        }
        CHECK(!tops.empty());
        // hangs: only under ceilings with 3 open cells: the single block is 1 long, the
        // corridor ceiling has 2 rows: nothing hangs
        CHECK(hangs.empty());
        // walls: the pillar (2 high, 2 open cells beside it) and the mass sides
        for (const auto& o : sides) {
            CHECK(near(o.rotation, 90.f) || near(o.rotation, 270.f));
            CHECK(!inSolid(o.x, o.y));
        }
        CHECK(!sides.empty());
    }

    // ---- a custom detail: parts keep their layout, triangles turn to point up -------------
    {
        std::vector<Obj> out;
        Params q = p;
        Placer pl(q, out);
        Item it = prefab::make({prefab::bar(0, 0, 4, 30), prefab::tri(0, 30, 10, 20, Tint::Accent)});
        Placer::Where at;
        at.foot = {100, 200};
        pl.place(it, at, {Role::Top, 3, 0, Tint::Detail});
        CHECK(out.size() == 2);
        // the bar: 4 x 30 standing on the foot
        CHECK(near(out[0].x, 100.f) && near(out[0].y, 215.f));
        CHECK(near(out[0].scaleX, 0.2f) && near(out[0].scaleY, 1.5f));
        CHECK(out[0].color == q.channels.details);
        // the triangle points right, so it is turned 270 and its axes swap
        CHECK(near(out[1].rotation, 270.f));
        CHECK(near(out[1].scaleX, 1.f) && near(out[1].scaleY, 0.5f));
        CHECK(near(out[1].y, 240.f) && near(out[1].x, 100.f));
        CHECK(out[1].color == q.channels.outline);
        // hanging: the same detail turned upside down under a ceiling
        out.clear();
        at.facing = Down;
        pl.place(it, at, {Role::Hang, 3, 0, Tint::Detail});
        CHECK(near(out[0].y, 185.f) && near(out[0].rotation, 180.f));
        CHECK(near(out[1].y, 160.f) && near(out[1].rotation, 90.f));
        CHECK(near(pl.halfWidth(it, 1.f), 5.f));
    }

    // ---- every style, everything on ----------------------------------------------------
    const Style* inferno = nullptr;
    for (const auto& s : styles) {
        if (s.name == "Inferno") inferno = &s;
    }
    CHECK(inferno);
    auto objs = build(*inferno, inferno->colors, p);
    Counts c = count(objs);
    std::printf("Inferno: fill %zu outline %zu glow %zu inner %zu details %zu structures %zu background %zu animated %zu triggers %zu\n",
                c.fill, c.outline, c.glow, c.inner, c.details, c.structures, c.background, c.animated, c.triggers);
    CHECK(c.fill == solid.size());
    CHECK(c.outline > 0 && c.glow > 0 && c.details > 0 && c.background > 0 && c.inner > 0 && c.structures > 0);
    CHECK(countRole(objs, Role::ColorTrigger) == 11);

    // patterns only inside the 7x7 mass, clear of its edge
    for (const auto& o : objs) {
        if (o.role != Role::Inner) continue;
        CHECK(o.zLayer == 5);
        CHECK(o.x > 31 * CELL - 1 && o.x < 36 * CELL + 1 && o.y > 4 * CELL - 1 && o.y < 9 * CELL + 1);
    }
    // structures stand behind everything
    for (const auto& o : objs) {
        if (o.role == Role::Structure) CHECK(o.zLayer == -3 && o.y > 90.f - 1.f);
    }
    // background: behind, and animated objects come with their loops
    {
        std::set<int> used;
        for (const auto& o : objs) {
            if (o.role == Role::Background) {
                CHECK(o.zLayer == -3);
                if (o.group) used.insert(o.group);
            }
        }
        CHECK(!used.empty());
        size_t moves = 0, rotates = 0, starters = 0;
        for (const auto& o : objs) {
            if (o.role == Role::MoveTrigger) {
                ++moves;
                CHECK(o.spawnTriggered && used.count(o.target));
                CHECK(o.moveY != 0 && o.duration > 0.f);
            }
            if (o.role == Role::RotateTrigger) {
                ++rotates;
                CHECK(used.count(o.target));
            }
            if (o.role == Role::SpawnTrigger && !o.spawnTriggered) ++starters;
            if (o.role == Role::SpawnTrigger && o.spawnTriggered) CHECK(o.delay > 0.f);
        }
        CHECK(moves % 2 == 0 && moves > 0);
        CHECK(starters == moves / 2 + rotates);
        // without free groups nothing moves
        Params q = p;
        q.animGroups.clear();
        auto still = build(*inferno, inferno->colors, q);
        for (const auto& o : still) CHECK(o.group == 0 && o.role != Role::MoveTrigger && o.role != Role::SpawnTrigger);
    }

    // pulses: Inferno pulses glow on every beat and BG + back on downbeats
    {
        size_t glowPulses = 0, bgPulses = 0;
        const float endX = bounds(solid, p.origin).maxX + 300.f;
        for (const auto& o : objs) {
            if (o.role != Role::PulseTrigger) continue;
            if (o.target == p.channels.glow) ++glowPulses;
            if (o.target == CH_BG) ++bgPulses;
            CHECK(o.x <= endX);
        }
        size_t expected = 0, expectedDown = 0;
        for (size_t k = 0; k < p.beats.size(); ++k) {
            if (p.beats[k] * 311.58 <= endX) {
                ++expected;
                if (p.downbeat[k]) ++expectedDown;
            }
        }
        CHECK(glowPulses == expected);
        CHECK(bgPulses == expectedDown);
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
        q.top = q.hang = q.side = q.ground = q.background = q.structures = q.inner = q.pulses = q.colorTriggers = false;
        Counts d = count(build(*inferno, inferno->colors, q));
        CHECK(d.details == 0 && d.background == 0 && d.structures == 0 && d.inner == 0 && d.triggers == 0 && d.fill == solid.size());
        q.fill = q.outline = q.glow = false;
        CHECK(build(*inferno, inferno->colors, q).empty());
    }

    // ---- level string -------------------------------------------------------------------
    {
        std::vector<Obj> v;
        Obj a{Role::Top, 126, 45.f, 126.f};
        a.rotation = 180;
        a.scaleX = a.scaleY = 0.8f;
        a.color = 993;
        a.zLayer = 3;
        v.push_back(a);
        Obj w{Role::Background, 211, 10.f, 100.f};
        w.scaleX = 0.2f;
        w.scaleY = 1.5f;
        w.rotation = 22.5f;
        w.color = 994;
        w.zLayer = -3;
        w.zOrder = -5;
        w.group = 101;
        v.push_back(w);
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
        Obj mv{Role::MoveTrigger, 901, 75.f, 400.f};
        mv.moveY = -10;
        mv.duration = 1.2f;
        mv.easing = 1;
        mv.target = 100;
        mv.group = 103;
        mv.spawnTriggered = true;
        v.push_back(mv);
        Obj rt{Role::RotateTrigger, 1346, 75.f, 430.f};
        rt.degrees = -360;
        rt.duration = 13.f;
        rt.target = 110;
        rt.group = 112;
        rt.spawnTriggered = true;
        v.push_back(rt);
        Obj sp{Role::SpawnTrigger, 1268, 45.f, 400.f};
        sp.target = 103;
        v.push_back(sp);
        const std::string s = toLevelString(v, 9999, 4, true);
        std::printf("%s\n", s.c_str());
        CHECK(s == "1,126,2,45.00,3,36.00,6,180,21,993,22,993,24,3,32,0.800,121,1,20,4,57,9999;"
                   "1,211,2,10.00,3,10.00,6,22.50,21,994,22,994,24,-3,25,-5,128,0.200,129,1.500,121,1,20,4,57,9999.101;"
                   "1,899,2,15.00,3,310.00,7,1,8,2,9,3,10,0,23,1000,35,0.50,20,4,57,9999;"
                   "1,1006,2,311.58,3,610.00,7,255,8,128,9,0,45,0,46,0,47,0.275,48,0,51,992,52,0,20,4,57,9999;"
                   "1,901,2,75.00,3,310.00,10,1.20,28,0,29,-10,30,1,85,2.00,51,100,62,1,87,1,20,4,57,9999.103;"
                   "1,1346,2,75.00,3,340.00,10,13.00,30,0,51,110,68,-360,62,1,87,1,20,4,57,9999.112;"
                   "1,1268,2,45.00,3,310.00,51,103,20,4,57,9999;");
        CHECK(toLevelString({}, 9999, 0, true).empty());
    }

    // ---- every style builds and only uses sane IDs ----------------------------------------
    for (const auto& s : styles) {
        auto o = build(s, s.colors, p);
        CHECK(!o.empty());
        Counts k = count(o);
        std::printf("  %-13s %5zu objects: inner %4zu details %4zu structures %4zu background %4zu animated %4zu\n", s.name.c_str(), o.size(),
                    k.inner, k.details, k.structures, k.background, k.animated);
        for (const auto& x : o) {
            CHECK(x.id > 0 && x.id < 5000);
            CHECK(std::isfinite(x.x) && std::isfinite(x.y) && std::isfinite(x.scaleX) && std::isfinite(x.scaleY));
            CHECK(x.scaleX > 0.f && x.scaleY > 0.f && x.scaleX < 40.f && x.scaleY < 40.f);
        }
        CHECK(!s.blurb.empty());
    }

    // ---- palette helpers ---------------------------------------------------------------------
    CHECK((mix({0, 0, 0}, {255, 255, 255}, 0.5f) == RGB{128, 128, 128}));
    CHECK((lighten({100, 0, 0}, 1.f) == RGB{255, 255, 255}));
    CHECK((darken({100, 50, 0}, 0.5f) == RGB{50, 25, 0}));

    if (failures == 0) std::printf("all build tests passed\n");
    return failures == 0 ? 0 : 1;
}
