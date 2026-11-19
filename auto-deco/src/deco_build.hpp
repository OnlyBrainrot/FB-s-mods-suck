// Auto Deco - building a full decoration from a style.
//
// Plain C++, no game dependency (tested in test/build_test.cpp). Takes the
// level's solid blocks (as grid cells), a style and a palette, and returns the
// objects to add: block fill, outline and glow, decoration growing on top of,
// under and out of the sides of structures, ground decoration, background
// objects, the colour triggers that set the palette, and pulse triggers on the
// song's beats.
#pragma once

#include "deco_core.hpp"

#include <climits>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace deco {

struct RGB {
    std::uint8_t r = 255, g = 255, b = 255;
    bool operator==(const RGB& o) const { return r == o.r && g == o.g && b == o.b; }
};

inline RGB mix(RGB a, RGB b, float t) {
    t = std::clamp(t, 0.f, 1.f);
    auto m = [t](std::uint8_t x, std::uint8_t y) {
        return static_cast<std::uint8_t>(std::lround(x + (static_cast<float>(y) - x) * t));
    };
    return {m(a.r, b.r), m(a.g, b.g), m(a.b, b.b)};
}
inline RGB lighten(RGB c, float t) { return mix(c, {255, 255, 255}, t); }
inline RGB darken(RGB c, float t) { return mix(c, {0, 0, 0}, t); }

// The four colours a user picks. Everything else is derived from them.
struct Palette {
    RGB background;
    RGB blocks;    // block fill
    RGB primary;   // outline, glow, ground line
    RGB secondary; // details: decoration and background objects
};

enum class OutlineKind { None, Classic, Small, Neon, Thick };
enum class GlowKind { None, Small, Medium, Large };

struct Item {
    int id = 0;
    float weight = 1.f;
    float scaleMin = 1.f, scaleMax = 1.f;
    bool spin = false;      // random rotation (background only)
    bool alt = false;       // background: use the second background colour
};

// Decoration attached to one kind of surface.
struct SurfaceLayer {
    std::vector<Item> items;
    float chance = 0.f; // per exposed face
    int minGap = 0;     // empty cells kept between two pieces in a row
};

struct Style {
    std::string name;
    std::string blurb;
    Palette colors;

    bool fill = true;
    OutlineKind outline = OutlineKind::Classic;
    GlowKind glow = GlowKind::Medium;

    SurfaceLayer top;    // standing on top faces
    SurfaceLayer hang;   // hanging under ceilings
    SurfaceLayer side;   // growing out of walls
    SurfaceLayer ground; // along the ground line
    int hangClearance = 3; // empty cells needed under a ceiling

    std::vector<Item> background;
    float backSpacing = 0.f; // distance between background points; 0 = none
    float backChance = 0.f;
    float backOpacity = 0.55f;

    // pulses on every beat / on downbeats only
    bool pulseOutline = true, pulseGlow = true, pulseDetails = false, pulseBack = false;
    bool downbeatBack = true, downbeatBG = false;
    float pulseStrength = 0.55f;
};

// Colour channels the decoration uses, counted from `base`.
struct Channels {
    int fill, outline, glow, details, back, back2;
    explicit Channels(int base = 990)
        : fill(base), outline(base + 1), glow(base + 2), details(base + 3), back(base + 4), back2(base + 5) {}
};

constexpr int CH_BG = 1000;
constexpr int CH_G1 = 1001;
constexpr int CH_LINE = 1002;
constexpr int CH_G2 = 1009;

struct ChannelColor {
    int channel;
    RGB color;
    float opacity = 1.f;
};

inline std::vector<ChannelColor> channelColors(const Style& style, const Palette& p, const Channels& ch) {
    return {
        {CH_BG, p.background},
        {CH_G1, darken(p.background, 0.45f)},
        {CH_G2, darken(p.background, 0.6f)},
        {CH_LINE, p.primary},
        {ch.fill, p.blocks},
        {ch.outline, p.primary},
        {ch.glow, p.primary},
        {ch.details, p.secondary},
        {ch.back, mix(p.secondary, p.background, 0.35f), style.backOpacity},
        {ch.back2, mix(p.primary, p.background, 0.45f), style.backOpacity},
    };
}

