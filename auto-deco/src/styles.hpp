// Auto Deco - the built-in styles.
// Object IDs: decoration only (thorns 123-128 and spikes 18-21 are the
// hitbox-free decorative ones). The mod also refuses, at runtime, any ID the
// game reports as a hazard or solid.
#pragma once

#include "deco_build.hpp"

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
} // namespace items

inline std::vector<Style> builtinStyles() {
    using namespace items;
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
        v.push_back(s);
    }
    {
        Style s;
        s.name = "Inferno";
        s.blurb = "Thorns, chains and fire";
        s.colors = {{40, 4, 0}, {20, 0, 0}, {255, 90, 0}, {200, 20, 10}};
        s.outline = OutlineKind::Thick;
        s.glow = GlowKind::Large;
        s.top = {thorns(0.6f, 1.1f), 0.55f, 0};
        s.hang = {chains(), 0.25f, 2};
        s.side = {smallThorns(), 0.2f, 0};
        s.ground = {thorns(0.7f, 1.2f), 0.4f, 0};
        s.background = {{3826, 3, 0.8f, 1.6f}, {1596, 1, 0.8f, 1.2f, false, true}};
        s.backSpacing = 130;
        s.backChance = 0.35f;
        s.pulseOutline = false;
        s.downbeatBG = true;
        v.push_back(s);
    }
    {
        Style s;
        s.name = "Frostbite";
        s.blurb = "Icicles under every ledge, falling snow";
        s.colors = {{6, 24, 48}, {10, 30, 60}, {170, 230, 255}, {230, 250, 255}};
        s.outline = OutlineKind::Classic;
        s.glow = GlowKind::Small;
        s.hang = {decoSpikes(0.6f, 1.1f), 0.5f, 0};
        s.hangClearance = 2;
        s.top = {{{21, 1, 0.5f, 0.8f}}, 0.2f, 1};
        s.background = {{3814, 3, 0.6f, 1.2f, true}, {222, 1, 0.8f, 1.2f, false, true}, {223, 1, 0.8f, 1.2f, false, true}, {224, 1, 0.8f, 1.2f, false, true}};
        s.backSpacing = 120;
        s.backChance = 0.4f;
        s.pulseGlow = false;
        v.push_back(s);
    }
    {
        Style s;
        s.name = "Overgrowth";
        s.blurb = "Grass, bushes and flowers on every surface";
        s.colors = {{30, 70, 50}, {14, 32, 18}, {120, 220, 90}, {170, 240, 120}};
        s.outline = OutlineKind::Classic;
        s.glow = GlowKind::Small;
        s.top = {join(grass(), join(bushes(), {{939, 1}})), 0.75f, 0};
        s.ground = {join(grass(), bushes()), 0.6f, 0};
        s.background = {{222, 1, 1.f, 1.6f}, {223, 1, 1.f, 1.6f}, {224, 1, 1.f, 1.6f}};
        s.backSpacing = 160;
        s.backChance = 0.35f;
        s.backOpacity = 0.4f;
        s.pulseOutline = false;
        v.push_back(s);
    }
    {
        Style s;
        s.name = "Factory";
        s.blurb = "Heavy outlines, chains, turning gears";
        s.colors = {{28, 30, 34}, {50, 52, 58}, {255, 180, 40}, {120, 125, 135}};
        s.outline = OutlineKind::Thick;
        s.glow = GlowKind::Small;
        s.top = {rods(), 0.25f, 3};
        s.hang = {chains(), 0.35f, 2};
        s.background = {{85, 1, 0.8f, 1.2f, true}, {86, 2, 0.8f, 1.2f, true}, {87, 2, 0.8f, 1.2f, true}, {97, 2, 0.8f, 1.2f, true}};
        s.backSpacing = 120;
        s.backChance = 0.5f;
        s.pulseGlow = false;
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
        v.push_back(s);
    }
    {
        Style s;
        s.name = "Candy";
        s.blurb = "Pastel pink, white blocks, puffy clouds";
        s.colors = {{255, 200, 225}, {255, 255, 255}, {255, 105, 180}, {255, 240, 120}};
        s.outline = OutlineKind::Classic;
        s.glow = GlowKind::Small;
        s.top = {{{939, 1}}, 0.3f, 1};
        s.background = {{222, 2, 1.f, 1.5f}, {223, 2, 1.f, 1.5f}, {224, 2, 1.f, 1.5f}, {3625, 1, 0.5f, 0.9f, true, true}};
        s.backSpacing = 130;
        s.backChance = 0.4f;
        s.backOpacity = 0.7f;
        s.pulseOutline = false;
        v.push_back(s);
    }
    {
        Style s;
        s.name = "Abyss";
        s.blurb = "Deep sea glow, hanging weeds, light orbs";
        s.colors = {{0, 20, 30}, {0, 10, 16}, {0, 255, 200}, {0, 140, 160}};
        s.outline = OutlineKind::Small;
        s.glow = GlowKind::Large;
        s.hang = {{{406, 1}, {407, 1}, {408, 1}}, 0.3f, 1};
        s.hangClearance = 2;
        s.background = {{1886, 2, 0.6f, 1.2f}, {1887, 3, 0.6f, 1.2f}, {1888, 1, 0.6f, 1.f}, {3807, 2, 0.5f, 1.f, false, true}};
        s.backSpacing = 120;
        s.backChance = 0.45f;
        s.pulseOutline = false;
        s.pulseDetails = true;
        v.push_back(s);
    }
    {
        Style s;
        s.name = "Golden Hour";
        s.blurb = "Warm sunset, grass tops, big soft clouds";
        s.colors = {{255, 140, 60}, {60, 25, 10}, {255, 220, 120}, {255, 190, 90}};
        s.outline = OutlineKind::Classic;
        s.glow = GlowKind::Small;
        s.top = {grass(), 0.5f, 0};
        s.ground = {bushes(), 0.3f, 1};
        s.background = {{129, 1, 1.f, 1.6f}, {130, 1, 1.f, 1.4f}, {131, 1, 1.f, 1.6f}};
        s.backSpacing = 180;
        s.backChance = 0.35f;
        s.backOpacity = 0.5f;
        s.pulseGlow = false;
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
        v.push_back(s);
    }
    {
        Style s;
        s.name = "Toxic";
        s.blurb = "Acid green glow, small thorns, glowing orbs";
        s.colors = {{10, 22, 0}, {6, 12, 0}, {170, 255, 0}, {110, 200, 0}};
        s.outline = OutlineKind::Neon;
        s.glow = GlowKind::Large;
        s.top = {smallThorns(), 0.35f, 0};
        s.background = {{1886, 1, 0.6f, 1.f}, {1887, 2, 0.6f, 1.f}};
        s.backSpacing = 130;
        s.backChance = 0.4f;
        s.pulseDetails = true;
        v.push_back(s);
    }
    {
        Style s;
        s.name = "Royal";
        s.blurb = "Gold trim, hanging chains, slow shines";
        s.colors = {{14, 10, 40}, {20, 14, 60}, {255, 205, 60}, {255, 230, 150}};
        s.outline = OutlineKind::Thick;
        s.glow = GlowKind::Medium;
        s.hang = {chains(), 0.3f, 2};
        s.background = {{1019, 1, 0.8f, 1.2f, true}, {1020, 2, 0.8f, 1.2f, true}, {1021, 2, 0.8f, 1.2f, true}};
        s.backSpacing = 150;
        s.backChance = 0.35f;
        s.pulseGlow = false;
        v.push_back(s);
    }
    {
        Style s;
        s.name = "Sakura";
        s.blurb = "Pale pink sky, flowers and falling petals";
        s.colors = {{255, 220, 230}, {70, 25, 50}, {255, 150, 190}, {255, 190, 210}};
        s.outline = OutlineKind::Classic;
        s.glow = GlowKind::Small;
        s.top = {join({{939, 2}}, bushes()), 0.35f, 0};
        s.background = {{3866, 2, 0.6f, 1.2f, true}, {3899, 2, 0.6f, 1.2f, true}};
        s.backSpacing = 110;
        s.backChance = 0.45f;
        s.backOpacity = 0.8f;
        s.pulseOutline = false;
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
        v.push_back(s);
    }
    {
        Style s;
        s.name = "Bloodmoon";
        s.blurb = "Crimson glow, big thorns, hanging spikes";
        s.colors = {{30, 0, 6}, {10, 0, 2}, {255, 30, 60}, {120, 0, 20}};
        s.outline = OutlineKind::Neon;
        s.glow = GlowKind::Medium;
        s.top = {thorns(0.8f, 1.2f), 0.4f, 1};
        s.hang = {decoSpikes(0.7f, 1.1f), 0.3f, 1};
        s.background = {{497, 1, 1.2f, 2.f}, {1596, 1, 0.8f, 1.2f, false, true}};
        s.backSpacing = 200;
        s.backChance = 0.25f;
        s.downbeatBG = true;
        v.push_back(s);
    }
    return v;
}

} // namespace deco
