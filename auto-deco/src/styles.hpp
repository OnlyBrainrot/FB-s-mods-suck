// Auto Deco - the built-in styles.
// Object IDs: decoration only (thorns 123-128 and spikes 18-21 are the
// hitbox-free decorative ones). The mod also refuses, at runtime, any ID the
// game reports as a hazard or solid.
#pragma once

#include "prefabs.hpp"

#include <vector>

namespace deco {

namespace items {
inline std::vector<Item> thorns(float s0 = 0.6f, float s1 = 1.f) {
    return {{126, 3, s0, s1}, {125, 2, s0, s1}, {127, 3, s0, s1}, {124, 1, s0, s1}};
}
inline std::vector<Item> smallThorns() { return {{126, 2, 0.5f, 0.8f}, {127, 3, 0.5f, 0.8f}}; }
inline std::vector<Item> decoSpikes(float s0 = 0.6f, float s1 = 1.f) {
    return {{20, 3, s0, s1}, {21, 3, s0, s1}, {19, 1, s0, s1}};
}
inline std::vector<Item> grass() {
    return {{406, 2}, {407, 2}, {408, 2}, {414, 2}, {906, 1}, {940, 1}, {941, 1}, {942, 1}};
}
inline std::vector<Item> bushes() { return {{908, 1, 0.8f, 1.f}, {909, 2, 0.8f, 1.f}, {910, 2, 0.8f, 1.f}}; }
inline std::vector<Item> chains() { return {{41, 1, 0.8f, 1.f}, {110, 2, 0.8f, 1.f}, {107, 1, 0.8f, 1.f}}; }
inline std::vector<Item> rods() { return {{15, 1}, {16, 2}, {17, 2}}; }
inline std::vector<Item> join(std::vector<Item> a, const std::vector<Item>& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}
// decoration along a surface: pieces `spacing` blocks apart, on `chance` of
// the surfaces at least `minRun` blocks long with `clearance` open blocks
inline SurfaceLayer layer(std::vector<Item> items, float chance, int spacing = 3, int minRun = 3, int clearance = 3) {
    SurfaceLayer l;
    l.items = std::move(items);
    l.chance = chance;
    l.spacing = spacing;
    l.minRun = minRun;
    l.clearance = clearance;
    return l;
}
} // namespace items

inline std::vector<Style> builtinStyles() {
    using namespace items;
    using namespace prefab;
    std::vector<Style> v;

    {
        Style s;
        s.name = "Neon Night";
        s.blurb = "Black blocks, neon edges, drifting particles";
        s.colors = {{10, 8, 30}, {0, 0, 0}, {0, 230, 255}, {180, 60, 255}};
        s.outline = OutlineKind::Neon;
        s.glow = GlowKind::Medium;
        s.background = {{1527, 3, 0.8f, 1.2f, true}, {1528, 3, 0.8f, 1.2f, true}, {51, 1, 0.6f, 1.f, false, true}, {50, 1, 0.5f, 0.8f, false, true}};
        s.backSpacing = 110;
        s.backChance = 0.45f;
        s.downbeatBack = true;
        s.top = layer({antenna(2), lamp(1), beacon(1)}, 0.8f, 4);
        s.hang = layer({hangLamp(2), hangCrystal(1)}, 0.6f, 5, 3, 4);
        s.side = layer({bracketLamp()}, 0.35f, 4, 3, 3);
        s.inner = {innerFrame(2), innerFrameDot(1)};
        s.pulseInner = true;
        s.structures = {tower(2), buildings(2), ringTower(1)};
        s.structSpacing = 230;
        s.background = join(s.background, {floatSquare(2), floatRing(1)});
        v.push_back(s);
    }
    {
        Style s;
        s.name = "Inferno";
        s.blurb = "Thorns, chains and fire";
        s.colors = {{40, 4, 0}, {20, 0, 0}, {255, 90, 0}, {200, 20, 10}};
        s.outline = OutlineKind::Thick;
        s.glow = GlowKind::Large;
        s.background = {{3826, 3, 0.8f, 1.6f}, {1596, 1, 0.8f, 1.2f, false, true}};
        s.backSpacing = 130;
        s.backChance = 0.35f;
        s.pulseOutline = false;
        s.downbeatBG = true;
        s.top = layer(join(thorns(0.6f, 1.1f), {spikes(3), torch(2)}), 0.85f, 3);
        s.hang = layer(join(chains(), {stalactites(3)}), 0.6f, 4, 3, 4);
        s.side = layer({wallSpikes()}, 0.3f, 3, 3, 3);
        s.ground = layer(join(thorns(0.7f, 1.2f), {torch(1)}), 0.8f, 3);
        s.inner = {innerCross(1), innerDiamond(1)};
        s.structures = {mountains(2), spire(1), ruins(1)};
        s.structSpacing = 260;
        v.push_back(s);
    }
    {
        Style s;
        s.name = "Frostbite";
        s.blurb = "Icicles under every ledge, falling snow";
        s.colors = {{6, 24, 48}, {10, 30, 60}, {170, 230, 255}, {230, 250, 255}};
        s.outline = OutlineKind::Classic;
        s.glow = GlowKind::Small;
        s.background = {{3814, 3, 0.6f, 1.2f, true}, {222, 1, 0.8f, 1.2f, false, true}, {223, 1, 0.8f, 1.2f, false, true}, {224, 1, 0.8f, 1.2f, false, true}};
        s.backSpacing = 120;
        s.backChance = 0.4f;
        s.pulseGlow = false;
        s.hang = layer(join(decoSpikes(0.6f, 1.1f), {stalactites(3), hangCrystal(2)}), 0.85f, 2, 3, 3);
        s.top = layer({crystals(2), {21, 1, 0.5f, 0.8f}}, 0.5f, 5);
        s.ground = layer({crystals(1), rocks(1)}, 0.4f, 6);
        s.inner = {innerStar(1), innerPlus(2), innerHex(1)};
        s.structures = {pine(3), mountains(2)};
        s.structSpacing = 200;
        s.background = join(s.background, {floatHex(2), floatStar(1)});
        v.push_back(s);
    }
    {
        Style s;
        s.name = "Overgrowth";
        s.blurb = "Grass, bushes and flowers on every surface";
        s.colors = {{30, 70, 50}, {14, 32, 18}, {120, 220, 90}, {170, 240, 120}};
        s.outline = OutlineKind::Classic;
        s.glow = GlowKind::Small;
        s.background = {{222, 1, 1.f, 1.6f}, {223, 1, 1.f, 1.6f}, {224, 1, 1.f, 1.6f}};
        s.backSpacing = 160;
        s.backChance = 0.35f;
        s.backOpacity = 0.4f;
        s.pulseOutline = false;
        s.top = layer(join(grass(), {flower(3), sprout(3), mushroom(1), tree(1)}), 0.95f, 2, 3, 2);
        s.ground = layer(join(join(grass(), bushes()), {flower(2), tree(2), rocks(1)}), 0.9f, 2);
        s.hang = layer({vines(3)}, 0.6f, 3, 3, 3);
        s.side = layer({wallMoss()}, 0.4f, 3, 3, 2);
        s.inner = {innerLeaf(2), innerDot(1)};
        s.innerSpacing = 4;
        s.structures = {bigTree(3), pine(2), mountains(1)};
        s.structSpacing = 210;
        s.background = join(s.background, {cloud(2), floatIsland(1)});
        v.push_back(s);
    }
    {
        Style s;
        s.name = "Factory";
        s.blurb = "Heavy outlines, chains, turning gears";
        s.colors = {{28, 30, 34}, {50, 52, 58}, {255, 180, 40}, {120, 125, 135}};
        s.outline = OutlineKind::Thick;
        s.glow = GlowKind::Small;
        s.background = {{85, 1, 0.8f, 1.2f, true}, {86, 2, 0.8f, 1.2f, true}, {87, 2, 0.8f, 1.2f, true}, {97, 2, 0.8f, 1.2f, true}};
        s.backSpacing = 120;
        s.backChance = 0.5f;
        s.pulseGlow = false;
        s.top = layer(join(rods(), {pipe(3), antenna(2), fence(2), post(2)}), 0.8f, 3);
        s.hang = layer(join(chains(), {hangLamp(2)}), 0.6f, 4, 3, 4);
        s.side = layer({vent(), bracketLamp()}, 0.4f, 3, 3, 3);
        s.ground = layer({fence(2), post(1)}, 0.5f, 2);
        s.inner = {innerBolts(2), innerGrill(1)};
        s.structures = {factory(3), tower(1), buildings(1)};
        s.structSpacing = 240;
        s.background = join(s.background, {floatCross(1)});
        v.push_back(s);
    }
    {
        Style s;
        s.name = "Void";
        s.blurb = "No edges, just deep glow and pulsing rings";
        s.colors = {{4, 0, 10}, {0, 0, 0}, {150, 60, 255}, {90, 30, 200}};
        s.outline = OutlineKind::None;
        s.glow = GlowKind::Large;
        s.background = {{236, 1, 0.6f, 1.f}, {497, 1, 0.6f, 1.f}, {50, 1, 0.5f, 0.9f}, {1019, 1, 0.8f, 1.2f, true, true}, {1020, 1, 0.8f, 1.2f, true, true}};
        s.backSpacing = 140;
        s.backChance = 0.4f;
        s.downbeatBG = true;
        s.hang = layer({hangCrystal()}, 0.4f, 6, 3, 4);
        s.top = layer({beacon(), obelisk()}, 0.35f, 6);
        s.inner = {innerRing(2), innerDot(1)};
        s.innerMix = 0.3f;
        s.pulseInner = true;
        s.structures = {ringTower(2), spire(2)};
        s.structSpacing = 300;
        s.background = join(s.background, {floatRing(2), floatOrb(2)});
        v.push_back(s);
    }
    {
        Style s;
        s.name = "Synthwave";
        s.blurb = "Hot pink edges, orange sun rings, stars";
        s.colors = {{30, 6, 48}, {14, 0, 26}, {255, 60, 200}, {255, 160, 40}};
        s.outline = OutlineKind::Neon;
        s.glow = GlowKind::Medium;
        s.background = {{497, 1, 0.8f, 1.4f}, {3966, 3, 0.4f, 0.8f, true, true}, {3967, 2, 0.4f, 0.8f, true, true}};
        s.backSpacing = 120;
        s.backChance = 0.4f;
        s.top = layer({post(2), antenna(1), lamp(1)}, 0.6f, 5);
        s.hang = layer({hangLamp()}, 0.4f, 6, 3, 4);
        s.inner = {innerFrame(1), innerGrill(1)};
        s.pulseInner = true;
        s.structures = {buildings(3), mountains(2), ringTower(1)};
        s.structSpacing = 220;
        s.background = join(s.background, {floatStar(1), floatSquare(1)});
        v.push_back(s);
    }
    {
        Style s;
        s.name = "Monochrome";
        s.blurb = "Black and white, crisp lines, floating shapes";
        s.colors = {{235, 235, 235}, {20, 20, 20}, {200, 200, 200}, {120, 120, 120}};
        s.outline = OutlineKind::Classic;
        s.glow = GlowKind::None;
        s.background = {{1764, 1, 1.f, 2.f, true}, {1765, 1, 1.f, 2.f, true}, {1766, 1, 1.f, 2.f, true}, {1767, 1, 1.f, 2.f, true}, {1768, 1, 1.f, 2.f, true}};
        s.backSpacing = 90;
        s.backChance = 0.5f;
        s.backOpacity = 0.7f;
        s.top = layer({obelisk(), post()}, 0.5f, 5);
        s.inner = {innerFrame(2), innerDiamond(1), innerDot(1)};
        s.innerMix = 0.35f;
        s.structures = {pillar(2), arch(2), bigObelisk(1)};
        s.structSpacing = 230;
        s.background = join(s.background, {floatSquare(2), floatHex(1), floatRing(1)});
        v.push_back(s);
    }
    {
        Style s;
        s.name = "Candy";
        s.blurb = "Pastel pink, white blocks, puffy clouds";
        s.colors = {{255, 200, 225}, {255, 255, 255}, {255, 105, 180}, {255, 240, 120}};
        s.outline = OutlineKind::Classic;
        s.glow = GlowKind::Small;
        s.background = {{222, 2, 1.f, 1.5f}, {223, 2, 1.f, 1.5f}, {224, 2, 1.f, 1.5f}, {3625, 1, 0.5f, 0.9f, true, true}};
        s.backSpacing = 130;
        s.backChance = 0.4f;
        s.backOpacity = 0.7f;
        s.pulseOutline = false;
        s.top = layer({heartPlant(2), flower(2), mushroom(2), {939, 1}}, 0.85f, 3);
        s.ground = layer({heartPlant(1), flower(2)}, 0.6f, 3);
        s.hang = layer({chainGem()}, 0.4f, 5, 3, 4);
        s.inner = {innerHeart(1), innerDot(2)};
        s.innerMix = 0.35f;
        s.structures = {bigTree(1), pagoda(1)};
        s.structSpacing = 320;
        s.background = join(s.background, {floatHeart(2), cloud(2)});
        v.push_back(s);
    }
    {
        Style s;
        s.name = "Abyss";
        s.blurb = "Deep sea glow, hanging weeds, light orbs";
        s.colors = {{0, 20, 30}, {0, 10, 16}, {0, 255, 200}, {0, 140, 160}};
        s.outline = OutlineKind::Small;
        s.glow = GlowKind::Large;
        s.background = {{1886, 2, 0.6f, 1.2f}, {1887, 3, 0.6f, 1.2f}, {1888, 1, 0.6f, 1.f}, {3807, 2, 0.5f, 1.f, false, true}};
        s.backSpacing = 120;
        s.backChance = 0.45f;
        s.pulseOutline = false;
        s.pulseDetails = true;
        s.hang = layer({vines(3), {406, 1}, {407, 1}, {408, 1}}, 0.8f, 3, 3, 2);
        s.top = layer({crystals(2), {406, 1}, {407, 1}}, 0.6f, 3);
        s.inner = {innerRing(1), innerDot(2)};
        s.structures = {spire(2), ruins(2)};
        s.structSpacing = 260;
        s.background = join(s.background, {floatOrb(2)});
        v.push_back(s);
    }
    {
        Style s;
        s.name = "Golden Hour";
        s.blurb = "Warm sunset, grass tops, big soft clouds";
        s.colors = {{255, 140, 60}, {60, 25, 10}, {255, 220, 120}, {255, 190, 90}};
        s.outline = OutlineKind::Classic;
        s.glow = GlowKind::Small;
        s.background = {{129, 1, 1.f, 1.6f}, {130, 1, 1.f, 1.4f}, {131, 1, 1.f, 1.6f}};
        s.backSpacing = 180;
        s.backChance = 0.35f;
        s.backOpacity = 0.5f;
        s.pulseGlow = false;
        s.top = layer(join(grass(), {flower(2), sprout(2), tree(1)}), 0.8f, 3, 3, 2);
        s.ground = layer(join(bushes(), {tree(2), fence(1), flower(1)}), 0.7f, 3);
        s.inner = {innerFrame(1)};
        s.innerMix = 0.15f;
        s.innerSpacing = 4;
        s.structures = {pine(2), mountains(2), bigTree(2)};
        s.structSpacing = 220;
        s.background = join(s.background, {cloud(2)});
        v.push_back(s);
    }
    {
        Style s;
        s.name = "Cyber Grid";
        s.blurb = "Thin green lines, data dots";
        s.colors = {{0, 12, 6}, {0, 0, 0}, {0, 255, 90}, {0, 160, 60}};
        s.outline = OutlineKind::Small;
        s.glow = GlowKind::Small;
        s.background = {{1608, 2}, {1609, 2}, {1610, 2}, {1765, 1, 1.f, 1.6f, false, true}};
        s.backSpacing = 100;
        s.backChance = 0.5f;
        s.pulseBack = true;
        s.top = layer({antenna(2), beacon(1)}, 0.6f, 4);
        s.side = layer({vent()}, 0.35f, 4, 3, 3);
        s.inner = {innerGrill(1), innerFrameDot(2), innerPlus(1)};
        s.innerMix = 0.3f;
        s.pulseInner = true;
        s.structures = {tower(2), buildings(2)};
        s.structSpacing = 230;
        s.background = join(s.background, {floatSquare(1)});
        v.push_back(s);
    }
    {
        Style s;
        s.name = "Starfield";
        s.blurb = "Black blocks under a sky full of stars";
        s.colors = {{2, 2, 12}, {0, 0, 0}, {255, 255, 255}, {200, 210, 255}};
        s.outline = OutlineKind::Small;
        s.glow = GlowKind::None;
        s.background = {{3625, 2, 0.3f, 0.7f, true}, {3966, 3, 0.3f, 0.7f, true}, {3967, 2, 0.3f, 0.7f, true}, {54, 1, 0.5f, 0.8f, false, true}};
        s.backSpacing = 80;
        s.backChance = 0.55f;
        s.backOpacity = 0.8f;
        s.pulseBack = true;
        s.top = layer({beacon()}, 0.35f, 6);
        s.inner = {innerStar(1)};
        s.innerSpacing = 4;
        s.innerMix = 0.25f;
        s.structures = {mountains(2), ringTower(1)};
        s.structSpacing = 320;
        s.background = join(s.background, {floatStar(2)});
        v.push_back(s);
    }
    {
        Style s;
        s.name = "Toxic";
        s.blurb = "Acid green glow, small thorns, glowing orbs";
        s.colors = {{10, 22, 0}, {6, 12, 0}, {170, 255, 0}, {110, 200, 0}};
        s.outline = OutlineKind::Neon;
        s.glow = GlowKind::Large;
        s.background = {{1886, 1, 0.6f, 1.f}, {1887, 2, 0.6f, 1.f}};
        s.backSpacing = 130;
        s.backChance = 0.4f;
        s.pulseDetails = true;
        s.top = layer(join(smallThorns(), {mushroom(2), crystals(1)}), 0.7f, 3);
        s.hang = layer({hangCrystal(1), vines(2)}, 0.6f, 4, 3, 3);
        s.inner = {innerHex(2), innerDot(1)};
        s.structures = {factory(2), tower(1)};
        s.structSpacing = 260;
        s.background = join(s.background, {floatOrb(1), floatHex(1)});
        v.push_back(s);
    }
    {
        Style s;
        s.name = "Royal";
        s.blurb = "Gold trim, hanging chains, slow shines";
        s.colors = {{14, 10, 40}, {20, 14, 60}, {255, 205, 60}, {255, 230, 150}};
        s.outline = OutlineKind::Thick;
        s.glow = GlowKind::Medium;
        s.background = {{1019, 1, 0.8f, 1.2f, true}, {1020, 2, 0.8f, 1.2f, true}, {1021, 2, 0.8f, 1.2f, true}};
        s.backSpacing = 150;
        s.backChance = 0.35f;
        s.pulseGlow = false;
        s.hang = layer(join(chains(), {banner(3), chainGem(2)}), 0.85f, 3, 3, 3);
        s.top = layer({candles(2), obelisk(1), post(1)}, 0.6f, 4);
        s.inner = {innerDiamond(2), innerFrameDot(1)};
        s.structures = {pillar(2), arch(2), bigObelisk(1)};
        s.structSpacing = 240;
        s.background = join(s.background, {floatDiamond(1)});
        v.push_back(s);
    }
    {
        Style s;
        s.name = "Sakura";
        s.blurb = "Pale pink sky, flowers and falling petals";
        s.colors = {{255, 220, 230}, {70, 25, 50}, {255, 150, 190}, {255, 190, 210}};
        s.outline = OutlineKind::Classic;
        s.glow = GlowKind::Small;
        s.background = {{3866, 2, 0.6f, 1.2f, true}, {3899, 2, 0.6f, 1.2f, true}};
        s.backSpacing = 110;
        s.backChance = 0.45f;
        s.backOpacity = 0.8f;
        s.pulseOutline = false;
        s.top = layer(join(bushes(), {flower(3), {939, 2}, tree(1)}), 0.85f, 3, 3, 2);
        s.ground = layer({flower(2), tree(1)}, 0.6f, 3);
        s.hang = layer({hangLamp()}, 0.4f, 5, 3, 4);
        s.inner = {innerLeaf(1), innerDot(1)};
        s.innerSpacing = 4;
        s.structures = {pagoda(2), bigTree(2), mountains(1)};
        s.structSpacing = 250;
        s.background = join(s.background, {lantern(1)});
        v.push_back(s);
    }
    {
        Style s;
        s.name = "Blackout";
        s.blurb = "Pure black with a white glow that hits on the beat";
        s.colors = {{0, 0, 0}, {0, 0, 0}, {200, 200, 200}, {255, 255, 255}};
        s.outline = OutlineKind::None;
        s.glow = GlowKind::Medium;
        s.pulseStrength = 0.9f;
        s.downbeatBack = false;
        s.downbeatBG = true;
        s.inner = {innerFrame()};
        s.innerMix = 0.08f;
        s.innerSpacing = 4;
        s.pulseInner = true;
        v.push_back(s);
    }
    {
        Style s;
        s.name = "Storm";
        s.blurb = "Grey sky, dark clouds, crackling electricity";
        s.colors = {{30, 36, 48}, {18, 20, 26}, {150, 200, 255}, {90, 100, 120}};
        s.outline = OutlineKind::Classic;
        s.glow = GlowKind::Small;
        s.background = {{129, 2, 1.f, 1.6f}, {130, 1, 1.f, 1.4f}, {131, 2, 1.f, 1.6f}, {1697, 1, 0.8f, 1.2f, true, true}};
        s.backSpacing = 160;
        s.backChance = 0.35f;
        s.downbeatBG = true;
        s.top = layer({antenna(2), post(1)}, 0.5f, 5);
        s.inner = {innerCross(1), innerDot(1)};
        s.structures = {buildings(2), tower(2)};
        s.structSpacing = 230;
        s.background = join(s.background, {cloud(2)});
        v.push_back(s);
    }
    {
        Style s;
        s.name = "Retro Arcade";
        s.blurb = "Chunky yellow outlines, bouncing shapes";
        s.colors = {{0, 0, 40}, {0, 0, 90}, {255, 255, 0}, {255, 0, 90}};
        s.outline = OutlineKind::Thick;
        s.glow = GlowKind::None;
        s.background = {{1764, 2, 1.2f, 2.f, true}, {1766, 2, 1.2f, 2.f, true}, {1767, 2, 1.2f, 2.f, true}, {3804, 1, 0.5f, 0.9f, true, true}};
        s.backSpacing = 100;
        s.backChance = 0.45f;
        s.pulseBack = true;
        s.top = layer({starPost(2), post(1)}, 0.6f, 4);
        s.inner = {innerFrameDot(2), innerPlus(1)};
        s.innerMix = 0.3f;
        s.pulseInner = true;
        s.structures = {buildings(2)};
        s.structSpacing = 260;
        s.background = join(s.background, {floatSquare(1), floatStar(1)});
        v.push_back(s);
    }
    {
        Style s;
        s.name = "Bloodmoon";
        s.blurb = "Crimson glow, big thorns, hanging spikes";
        s.colors = {{30, 0, 6}, {10, 0, 2}, {255, 30, 60}, {120, 0, 20}};
        s.outline = OutlineKind::Neon;
        s.glow = GlowKind::Medium;
        s.background = {{497, 1, 1.2f, 2.f}, {1596, 1, 0.8f, 1.2f, false, true}};
        s.backSpacing = 200;
        s.backChance = 0.25f;
        s.downbeatBG = true;
        s.top = layer(join(thorns(0.8f, 1.2f), {spikes(2), tombstone(2)}), 0.8f, 3);
        s.hang = layer(join(decoSpikes(0.7f, 1.1f), {stalactites(2)}), 0.6f, 3, 3, 3);
        s.ground = layer({tombstone(1), spikes(1)}, 0.5f, 4);
        s.inner = {innerCross(1), innerDiamond(1)};
        s.structures = {ruins(2), spire(1), bigTree(1)};
        s.structSpacing = 260;
        s.background = join(s.background, {floatDiamond(1)});
        v.push_back(s);
    }
    return v;
}

} // namespace deco