inline std::pair<int, int> outlineIDs(OutlineKind k) {
    switch (k) {
        case OutlineKind::Classic: return {468, 472};
        case OutlineKind::Small: return {1154, 1158};
        case OutlineKind::Neon: return {1191, 1194};
        case OutlineKind::Thick: return {1202, 1205};
        default: return {0, 0};
    }
}

struct GlowIDs {
    int edge = 0, outer = 0, inner = 0;
};
inline GlowIDs glowIDs(GlowKind k) {
    switch (k) {
        case GlowKind::Small: return {1292, 1009, 1010};
        case GlowKind::Medium: return {503, 504, 505};
        case GlowKind::Large: return {1011, 1012, 1013};
        default: return {};
    }
}

// What the decoration is made of, before it becomes a level string.
enum class Role { Fill, Outline, Glow, Top, Hang, Side, Ground, Background, ColorTrigger, PulseTrigger };

struct Obj {
    Role role;
    int id;
    float x, y; // editor node position
    int rotation = 0;
    float scale = 1.f;
    int color = 0;
    int zLayer = 0;
    int zOrder = 0;
    // trigger fields
    RGB rgb{};
    int target = 0;
    float opacity = 1.f;
    float fadeOut = 0.f;
};

struct Params {
    CellSet solid;
    Vec origin;
    Anchors anchors; // outline and glow pieces (measured)
    // where the "foot" of a decoration item is: an edge anchor on the Down side
    std::function<EdgeAnchor(int id)> itemFoot;
    unsigned seed = 1;

    bool fill = true, outline = true, glow = true;
    bool top = true, hang = true, side = true, ground = true, background = true;
    bool colorTriggers = true, pulses = true;
    Channels channels{990};
    float groundY = 90.f; // top of the ground, node coordinates

    // beats in level time (seconds); timeToX converts to a position
    std::vector<double> beats;
    std::vector<bool> downbeat;
    std::function<float(double)> timeToX;
    float beatInterval = 0.5f; // used for pulse length
};

namespace detail {

inline double hash01(unsigned seed, int a, int b, int c) {
    std::uint64_t z = (static_cast<std::uint64_t>(seed) << 32) ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(a)) * 0x9E3779B97F4A7C15ull) ^
                      (static_cast<std::uint64_t>(static_cast<std::uint32_t>(b)) * 0xC2B2AE3D27D4EB4Full) ^
                      (static_cast<std::uint64_t>(static_cast<std::uint32_t>(c)) * 0x165667B19E3779F9ull);
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return static_cast<double>(z >> 11) * (1.0 / 9007199254740992.0);
}

inline const Item* pick(const std::vector<Item>& items, double u) {
    float total = 0.f;
    for (const auto& it : items) total += std::max(it.weight, 0.f);
    if (items.empty() || total <= 0.f) return nullptr;
    double acc = 0.0;
    const double target = u * total;
    for (const auto& it : items) {
        acc += std::max(it.weight, 0.f);
        if (target < acc) return &it;
    }
    return &items.back();
}

inline float lerp(float a, float b, double t) { return a + static_cast<float>((b - a) * t); }

} // namespace detail

struct Bounds {
    float minX = 0, maxX = 0, maxY = 0;
    bool valid = false;
};

inline Bounds bounds(const CellSet& solid, Vec origin) {
    Bounds b;
    for (const auto& c : solid) {
        const float x0 = origin.x + c.first * CELL, y1 = origin.y + (c.second + 1) * CELL;
        if (!b.valid) {
            b = {x0, x0 + CELL, y1, true};
        } else {
            b.minX = std::min(b.minX, x0);
            b.maxX = std::max(b.maxX, x0 + CELL);
            b.maxY = std::max(b.maxY, y1);
        }
    }
    return b;
}

