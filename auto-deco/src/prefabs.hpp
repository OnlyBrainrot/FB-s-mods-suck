// Auto Deco - custom details built from basic shapes.
//
// Every detail here is drawn in its own frame: the foot is at (0, 0) on the
// surface it grows from and +y points away from that surface (up for things
// standing on a block, down for things hanging from a ceiling, outwards for
// things on a wall). Sizes are in units, 30 = one block. The builder measures
// every shape in the game, so the sizes come out right whatever the sprites
// look like, and it turns triangles to point the right way on its own.
//
// Tint::Detail means "the colour of the layer it is placed in": the details
// colour on surfaces, the pattern colour inside blocks, the background colour
// for structures. Tint::Accent is the primary colour (lights, tips, gems) and
// pulses with the outline.
#pragma once

#include "deco_build.hpp"

namespace deco::prefab {

namespace shape {
constexpr int SQUARE = 211;      // plain colour square
constexpr int CIRCLE = 3621;
constexpr int RING = 3622;
constexpr int HEART = 3623;
constexpr int DIAMOND = 3624;
constexpr int STAR = 3625;
constexpr int TRIANGLE = 3628;
constexpr int HEXAGON = 3629;
constexpr int BIG_RING = 3636;
constexpr int BIG_CIRCLE = 3637;
constexpr int FRAME = 3639;      // hollow square
} // namespace shape

inline Part bar(float x, float y, float w, float h, Tint t = Tint::Detail, float rot = 0.f, int z = 0) {
    return {shape::SQUARE, x, y, w, h, rot, t, z};
}
inline Part oval(float x, float y, float w, float h, Tint t = Tint::Detail, int z = 0) {
    return {std::max(w, h) > 24.f ? shape::BIG_CIRCLE : shape::CIRCLE, x, y, w, h, 0.f, t, z};
}
inline Part circle(float x, float y, float d, Tint t = Tint::Detail, int z = 0) { return oval(x, y, d, d, t, z); }
inline Part ring(float x, float y, float d, Tint t = Tint::Detail, int z = 0) {
    return {d > 24.f ? shape::BIG_RING : shape::RING, x, y, d, d, 0.f, t, z};
}
inline Part tri(float x, float y, float w, float h, Tint t = Tint::Detail, float rot = 0.f, int z = 0) {
    Part p{shape::TRIANGLE, x, y, w, h, rot, t, z};
    p.point = true;
    return p;
}
inline Part diamond(float x, float y, float w, float h, Tint t = Tint::Detail, float rot = 0.f, int z = 0) {
    return {shape::DIAMOND, x, y, w, h, rot, t, z};
}
inline Part hex(float x, float y, float w, float h, Tint t = Tint::Detail, float rot = 0.f, int z = 0) {
    return {shape::HEXAGON, x, y, w, h, rot, t, z};
}
inline Part star(float x, float y, float d, Tint t = Tint::Detail, float rot = 0.f, int z = 0) {
    return {shape::STAR, x, y, d, d, rot, t, z};
}
inline Part heart(float x, float y, float d, Tint t = Tint::Detail, int z = 0) { return {shape::HEART, x, y, d, d, 0.f, t, z}; }
inline Part frame(float x, float y, float d, Tint t = Tint::Detail, float rot = 0.f, int z = 0) {
    return {shape::FRAME, x, y, d, d, rot, t, z};
}

inline Item make(std::vector<Part> parts, float weight = 1.f, float s0 = 1.f, float s1 = 1.f, Motion motion = Motion::None) {
    Item it;
    it.parts = std::move(parts);
    it.weight = weight;
    it.scaleMin = s0;
    it.scaleMax = s1;
    it.motion = motion;
    return it;
}

// ---- standing on top of blocks and on the ground ---------------------------
inline Item lamp(float w = 1.f) {
    return make({bar(0, 0, 12, 3), bar(0, 3, 3, 38), bar(0, 39, 14, 3), circle(0, 42, 10, Tint::Accent, 1)}, w, 0.9f, 1.1f);
}
inline Item crystals(float w = 1.f) {
    return make({diamond(0, 0, 11, 38), diamond(-9, 0, 8, 24, Tint::Detail, -16), diamond(9, 0, 8, 26, Tint::Detail, 16),
                 diamond(0, 8, 4, 18, Tint::Accent, 0, 1)},
                w, 0.7f, 1.1f);
}
inline Item spikes(float w = 1.f) {
    return make({tri(-8, 0, 7, 16), tri(0, 0, 9, 26), tri(8, 0, 7, 18)}, w, 0.7f, 1.1f);
}
inline Item antenna(float w = 1.f) {
    return make({bar(0, 0, 2, 46), bar(0, 28, 14, 2), bar(0, 36, 9, 2), diamond(0, 44, 7, 7, Tint::Accent, 0, 1)}, w, 0.8f, 1.1f);
}
inline Item fence(float w = 1.f) {
    // 60 units wide: two of them meet exactly when spaced 2 blocks apart
    return make({bar(-28, 0, 4, 22), bar(0, 0, 4, 22), bar(28, 0, 4, 22), bar(0, 15, 60, 3), bar(0, 7, 60, 2)}, w);
}
inline Item flower(float w = 1.f) {
    return make({bar(0, 0, 2, 16), circle(0, 13, 11, Tint::Accent), circle(0, 16, 5, Tint::Detail, 1)}, w, 0.7f, 1.1f);
}
inline Item sprout(float w = 1.f) {
    return make({tri(-5, 0, 4, 12, Tint::Detail, -20), tri(0, 0, 4, 17), tri(5, 0, 4, 12, Tint::Detail, 20)}, w, 0.7f, 1.2f);
}
inline Item tree(float w = 1.f) {
    return make({bar(0, 0, 6, 26, Tint::Fill), circle(0, 20, 36), circle(-14, 16, 22), circle(14, 16, 22)}, w, 0.8f, 1.2f);
}
inline Item mushroom(float w = 1.f) {
    return make({bar(0, 0, 5, 10, Tint::Detail), oval(0, 7, 22, 12, Tint::Accent), circle(-4, 12, 3, Tint::Detail, 1)}, w, 0.7f, 1.1f);
}
inline Item torch(float w = 1.f) {
    return make({bar(0, 0, 4, 18), bar(0, 16, 8, 3), tri(0, 19, 9, 14, Tint::Accent)}, w, 0.9f, 1.1f);
}
inline Item post(float w = 1.f) {
    return make({bar(0, 0, 16, 4), bar(0, 4, 10, 24), bar(0, 28, 16, 4), circle(0, 32, 6, Tint::Accent)}, w, 0.9f, 1.1f);
}
inline Item obelisk(float w = 1.f) {
    return make({bar(0, 0, 16, 4), bar(0, 4, 10, 30), tri(0, 34, 10, 10), diamond(0, 16, 4, 8, Tint::Accent, 0, 1)}, w, 0.9f, 1.2f);
}
inline Item beacon(float w = 1.f) {
    return make({hex(0, 0, 16, 8), ring(0, 6, 16, Tint::Accent), circle(0, 10, 8, Tint::Accent, 1)}, w);
}
inline Item pipe(float w = 1.f) {
    return make({bar(0, 0, 54, 8, Tint::Fill), bar(-24, 0, 5, 11), bar(24, 0, 5, 11), bar(0, 3, 54, 2, Tint::Accent, 0, 1)}, w);
}
inline Item rocks(float w = 1.f) {
    return make({hex(-7, 0, 15, 10), hex(7, 0, 11, 7), hex(1, 0, 6, 4, Tint::Accent, 0, 1)}, w, 0.8f, 1.2f);
}
inline Item candles(float w = 1.f) {
    return make({bar(-7, 0, 4, 10), bar(0, 0, 4, 16), bar(7, 0, 4, 8), tri(-7, 10, 4, 6, Tint::Accent), tri(0, 16, 4, 7, Tint::Accent),
                 tri(7, 8, 4, 6, Tint::Accent)},
                w);
}
inline Item heartPlant(float w = 1.f) { return make({bar(0, 0, 2, 16), heart(0, 13, 13, Tint::Accent)}, w, 0.8f, 1.1f); }
inline Item starPost(float w = 1.f) { return make({bar(0, 0, 2, 30), star(0, 26, 14, Tint::Accent)}, w, 0.8f, 1.1f); }
inline Item tombstone(float w = 1.f) {
    return make({bar(0, 0, 16, 18), circle(0, 10, 16), bar(0, 12, 2, 8, Tint::Accent, 0, 1), bar(0, 16, 7, 2, Tint::Accent, 0, 1)}, w,
                0.8f, 1.1f);
}
inline Item cactus(float w = 1.f) {
    return make({bar(0, 0, 7, 30), bar(-7, 10, 7, 3), bar(-10, 10, 3, 10), bar(7, 14, 7, 3), bar(10, 14, 3, 8)}, w, 0.8f, 1.1f);
}

// ---- hanging from ceilings (+y is down) --------------------------------------
inline Item hangLamp(float w = 1.f) {
    return make({bar(0, 0, 2, 26), tri(0, 24, 16, 9, Tint::Detail, 180.f), circle(0, 31, 8, Tint::Accent, 1)}, w, 0.9f, 1.2f);
}
inline Item banner(float w = 1.f) {
    return make({bar(0, 0, 22, 3), bar(0, 3, 16, 24), bar(0, 9, 16, 3, Tint::Accent, 0, 1), tri(0, 27, 16, 8)}, w, 0.9f, 1.2f);
}
inline Item stalactites(float w = 1.f) {
    return make({tri(0, 0, 9, 28), tri(-9, 0, 6, 16), tri(9, 0, 6, 18)}, w, 0.7f, 1.2f);
}
inline Item chainGem(float w = 1.f) {
    return make({ring(0, 0, 7), ring(0, 6, 7), ring(0, 12, 7), ring(0, 18, 7), diamond(0, 24, 10, 14, Tint::Accent)}, w, 0.9f, 1.2f);
}
inline Item vines(float w = 1.f) {
    return make({bar(0, 0, 2, 32), circle(-3, 8, 6), circle(3, 17, 5), circle(-2, 26, 6), circle(0, 30, 4, Tint::Accent, 1)}, w, 0.8f, 1.3f);
}
inline Item hangCrystal(float w = 1.f) {
    return make({bar(0, 0, 2, 10), diamond(0, 9, 10, 22, Tint::Accent), diamond(0, 13, 4, 10, Tint::Detail, 0, 1)}, w, 0.8f, 1.2f);
}

// ---- on walls (+y points out of the wall) --------------------------------------
inline Item bracketLamp(float w = 1.f) { return make({bar(0, 0, 3, 14), bar(0, 10, 8, 2), circle(0, 12, 8, Tint::Accent, 1)}, w); }
inline Item wallSpikes(float w = 1.f) { return make({tri(-7, 0, 7, 11), tri(7, 0, 7, 11), tri(0, 0, 6, 8)}, w, 0.8f, 1.1f); }
inline Item vent(float w = 1.f) {
    return make({bar(0, 0, 18, 4), bar(-5, 4, 2, 3, Tint::Accent), bar(0, 4, 2, 3, Tint::Accent), bar(5, 4, 2, 3, Tint::Accent)}, w);
}
inline Item wallMoss(float w = 1.f) { return make({circle(-4, 0, 8), circle(5, 0, 7), circle(0, 3, 5, Tint::Accent, 1)}, w, 0.8f, 1.2f); }

// ---- patterns inside blocks (centred on a cell) ---------------------------------
inline Item innerFrame(float w = 1.f) { return make({frame(0, 0, 20)}, w); }
inline Item innerFrameDot(float w = 1.f) { return make({frame(0, 0, 20), circle(0, 7, 6, Tint::Detail, 1)}, w); }
inline Item innerDot(float w = 1.f) { return make({circle(0, 0, 6)}, w); }
inline Item innerRing(float w = 1.f) { return make({ring(0, 0, 16)}, w); }
inline Item innerCross(float w = 1.f) { return make({bar(0, 0, 22, 3, Tint::Detail, 45.f), bar(0, 0, 22, 3, Tint::Detail, -45.f)}, w); }
inline Item innerPlus(float w = 1.f) { return make({bar(0, 0, 3, 16), bar(0, 6.5f, 16, 3)}, w); }
inline Item innerDiamond(float w = 1.f) { return make({diamond(0, 0, 12, 12)}, w); }
inline Item innerHex(float w = 1.f) { return make({hex(0, 0, 15, 15)}, w); }
inline Item innerBolts(float w = 1.f) {
    return make({circle(-9, 0, 4), circle(9, 0, 4), circle(-9, 18, 4), circle(9, 18, 4)}, w);
}
inline Item innerGrill(float w = 1.f) { return make({bar(0, 0, 20, 2), bar(0, 6, 20, 2), bar(0, 12, 20, 2)}, w); }
inline Item innerStar(float w = 1.f) { return make({star(0, 0, 14)}, w); }
inline Item innerHeart(float w = 1.f) { return make({heart(0, 0, 12)}, w); }
inline Item innerBrick(float w = 1.f) { return make({bar(0, 0, 26, 2), bar(-6, 2, 2, 10), bar(0, 12, 26, 2), bar(6, 14, 2, 10)}, w); }
inline Item innerLeaf(float w = 1.f) { return make({diamond(-4, 0, 7, 14, Tint::Detail, -30.f), diamond(4, 0, 7, 14, Tint::Detail, 30.f)}, w); }

// ---- structures standing on the ground behind the level -------------------------
inline Item tower(float w = 1.f) {
    return make({bar(0, 0, 40, 170), bar(0, 170, 48, 8), bar(0, 178, 3, 40), circle(0, 214, 8, Tint::Back2), bar(-10, 20, 5, 130, Tint::Back2, 0, 1),
                 bar(10, 20, 5, 130, Tint::Back2, 0, 1)},
                w, 0.7f, 1.4f);
}
inline Item buildings(float w = 1.f) {
    return make({bar(-30, 0, 50, 110), bar(25, 0, 40, 160), bar(25, 160, 24, 12), bar(-30, 110, 30, 10), bar(-30, 30, 30, 4, Tint::Back2, 0, 1),
                 bar(-30, 60, 30, 4, Tint::Back2, 0, 1), bar(25, 50, 24, 4, Tint::Back2, 0, 1), bar(25, 100, 24, 4, Tint::Back2, 0, 1)},
                w, 0.7f, 1.5f);
}
inline Item pillar(float w = 1.f) {
    return make({bar(0, 0, 44, 12), bar(0, 12, 28, 190), bar(0, 202, 44, 12), bar(0, 20, 4, 174, Tint::Back2, 0, 1)}, w, 0.6f, 1.3f);
}
inline Item arch(float w = 1.f) {
    return make({bar(-48, 0, 20, 130), bar(48, 0, 20, 130), bar(0, 130, 116, 20), tri(0, 150, 60, 30)}, w, 0.7f, 1.3f);
}
inline Item mountains(float w = 1.f) {
    return make({tri(0, 0, 240, 150), tri(110, 0, 150, 100, Tint::Back2, 0, -1), tri(-115, 0, 130, 80, Tint::Back2, 0, -1)}, w, 0.6f, 1.3f);
}
inline Item pine(float w = 1.f) {
    return make({bar(0, 0, 10, 24), tri(0, 16, 70, 60), tri(0, 50, 54, 48), tri(0, 82, 36, 36)}, w, 0.7f, 1.6f);
}
inline Item bigTree(float w = 1.f) {
    return make({bar(0, 0, 16, 90, Tint::Back2), circle(0, 70, 120), circle(-50, 60, 80), circle(50, 60, 80)}, w, 0.6f, 1.3f);
}
inline Item spire(float w = 1.f) {
    return make({diamond(0, 0, 36, 200), diamond(-30, 0, 22, 110, Tint::Detail, -8.f), diamond(30, 0, 22, 120, Tint::Detail, 8.f),
                 diamond(0, 40, 10, 90, Tint::Back2, 0, 1)},
                w, 0.6f, 1.3f);
}
inline Item ringTower(float w = 1.f) { return make({bar(0, 0, 10, 120), ring(0, 110, 110), circle(0, 150, 30, Tint::Back2)}, w, 0.6f, 1.3f); }
inline Item bigObelisk(float w = 1.f) {
    return make({bar(0, 0, 44, 12), bar(0, 12, 28, 170), tri(0, 182, 28, 30), diamond(0, 60, 10, 40, Tint::Back2, 0, 1)}, w, 0.6f, 1.4f);
}
inline Item pagoda(float w = 1.f) {
    return make({bar(0, 0, 40, 40), bar(0, 40, 76, 6), bar(0, 46, 32, 32), bar(0, 78, 64, 6), bar(0, 84, 24, 26), tri(0, 110, 44, 22)}, w,
                0.7f, 1.4f);
}
inline Item factory(float w = 1.f) {
    return make({bar(0, 0, 120, 70), bar(-40, 70, 16, 70), bar(-12, 70, 14, 50), tri(30, 70, 40, 20), tri(58, 70, 40, 20),
                 bar(-40, 140, 22, 6, Tint::Back2), bar(0, 20, 100, 6, Tint::Back2, 0, 1)},
                w, 0.7f, 1.3f);
}
inline Item ruins(float w = 1.f) {
    return make({bar(-40, 0, 18, 90), bar(40, 0, 18, 60), bar(-20, 90, 58, 14), bar(0, 0, 100, 10), hex(15, 10, 20, 14, Tint::Back2)}, w,
                0.7f, 1.3f);
}

// ---- floating in the background (centred on a point) ------------------------------
inline Item floatDiamond(float w = 1.f) {
    return make({diamond(0, 0, 16, 26), diamond(0, 8, 6, 10, Tint::Back2, 0, 1)}, w, 0.6f, 1.4f, Motion::Float);
}
inline Item floatHex(float w = 1.f) { return make({hex(0, 0, 22, 22)}, w, 0.6f, 1.4f); }
inline Item floatRing(float w = 1.f) { return make({ring(0, 0, 26)}, w, 0.6f, 1.6f); }
inline Item floatSquare(float w = 1.f) {
    Item it = make({frame(0, 0, 22)}, w, 0.6f, 1.5f, Motion::Spin);
    it.spin = true;
    return it;
}
inline Item floatStar(float w = 1.f) {
    Item it = make({star(0, 0, 16)}, w, 0.5f, 1.2f, Motion::Spin);
    it.spin = true;
    return it;
}
inline Item floatCross(float w = 1.f) { return make({bar(0, 0, 4, 22), bar(0, 9, 22, 4)}, w, 0.6f, 1.3f, Motion::Float); }
inline Item floatOrb(float w = 1.f) { return make({circle(0, 0, 16), ring(0, -5, 26, Tint::Back2)}, w, 0.6f, 1.4f, Motion::Float); }
inline Item cloud(float w = 1.f) {
    return make({circle(-16, 0, 24), circle(0, 0, 34), circle(17, 0, 22), bar(0, 0, 50, 10)}, w, 0.8f, 1.8f, Motion::Float);
}
inline Item floatIsland(float w = 1.f) {
    return make({bar(0, 20, 60, 10), tri(0, 0, 50, 20, Tint::Detail, 180.f), circle(-14, 30, 10, Tint::Back2), circle(10, 30, 14, Tint::Back2)}, w,
                0.7f, 1.3f, Motion::Float);
}
inline Item floatHeart(float w = 1.f) { return make({heart(0, 0, 18)}, w, 0.6f, 1.4f, Motion::Float); }
inline Item lantern(float w = 1.f) {
    return make({bar(0, 0, 14, 18), bar(0, 18, 18, 3, Tint::Back2), bar(0, -3, 18, 3, Tint::Back2), circle(0, 5, 6, Tint::Back2, 1)}, w,
                0.7f, 1.3f, Motion::Float);
}

} // namespace deco::prefab