inline std::vector<Obj> build(const Style& style, const Palette& pal, const Params& p) {
    std::vector<Obj> out;
    if (p.solid.empty()) return out;
    const Channels& ch = p.channels;
    auto has = [&](int i, int j) { return p.solid.count({i, j}) != 0; };
    const Bounds b = bounds(p.solid, p.origin);
    const int groundRow = static_cast<int>(std::lround((p.groundY - p.origin.y) / CELL)); // first row above the ground

    // ---- blocks: fill, outline, glow ---------------------------------------
    Options opt;
    opt.fill = p.fill && style.fill;
    opt.outline = p.outline && style.outline != OutlineKind::None;
    opt.glow = p.glow && style.glow != GlowKind::None;
    const auto [outlineEdge, outlineInner] = outlineIDs(style.outline);
    const GlowIDs glow = glowIDs(style.glow);
    for (const auto& piece : generate(p.solid, p.origin, opt, p.anchors)) {
        Obj o{Role::Fill, 0, piece.x, piece.y, piece.rotation};
        switch (piece.piece) {
            case Piece::Fill: o.role = Role::Fill; o.id = 211; o.color = ch.fill; o.zLayer = 5; o.zOrder = 1; break;
            case Piece::OutlineEdge: o.role = Role::Outline; o.id = outlineEdge; o.color = ch.outline; o.zLayer = 5; o.zOrder = 2; break;
            case Piece::OutlineInner: o.role = Role::Outline; o.id = outlineInner; o.color = ch.outline; o.zLayer = 5; o.zOrder = 2; break;
            case Piece::GlowEdge: o.role = Role::Glow; o.id = glow.edge; o.color = ch.glow; o.zLayer = 1; break;
            case Piece::GlowOuter: o.role = Role::Glow; o.id = glow.outer; o.color = ch.glow; o.zLayer = 1; break;
            case Piece::GlowInner: o.role = Role::Glow; o.id = glow.inner; o.color = ch.glow; o.zLayer = 1; break;
        }
        if (o.id > 0) out.push_back(o);
    }

    // ---- surface decoration --------------------------------------------------
    auto placeItem = [&](Role role, const SurfaceLayer& layer, Vec mid, int towardsSurface, int i, int j, int slot) {
        const Item* item = detail::pick(layer.items, detail::hash01(p.seed, i, j, slot * 7 + 1));
        if (!item) return;
        const double us = detail::hash01(p.seed, i, j, slot * 7 + 2);
        const float scale = detail::lerp(item->scaleMin, item->scaleMax, us);
        EdgeAnchor foot = p.itemFoot ? p.itemFoot(item->id) : EdgeAnchor{Down, 0.f};
        foot.dist *= scale;
        Placed pl = placeEdge(Piece::Fill, foot, mid, towardsSurface);
        Obj o{role, item->id, pl.x, pl.y, pl.rotation, scale, ch.details, 3 /* B1 */, 0};
        out.push_back(o);
    };

    // top faces, scanned in rows so minGap works
    auto scanRuns = [&](Role role, const SurfaceLayer& layer, int slot, auto exposedAt, auto midOf, int towards) {
        if (layer.items.empty() || layer.chance <= 0.f) return;
        std::map<int, std::vector<int>> rows;
        for (const auto& c : p.solid) rows[c.second].push_back(c.first);
        for (auto& [j, is] : rows) {
            std::sort(is.begin(), is.end());
            int last = INT_MIN / 2;
            for (int i : is) {
                if (!exposedAt(i, j)) continue;
                if (i - last <= layer.minGap) continue;
                if (detail::hash01(p.seed, i, j, slot) >= layer.chance) continue;
                placeItem(role, layer, midOf(i, j), towards, i, j, slot);
                last = i;
            }
        }
    };

    if (p.top) {
        scanRuns(Role::Top, style.top, 11,
                 [&](int i, int j) { return !has(i, j + 1); },
                 [&](int i, int j) { const Vec c = cellCentre({i, j}, p.origin); return Vec{c.x, c.y + HALF}; },
                 Down);
    }
    if (p.hang) {
        scanRuns(Role::Hang, style.hang, 12,
                 [&](int i, int j) {
                     if (j - style.hangClearance < groundRow) return false;
                     for (int d = 1; d <= style.hangClearance; ++d) {
                         if (has(i, j - d)) return false;
                     }
                     return true;
                 },
                 [&](int i, int j) { const Vec c = cellCentre({i, j}, p.origin); return Vec{c.x, c.y - HALF}; },
                 Up);
    }
    if (p.side && !style.side.items.empty() && style.side.chance > 0.f) {
        for (const auto& c : p.solid) {
            for (int dir : {Left, Right}) {
                const int ni = c.first + SIDE_CELL[dir][0];
                if (has(ni, c.second)) continue;
                if (detail::hash01(p.seed, c.first * 2 + (dir == Left), c.second, 13) >= style.side.chance) continue;
                const Vec centre = cellCentre(c, p.origin);
                const Vec mid{centre.x + HALF * SIDE_CELL[dir][0], centre.y};
                // the piece sits in the air cell; the block is in the opposite direction
                placeItem(Role::Side, style.side, mid, dir == Left ? Right : Left, c.first * 2 + (dir == Left), c.second, 13);
            }
        }
    }
    if (p.ground && !style.ground.items.empty() && style.ground.chance > 0.f) {
        const int i0 = static_cast<int>(std::floor((b.minX - p.origin.x) / CELL)) - 2;
        const int i1 = static_cast<int>(std::ceil((b.maxX - p.origin.x) / CELL)) + 2;
        int last = INT_MIN / 2;
        for (int i = i0; i <= i1; ++i) {
            if (has(i, groundRow)) continue; // a block stands on the ground here
            if (i - last <= style.ground.minGap) continue;
            if (detail::hash01(p.seed, i, -999, 14) >= style.ground.chance) continue;
            const Vec mid{p.origin.x + i * CELL + HALF, p.groundY};
            placeItem(Role::Ground, style.ground, mid, Down, i, -999, 14);
            last = i;
        }
    }

    // ---- background -------------------------------------------------------------
    if (p.background && !style.background.empty() && style.backSpacing > 1.f && style.backChance > 0.f) {
        const float s = style.backSpacing;
        const float x0 = b.minX - s, x1 = b.maxX + s;
        const float y0 = p.groundY + s * 0.5f, y1 = std::max(b.maxY, p.groundY + 300.f) + s * 2.f;
        int gi = 0;
        for (float x = x0; x <= x1; x += s, ++gi) {
            int gj = 0;
            for (float y = y0; y <= y1; y += s, ++gj) {
                if (detail::hash01(p.seed, gi, gj, 21) >= style.backChance) continue;
                const float px = x + static_cast<float>((detail::hash01(p.seed, gi, gj, 22) - 0.5) * s * 0.8);
                const float py = y + static_cast<float>((detail::hash01(p.seed, gi, gj, 23) - 0.5) * s * 0.8);
                const int ci = static_cast<int>(std::floor((px - p.origin.x) / CELL));
                const int cj = static_cast<int>(std::floor((py - p.origin.y) / CELL));
                if (has(ci, cj)) continue; // would be hidden behind a block
                const Item* item = detail::pick(style.background, detail::hash01(p.seed, gi, gj, 24));
                if (!item) continue;
                Obj o{Role::Background, item->id, px, py};
                o.scale = detail::lerp(item->scaleMin, item->scaleMax, detail::hash01(p.seed, gi, gj, 25));
                o.rotation = item->spin ? static_cast<int>(detail::hash01(p.seed, gi, gj, 26) * 360.0) : 0;
                o.color = item->alt ? ch.back2 : ch.back;
                o.zLayer = -3; // B4
                o.zOrder = -5;
                out.push_back(o);
            }
        }
    }

    // ---- triggers -----------------------------------------------------------------
    const float triggerY = b.maxY + 90.f;
    if (p.colorTriggers) {
        int k = 0;
        for (const auto& cc : channelColors(style, pal, ch)) {
            Obj o{Role::ColorTrigger, 899, 15.f, triggerY + 30.f * k++};
            o.rgb = cc.color;
            o.target = cc.channel;
            o.opacity = cc.opacity;
            out.push_back(o);
        }
    }

    if (p.pulses && p.timeToX && !p.beats.empty()) {
        const float fade = std::clamp(p.beatInterval * 0.55f, 0.08f, 0.5f);
        const float endX = b.maxX + 300.f;
        struct Target {
            int channel;
            RGB color;
            bool everyBeat;
        };
        std::vector<Target> targets;
        const RGB lightPrimary = lighten(pal.primary, style.pulseStrength);
        if (style.pulseOutline && p.outline && style.outline != OutlineKind::None) targets.push_back({ch.outline, lightPrimary, true});
        if (style.pulseGlow && p.glow && style.glow != GlowKind::None) targets.push_back({ch.glow, lightPrimary, true});
        if (style.pulseDetails) targets.push_back({ch.details, lighten(pal.secondary, style.pulseStrength), true});
        if (style.pulseBack || style.downbeatBack) {
            targets.push_back({ch.back, lighten(mix(pal.secondary, pal.background, 0.35f), style.pulseStrength), style.pulseBack});
        }
        if (style.downbeatBG) targets.push_back({CH_BG, lighten(pal.background, 0.22f), false});

        for (size_t k = 0; k < p.beats.size(); ++k) {
            if (p.beats[k] < 0) continue;
            const bool down = k < p.downbeat.size() && p.downbeat[k];
            const float x = p.timeToX(p.beats[k]);
            if (!std::isfinite(x) || x < 0.f || x > endX) continue;
            int row = 0;
            for (const auto& t : targets) {
                if (!t.everyBeat && !down) continue;
                Obj o{Role::PulseTrigger, 1006, x, triggerY + 30.f * (11 + row++)};
                o.rgb = t.color;
                o.target = t.channel;
                o.fadeOut = down ? fade * 1.6f : fade;
                out.push_back(o);
            }
        }
    }
    return out;
}

// Level-string form. Node y is 90 above level-string y.
inline std::string toLevelString(const std::vector<Obj>& objs, int group, int editorLayer, bool noTouch) {
    std::string s;
    s.reserve(objs.size() * 80);
    char buf[64];
    auto add = [&](int key, const char* fmt, auto value) {
        int n = std::snprintf(buf, sizeof buf, fmt, value);
        if (!s.empty() && s.back() != ';') s += ',';
        s += std::to_string(key);
        s += ',';
        if (n > 0) s.append(buf, static_cast<size_t>(std::min<int>(n, sizeof buf - 1)));
    };
    for (const auto& o : objs) {
        if (o.role == Role::ColorTrigger) {
            add(1, "%d", 899);
            add(2, "%.2f", o.x);
            add(3, "%.2f", o.y - 90.f);
            add(7, "%d", o.rgb.r);
            add(8, "%d", o.rgb.g);
            add(9, "%d", o.rgb.b);
            add(10, "%d", 0);
            add(23, "%d", o.target);
            add(35, "%.2f", o.opacity);
        } else if (o.role == Role::PulseTrigger) {
            add(1, "%d", 1006);
            add(2, "%.2f", o.x);
            add(3, "%.2f", o.y - 90.f);
            add(7, "%d", o.rgb.r);
            add(8, "%d", o.rgb.g);
            add(9, "%d", o.rgb.b);
            add(45, "%d", 0);
            add(46, "%d", 0);
            add(47, "%.3f", o.fadeOut);
            add(48, "%d", 0);
            add(51, "%d", o.target);
            add(52, "%d", 0);
        } else {
            add(1, "%d", o.id);
            add(2, "%.2f", o.x);
            add(3, "%.2f", o.y - 90.f);
            if (o.rotation != 0) add(6, "%d", o.rotation);
            add(21, "%d", o.color);
            add(24, "%d", o.zLayer);
            if (o.zOrder != 0) add(25, "%d", o.zOrder);
            if (std::fabs(o.scale - 1.f) > 0.001f) add(32, "%.2f", o.scale);
            if (noTouch) add(121, "%d", 1);
        }
        if (editorLayer != 0) add(20, "%d", editorLayer);
        if (group > 0) add(57, "%d", group);
        s += ';';
    }
    return s;
}

struct Counts {
    size_t fill = 0, outline = 0, glow = 0, details = 0, background = 0, triggers = 0;
    size_t total() const { return fill + outline + glow + details + background + triggers; }
};

inline Counts count(const std::vector<Obj>& objs) {
    Counts c;
    for (const auto& o : objs) {
        switch (o.role) {
            case Role::Fill: ++c.fill; break;
            case Role::Outline: ++c.outline; break;
            case Role::Glow: ++c.glow; break;
            case Role::Top: case Role::Hang: case Role::Side: case Role::Ground: ++c.details; break;
            case Role::Background: ++c.background; break;
            default: ++c.triggers; break;
        }
    }
    return c;
}

} // namespace deco
