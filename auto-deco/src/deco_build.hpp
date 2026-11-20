// Auto Deco - building a full decoration from a style.
//
// Plain C++, no game dependency (tested in test/build_test.cpp). Takes the
// level's solid blocks (as grid cells), a style and a palette, and returns the
// objects to add:
//   - block fill, outline and glow
//   - patterns inside large block masses
//   - details along surfaces: only on runs long enough to carry them, only
//     where there is open air (never inside corridors the player flies
//     through), evenly spaced and centred on the run
//   - custom details built from basic shapes (lamps, crystals, fences, trees...)
//   - background structures standing on the ground behind the level
//   - floating and spinning background objects, driven by move / rotate
//     trigger loops
//   - the colour triggers that set the palette, and pulse triggers on the
//     song's beats
#pragma once

#include "deco_core.hpp"

#include <array>
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
    RGB primary;   // outline, glow, lights, ground line
    RGB secondary; // details and background objects
};

enum class OutlineKind { None, Classic, Small, Neon, Thick };
enum class GlowKind { None, Small, Medium, Large };

// Which colour a piece takes.
enum class Tint { Detail, Accent, Inner, Back, Back2, Fill, Glow };
// How a background object moves.
enum class Motion { None, Float, Spin };

// One basic shape inside a custom detail. Local frame: the detail's foot is at
// (0, 0) on the surface it grows from, +y points away from that surface.
struct Part {
    int id = 0;
    float x = 0.f, y = 0.f; // bottom-centre of the part
    float w = 0.f, h = 0.f; // size in units (30 = one block); 0 = the object's own size
    float rot = 0.f;        // degrees clockwise, around the part's centre
    Tint tint = Tint::Detail;
    int z = 0;              // added to the z order
    bool point = false;     // a pointed shape (triangle): turned to point along +y first
};

struct Item {
    int id = 0;
    float weight = 1.f;
    float scaleMin = 1.f, scaleMax = 1.f;
    bool spin = false;      // background: random rotation, and slowly spins when animated
    bool alt = false;       // background: second background colour
    std::vector<Part> parts; // a custom detail made of shapes; empty = just `id`
    Motion motion = Motion::None; // background: None = decided by the style
};

// Decoration along one kind of surface.
struct SurfaceLayer {
    std::vector<Item> items;
    float chance = 0.f; // share of qualifying surfaces that get decorated
    int spacing = 3;    // cells between two pieces
    int minRun = 3;     // shortest surface (in cells) that gets anything
    int clearance = 3;  // open cells needed in front of the surface
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

    // patterns inside block masses, on a lattice `innerSpacing` cells apart
    std::vector<Item> inner;
    int innerSpacing = 3;
    float innerMix = 0.22f; // pattern colour: blocks -> primary
    bool innerBrick = true; // offset every other lattice row

    // structures standing on the ground behind everything
    std::vector<Item> structures;
    float structSpacing = 0.f; // units between structures; 0 = none

    // scattered background objects
    std::vector<Item> background;
    float backSpacing = 0.f; // distance between background points; 0 = none
    float backChance = 0.f;
    float backOpacity = 0.55f;
    float floatChance = 0.5f; // share of background objects that bob up and down

    // pulses on every beat / on downbeats only
    bool pulseOutline = true, pulseGlow = true, pulseDetails = false, pulseBack = false, pulseInner = false;
    bool downbeatBack = true, downbeatBG = false;
    float pulseStrength = 0.55f;
};

// Colour channels the decoration uses, counted from `base`.
struct Channels {
    int fill, outline, glow, details, back, back2, inner;
    explicit Channels(int base = 990)
        : fill(base), outline(base + 1), glow(base + 2), details(base + 3), back(base + 4), back2(base + 5), inner(base + 6) {}
};
constexpr int CHANNEL_COUNT = 7;

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
        {ch.inner, mix(p.blocks, p.primary, style.innerMix)},
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
enum class Role {
    Fill, Outline, Glow, Inner, Top, Hang, Side, Ground, Structure, Background,
    ColorTrigger, PulseTrigger, MoveTrigger, RotateTrigger, SpawnTrigger
};

inline bool isTrigger(Role r) { return r >= Role::ColorTrigger; }

struct Obj {
    Role role;
    int id;
    float x, y; // editor node position
    float rotation = 0.f;
    float scaleX = 1.f, scaleY = 1.f;
    int color = 0;
    int zLayer = 0;
    int zOrder = 0;
    int group = 0; // an extra group (animation), besides the Auto Deco tag
    // trigger fields
    RGB rgb{};
    int target = 0;
    float opacity = 1.f;
    float fadeOut = 0.f;
    float duration = 0.f;
    int moveX = 0, moveY = 0;
    int easing = 0;
    int degrees = 0;
    float delay = 0.f;
    bool spawnTriggered = false;
};

// Groups the animation needs: 3 float targets, 3 + 3 float loop controls,
// 2 spin targets, 2 spin loop controls.
constexpr int ANIM_GROUPS = 13;

struct Params {
    CellSet solid;
    Vec origin;
    Anchors anchors; // outline and glow pieces (measured)
    // the measured look of an object (ink bounds at scale 1, rotation 0)
    std::function<Shape(int id)> shapeOf;
    unsigned seed = 1;

    bool fill = true, outline = true, glow = true, inner = true;
    bool top = true, hang = true, side = true, ground = true;
    bool structures = true, background = true;
    bool colorTriggers = true, pulses = true, animate = true;
    Channels channels{990};
    float groundY = 90.f; // top of the ground, node coordinates
    std::vector<int> animGroups; // ANIM_GROUPS free group IDs; fewer = no animation

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

inline int floorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
inline int floorMod(int a, int b) { return a - floorDiv(a, b) * b; }

// rotate clockwise by `deg` degrees (y up)
inline Vec rotDeg(Vec v, float deg) {
    const float a = deg * 3.14159265358979f / 180.f;
    const float c = std::cos(a), s = std::sin(a);
    return {v.x * c + v.y * s, -v.x * s + v.y * c};
}

inline float normDeg(float d) {
    d = std::fmod(d, 360.f);
    if (d < 0.f) d += 360.f;
    if (d > 359.995f) d = 0.f;
    return d;
}

// Which way a pointed sprite points, from where its ink is heavy: a triangle
// pointing up has most of its ink below the middle.
inline int pointing(const Shape& s) {
    const float dx = s.cx - (s.minX + s.maxX) * 0.5f;
    const float dy = s.cy - (s.minY + s.maxY) * 0.5f;
    if (std::fabs(dy) >= std::fabs(dx)) return dy <= 0.f ? Up : Down;
    return dx <= 0.f ? Right : Left;
}

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

// Places items (single objects or custom details) at a foot point.
class Placer {
public:
    Placer(const Params& p, std::vector<Obj>& out) : m_p(p), m_out(out) {}

    Shape shape(int id) const {
        Shape s = m_p.shapeOf ? m_p.shapeOf(id) : Shape{};
        if (!s.valid || s.maxX - s.minX < 0.5f || s.maxY - s.minY < 0.5f) {
            s = Shape{true, -HALF, HALF, -HALF, HALF, 0.f, 0.f};
        }
        return s;
    }

    int channel(Tint t) const {
        const Channels& ch = m_p.channels;
        switch (t) {
            case Tint::Detail: return ch.details;
            case Tint::Accent: return ch.outline;
            case Tint::Inner: return ch.inner;
            case Tint::Back: return ch.back;
            case Tint::Back2: return ch.back2;
            case Tint::Fill: return ch.fill;
            case Tint::Glow: return ch.glow;
        }
        return ch.details;
    }

    struct Where {
        Vec foot;
        int facing = Up; // which way the detail's +y points
        float scale = 1.f;
        bool mirror = false;
        float spin = 0.f; // extra rotation of the whole detail (background)
        bool centred = false; // `foot` is the middle of the detail instead of its foot
    };
    struct Look {
        Role role;
        int zLayer;
        int zOrder;
        Tint tint; // for single-object items and parts left on Detail
        int group = 0;
    };

    // Natural size of a part in its upright frame, and whether its axes swap.
    void partFrame(const Part& part, const Shape& s, int& fix, float& uw, float& uh) const {
        fix = part.point ? mod4(-detail::pointing(s)) : 0;
        const float nw = s.maxX - s.minX, nh = s.maxY - s.minY;
        uw = (fix % 2) ? nh : nw;
        uh = (fix % 2) ? nw : nh;
    }

    // Half the width of an item across the surface, in units.
    float halfWidth(const Item& item, float scale) const {
        float hw = 0.f;
        forParts(item, Tint::Detail, [&](const Part& part) {
            int fix;
            float uw, uh;
            partFrame(part, shape(part.id), fix, uw, uh);
            const float w = part.w > 0.f ? part.w : uw;
            hw = std::max(hw, std::fabs(part.x) + w * 0.5f);
        });
        return hw * scale;
    }

    void place(const Item& item, const Where& at, const Look& look) const {
        float shift = 0.f;
        if (at.centred) {
            float lo = 1e9f, hi = -1e9f;
            forParts(item, look.tint, [&](const Part& part) {
                int fix;
                float uw, uh;
                partFrame(part, shape(part.id), fix, uw, uh);
                const float h = part.h > 0.f ? part.h : uh;
                lo = std::min(lo, part.y);
                hi = std::max(hi, part.y + h);
            });
            if (lo <= hi) shift = (lo + hi) * 0.5f * at.scale;
        }
        forParts(item, look.tint, [&](const Part& part) { placePart(part, at, look, shift); });
    }

private:
    template <class F>
    void forParts(const Item& item, Tint tint, F&& f) const {
        if (item.parts.empty()) {
            Part single;
            single.id = item.id;
            single.tint = item.alt ? Tint::Back2 : tint;
            f(single);
            return;
        }
        for (const auto& part : item.parts) f(part);
    }

    void placePart(const Part& part, const Where& at, const Look& look, float shift) const {
        if (part.id <= 0) return;
        const Shape s = shape(part.id);
        int fix;
        float uw, uh;
        partFrame(part, s, fix, uw, uh);
        const float tw = (part.w > 0.f ? part.w : uw) * at.scale;
        const float th = (part.h > 0.f ? part.h : uh) * at.scale;
        // scale along the upright axes, then along the sprite's own axes
        const float sxUp = tw / uw, syUp = th / uh;
        const float sx = (fix % 2) ? syUp : sxUp;
        const float sy = (fix % 2) ? sxUp : syUp;

        Vec c{(at.mirror ? -part.x : part.x) * at.scale, part.y * at.scale + th * 0.5f - shift};
        if (at.spin != 0.f) c = detail::rotDeg(c, at.spin);
        const Vec r = rotateCW(c, at.facing);
        const Vec centre{at.foot.x + r.x, at.foot.y + r.y};
        const float rot = (at.mirror ? -part.rot : part.rot) + at.spin + 90.f * static_cast<float>(at.facing + fix);
        // the object position is not always the middle of its ink
        const Vec ink{(s.minX + s.maxX) * 0.5f * sx, (s.minY + s.maxY) * 0.5f * sy};
        const Vec off = detail::rotDeg(ink, rot);

        Obj o{look.role, part.id, centre.x - off.x, centre.y - off.y};
        o.rotation = detail::normDeg(rot);
        o.scaleX = sx;
        o.scaleY = sy;
        o.color = channel(part.tint == Tint::Detail ? look.tint : part.tint);
        o.zLayer = look.zLayer;
        o.zOrder = look.zOrder + part.z;
        o.group = look.group;
        m_out.push_back(o);
    }

    const Params& m_p;
    std::vector<Obj>& m_out;
};

inline std::vector<Obj> build(const Style& style, const Palette& pal, const Params& p) {
    using detail::hash01;
    std::vector<Obj> out;
    if (p.solid.empty()) return out;
    const Channels& ch = p.channels;
    auto has = [&](int i, int j) { return p.solid.count({i, j}) != 0; };
    const Bounds b = bounds(p.solid, p.origin);
    const int groundRow = static_cast<int>(std::lround((p.groundY - p.origin.y) / CELL)); // first row above the ground
    Placer placer(p, out);

    // ---- blocks: fill, outline, glow ---------------------------------------
    Options opt;
    opt.fill = p.fill && style.fill;
    opt.outline = p.outline && style.outline != OutlineKind::None;
    opt.glow = p.glow && style.glow != GlowKind::None;
    const auto [outlineEdge, outlineInner] = outlineIDs(style.outline);
    const GlowIDs glow = glowIDs(style.glow);
    for (const auto& piece : generate(p.solid, p.origin, opt, p.anchors)) {
        Obj o{Role::Fill, 0, piece.x, piece.y};
        o.rotation = static_cast<float>(piece.rotation);
        switch (piece.piece) {
            case Piece::Fill: o.role = Role::Fill; o.id = 211; o.color = ch.fill; o.zLayer = 5; o.zOrder = 1; break;
            case Piece::OutlineEdge: o.role = Role::Outline; o.id = outlineEdge; o.color = ch.outline; o.zLayer = 5; o.zOrder = 4; break;
            case Piece::OutlineInner: o.role = Role::Outline; o.id = outlineInner; o.color = ch.outline; o.zLayer = 5; o.zOrder = 4; break;
            case Piece::GlowEdge: o.role = Role::Glow; o.id = glow.edge; o.color = ch.glow; o.zLayer = 1; break;
            case Piece::GlowOuter: o.role = Role::Glow; o.id = glow.outer; o.color = ch.glow; o.zLayer = 1; break;
            case Piece::GlowInner: o.role = Role::Glow; o.id = glow.inner; o.color = ch.glow; o.zLayer = 1; break;
        }
        if (o.id > 0) out.push_back(o);
    }

    // ---- patterns inside block masses ----------------------------------------
    if (p.inner && !style.inner.empty() && style.innerSpacing >= 2) {
        const int sp = style.innerSpacing;
        const int phaseI = static_cast<int>(hash01(p.seed, 1, 2, 31) * sp);
        const int phaseJ = static_cast<int>(hash01(p.seed, 3, 4, 31) * sp);
        for (const auto& c : p.solid) {
            const int lj = detail::floorDiv(c.second - phaseJ, sp);
            if (detail::floorMod(c.second - phaseJ, sp) != 0) continue;
            const int shift = (style.innerBrick && (lj & 1)) ? sp / 2 : 0;
            if (detail::floorMod(c.first - phaseI - shift, sp) != 0) continue;
            const int li = detail::floorDiv(c.first - phaseI - shift, sp);
            // the pattern must sit fully inside, clear of the outline
            bool inside = true;
            for (int di = -1; di <= 1 && inside; ++di) {
                for (int dj = -1; dj <= 1 && inside; ++dj) inside = has(c.first + di, c.second + dj);
            }
            if (!inside) continue;
            // neighbouring patterns match: one choice per patch of the lattice
            const Item* item = detail::pick(style.inner, hash01(p.seed, detail::floorDiv(li, 3), detail::floorDiv(lj, 3), 32));
            if (!item) continue;
            Placer::Where at;
            at.foot = cellCentre(c, p.origin);
            at.centred = true;
            placer.place(*item, at, {Role::Inner, 5, 2, Tint::Inner});
        }
    }

    // ---- surface decoration --------------------------------------------------
    // open cells from (i, j) going (di, dj), up to `cap`; the ground counts as solid
    auto openCells = [&](int i, int j, int di, int dj, int cap) {
        int n = 0;
        for (int d = 1; d <= cap; ++d) {
            const int ci = i + di * d, cj = j + dj * d;
            if (cj < groundRow || has(ci, cj)) break;
            ++n;
        }
        return n;
    };

    // A straight surface: `n` faces in a row, the first foot at `foot0`, each
    // next one `step` further.
    struct Run {
        Vec foot0;
        Vec step;
        int n;
        int key1, key2;
    };
    auto decorateRun = [&](const SurfaceLayer& layer, Role role, int facing, int slot, const Run& run) {
        if (layer.items.empty() || run.n < std::max(1, layer.minRun)) return;
        if (hash01(p.seed, run.key1, run.key2, slot) >= layer.chance) return;
        const int sp = std::max(1, layer.spacing);
        const int inset = run.n >= 5 ? 1 : 0;
        const int usable = run.n - 2 * inset;
        if (usable < 1) return;
        const int m = (usable - 1) / sp + 1;
        const float start = static_cast<float>(inset) + static_cast<float>(usable - 1 - (m - 1) * sp) * 0.5f;
        const float centre = static_cast<float>(m - 1) * 0.5f;
        for (int k = 0; k < m; ++k) {
            const float t = start + static_cast<float>(k * sp);
            // long surfaces change pieces every few metres; inside a section
            // two pieces alternate around the middle
            const int section = static_cast<int>(t) / 12;
            const Item* a = detail::pick(layer.items, hash01(p.seed, run.key1 + section, run.key2, slot + 1));
            const Item* bb = detail::pick(layer.items, hash01(p.seed, run.key1 + section, run.key2, slot + 2));
            const bool second = m >= 3 && (static_cast<int>(std::lround(std::fabs(static_cast<float>(k) - centre))) & 1);
            const Item* item = second && bb ? bb : a;
            if (!item) continue;
            Placer::Where at;
            at.foot = {run.foot0.x + run.step.x * t, run.foot0.y + run.step.y * t};
            at.facing = facing;
            at.scale = detail::lerp(item->scaleMin, item->scaleMax, hash01(p.seed, run.key1 + k, run.key2, slot + 3));
            at.mirror = hash01(p.seed, run.key1 + k, run.key2, slot + 4) < 0.5;
            // a piece must not stick out past the end of a short surface
            const float room = (static_cast<float>(run.n) * 0.5f) * CELL + 4.f;
            if (placer.halfWidth(*item, at.scale) > room) continue;
            placer.place(*item, at, {role, 3 /* B1 */, 0, Tint::Detail});
        }
    };

    // rows of faces: `exposed(i, j)` says whether cell (i, j) has the face
    auto rowRuns = [&](auto exposed, auto footOf, auto&& use) {
        std::map<int, std::vector<int>> rows;
        for (const auto& c : p.solid) rows[c.second].push_back(c.first);
        for (auto& [j, is] : rows) {
            std::sort(is.begin(), is.end());
            int first = 0, prev = 0, n = 0;
            auto flush = [&] {
                if (n > 0) use(Run{footOf(first, j), {CELL, 0.f}, n, first, j});
                n = 0;
            };
            for (int i : is) {
                if (!exposed(i, j)) {
                    flush();
                    continue;
                }
                if (n > 0 && i != prev + 1) flush();
                if (n == 0) first = i;
                prev = i;
                ++n;
            }
            flush();
        }
    };

    if (p.top) {
        // three open blocks at least: lower gaps are corridors the player flies through
        const auto& L = style.top;
        const int clear = std::max(3, L.clearance);
        rowRuns([&](int i, int j) { return openCells(i, j, 0, 1, clear) >= clear; },
                [&](int i, int j) { const Vec c = cellCentre({i, j}, p.origin); return Vec{c.x, c.y + HALF}; },
                [&](const Run& r) { decorateRun(L, Role::Top, Up, 110, r); });
    }
    if (p.hang) {
        const auto& L = style.hang;
        const int clear = std::max(3, L.clearance);
        rowRuns([&](int i, int j) { return j - 1 >= groundRow && openCells(i, j, 0, -1, clear) >= clear; },
                [&](int i, int j) { const Vec c = cellCentre({i, j}, p.origin); return Vec{c.x, c.y - HALF}; },
                [&](const Run& r) { decorateRun(L, Role::Hang, Down, 120, r); });
    }
    if (p.side && !style.side.items.empty()) {
        const auto& L = style.side;
        std::map<int, std::vector<int>> cols;
        for (const auto& c : p.solid) cols[c.first].push_back(c.second);
        for (auto& [i, js] : cols) {
            std::sort(js.begin(), js.end());
            for (int dir : {Left, Right}) {
                const int di = SIDE_CELL[dir][0];
                int first = 0, prev = 0, n = 0;
                auto flush = [&] {
                    if (n > 0) {
                        const Vec c = cellCentre({i, first}, p.origin);
                        decorateRun(L, Role::Side, dir, 130 + dir, Run{{c.x + HALF * di, c.y}, {0.f, CELL}, n, i * 2 + (dir == Left), first});
                    }
                    n = 0;
                };
                for (int j : js) {
                    const int clear = std::max(2, L.clearance);
                    const bool exposed = j >= groundRow && openCells(i, j, di, 0, clear) >= clear;
                    if (!exposed) {
                        flush();
                        continue;
                    }
                    if (n > 0 && j != prev + 1) flush();
                    if (n == 0) first = j;
                    prev = j;
                    ++n;
                }
                flush();
            }
        }
    }
    if (p.ground && !style.ground.items.empty()) {
        const auto& L = style.ground;
        const int i0 = static_cast<int>(std::floor((b.minX - p.origin.x) / CELL)) - 4;
        const int i1 = static_cast<int>(std::ceil((b.maxX - p.origin.x) / CELL)) + 4;
        int first = 0, n = 0;
        auto flush = [&] {
            if (n > 0) decorateRun(L, Role::Ground, Up, 140, Run{{p.origin.x + first * CELL + HALF, p.groundY}, {CELL, 0.f}, n, first, -999});
            n = 0;
        };
        for (int i = i0; i <= i1; ++i) {
            // open ground: nothing standing on it and room above
            const int clear = std::max(3, L.clearance);
            const bool open = !has(i, groundRow) && openCells(i, groundRow - 1, 0, 1, clear) >= clear;
            if (!open) {
                flush();
                continue;
            }
            if (n == 0) first = i;
            ++n;
        }
        flush();
    }

    // ---- animation groups ------------------------------------------------------
    const bool animate = p.animate && p.animGroups.size() >= static_cast<size_t>(ANIM_GROUPS);
    bool floatUsed[3] = {false, false, false};
    bool spinUsed[2] = {false, false};
    auto motionGroup = [&](const Item& item, int a, int bKey, int slot) -> int {
        if (!animate) return 0;
        Motion m = item.motion;
        if (m == Motion::None) {
            if (item.spin && item.parts.size() <= 1) m = Motion::Spin;
            else if (hash01(p.seed, a, bKey, slot) < style.floatChance) m = Motion::Float;
        }
        // a rotate trigger turns every object of a group around itself, so a
        // detail made of several shapes would fall apart: let it float instead
        if (m == Motion::Spin && item.parts.size() > 1) m = Motion::Float;
        if (m == Motion::Float) {
            const int k = static_cast<int>(hash01(p.seed, a, bKey, slot + 1) * 3) % 3;
            floatUsed[k] = true;
            return p.animGroups[k];
        }
        if (m == Motion::Spin) {
            const int k = static_cast<int>(hash01(p.seed, a, bKey, slot + 1) * 2) % 2;
            spinUsed[k] = true;
            return p.animGroups[9 + k];
        }
        return 0;
    };

    // ---- structures behind the level -----------------------------------------------
    if (p.structures && !style.structures.empty() && style.structSpacing > CELL) {
        const float s = style.structSpacing;
        int k = 0;
        for (float x = b.minX - s * 0.5f; x <= b.maxX + s; ++k) {
            const Item* item = detail::pick(style.structures, hash01(p.seed, k, 7, 41));
            if (item) {
                Placer::Where at;
                at.foot = {x, p.groundY};
                at.scale = detail::lerp(item->scaleMin, item->scaleMax, hash01(p.seed, k, 7, 42));
                at.mirror = hash01(p.seed, k, 7, 43) < 0.5;
                placer.place(*item, at, {Role::Structure, -3 /* B4 */, -9, item->alt ? Tint::Back2 : Tint::Back});
            }
            x += s * static_cast<float>(0.7 + 0.6 * hash01(p.seed, k, 7, 44));
        }
    }

    // ---- scattered background objects ---------------------------------------------
    if (p.background && !style.background.empty() && style.backSpacing > 1.f && style.backChance > 0.f) {
        const float s = style.backSpacing;
        const float x0 = b.minX - s, x1 = b.maxX + s;
        const float y0 = p.groundY + s * 0.5f, y1 = std::max(b.maxY, p.groundY + 300.f) + s * 2.f;
        int gi = 0;
        for (float x = x0; x <= x1; x += s, ++gi) {
            int gj = 0;
            for (float y = y0; y <= y1; y += s, ++gj) {
                if (hash01(p.seed, gi, gj, 21) >= style.backChance) continue;
                const float px = x + static_cast<float>((hash01(p.seed, gi, gj, 22) - 0.5) * s * 0.8);
                const float py = y + static_cast<float>((hash01(p.seed, gi, gj, 23) - 0.5) * s * 0.8);
                const int ci = static_cast<int>(std::floor((px - p.origin.x) / CELL));
                const int cj = static_cast<int>(std::floor((py - p.origin.y) / CELL));
                if (has(ci, cj)) continue; // would be hidden behind a block
                const Item* item = detail::pick(style.background, hash01(p.seed, gi, gj, 24));
                if (!item) continue;
                Placer::Where at;
                at.scale = detail::lerp(item->scaleMin, item->scaleMax, hash01(p.seed, gi, gj, 25));
                at.spin = item->spin ? static_cast<float>(std::floor(hash01(p.seed, gi, gj, 26) * 360.0)) : 0.f;
                at.mirror = hash01(p.seed, gi, gj, 27) < 0.5;
                at.foot = {px, py};
                at.centred = true; // the point is the middle of the object
                Placer::Look look{Role::Background, -3 /* B4 */, -5, item->alt ? Tint::Back2 : Tint::Back};
                look.group = motionGroup(*item, gi, gj, 28);
                placer.place(*item, at, look);
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

    if (animate) {
        // A loop is a pair of trigger groups that spawn each other: one moves
        // the objects up and, half a period later, spawns the other, which
        // moves them back down and spawns the first again. One spawn trigger
        // at the start of the level gets each loop going.
        struct Float {
            int amp;
            float period;
        };
        const Float floats[3] = {{10, 2.4f}, {16, 3.1f}, {7, 1.9f}};
        int col = 0, startRow = 0;
        auto trigger = [&](Role role, int id, int group, int target) {
            Obj o{role, id, 75.f + 30.f * static_cast<float>(col / 4), triggerY + 30.f * static_cast<float>(col % 4)};
            ++col;
            o.group = group;
            o.target = target;
            o.spawnTriggered = group != 0;
            return o;
        };
        auto starter = [&](int target) {
            Obj o{Role::SpawnTrigger, 1268, 45.f, triggerY + 30.f * static_cast<float>(startRow++)};
            o.target = target;
            out.push_back(o);
        };
        for (int k = 0; k < 3; ++k) {
            if (!floatUsed[k]) continue;
            const int target = p.animGroups[k], up = p.animGroups[3 + k], down = p.animGroups[6 + k];
            const float half = floats[k].period * 0.5f;
            for (int dir : {1, -1}) {
                const int self = dir > 0 ? up : down, next = dir > 0 ? down : up;
                Obj mv = trigger(Role::MoveTrigger, 901, self, target);
                mv.moveY = dir * floats[k].amp;
                mv.duration = half;
                mv.easing = 1; // ease in out
                out.push_back(mv);
                Obj sp = trigger(Role::SpawnTrigger, 1268, self, next);
                sp.delay = half;
                out.push_back(sp);
            }
            starter(up);
        }
        const float spins[2][2] = {{360.f, 9.f}, {-360.f, 13.f}};
        for (int k = 0; k < 2; ++k) {
            if (!spinUsed[k]) continue;
            const int target = p.animGroups[9 + k], loop = p.animGroups[11 + k];
            Obj rt = trigger(Role::RotateTrigger, 1346, loop, target);
            rt.degrees = static_cast<int>(spins[k][0]);
            rt.duration = spins[k][1];
            out.push_back(rt);
            Obj sp = trigger(Role::SpawnTrigger, 1268, loop, loop);
            sp.delay = spins[k][1];
            out.push_back(sp);
            starter(loop);
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
        if (style.pulseInner && p.inner && !style.inner.empty()) {
            targets.push_back({ch.inner, lighten(mix(pal.blocks, pal.primary, style.innerMix), style.pulseStrength * 0.6f), true});
        }
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
                Obj o{Role::PulseTrigger, 1006, x, triggerY + 30.f * static_cast<float>(12 + row++)};
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
    s.reserve(objs.size() * 90);
    char buf[64];
    auto add = [&](int key, const char* fmt, auto value) {
        int n = std::snprintf(buf, sizeof buf, fmt, value);
        if (!s.empty() && s.back() != ';') s += ',';
        s += std::to_string(key);
        s += ',';
        if (n > 0) s.append(buf, static_cast<size_t>(std::min<int>(n, sizeof buf - 1)));
    };
    auto head = [&](const Obj& o, int id) {
        add(1, "%d", id);
        add(2, "%.2f", o.x);
        add(3, "%.2f", o.y - 90.f);
    };
    auto spawnFlags = [&](const Obj& o) {
        if (o.spawnTriggered) {
            add(62, "%d", 1);
            add(87, "%d", 1); // multi trigger: loops fire again and again
        }
    };
    for (const auto& o : objs) {
        switch (o.role) {
            case Role::ColorTrigger:
                head(o, 899);
                add(7, "%d", o.rgb.r);
                add(8, "%d", o.rgb.g);
                add(9, "%d", o.rgb.b);
                add(10, "%d", 0);
                add(23, "%d", o.target);
                add(35, "%.2f", o.opacity);
                break;
            case Role::PulseTrigger:
                head(o, 1006);
                add(7, "%d", o.rgb.r);
                add(8, "%d", o.rgb.g);
                add(9, "%d", o.rgb.b);
                add(45, "%d", 0);
                add(46, "%d", 0);
                add(47, "%.3f", o.fadeOut);
                add(48, "%d", 0);
                add(51, "%d", o.target);
                add(52, "%d", 0);
                break;
            case Role::MoveTrigger:
                head(o, 901);
                add(10, "%.2f", o.duration);
                add(28, "%d", o.moveX);
                add(29, "%d", o.moveY);
                add(30, "%d", o.easing);
                if (o.easing != 0) add(85, "%.2f", 2.f);
                add(51, "%d", o.target);
                spawnFlags(o);
                break;
            case Role::RotateTrigger:
                head(o, 1346);
                add(10, "%.2f", o.duration);
                add(30, "%d", o.easing);
                add(51, "%d", o.target);
                add(68, "%d", o.degrees);
                spawnFlags(o);
                break;
            case Role::SpawnTrigger:
                head(o, 1268);
                add(51, "%d", o.target);
                if (o.delay > 0.f) add(63, "%.2f", o.delay);
                spawnFlags(o);
                break;
            default: {
                head(o, o.id);
                const float rot = std::fabs(o.rotation) < 0.005f ? 0.f : o.rotation;
                if (rot != 0.f) {
                    if (std::fabs(rot - std::round(rot)) < 0.005f) add(6, "%d", static_cast<int>(std::lround(rot)));
                    else add(6, "%.2f", rot);
                }
                add(21, "%d", o.color);
                add(22, "%d", o.color); // objects coloured through their detail part
                add(24, "%d", o.zLayer);
                if (o.zOrder != 0) add(25, "%d", o.zOrder);
                if (std::fabs(o.scaleX - o.scaleY) < 0.0005f) {
                    if (std::fabs(o.scaleX - 1.f) > 0.0005f) add(32, "%.3f", o.scaleX);
                } else {
                    add(128, "%.3f", o.scaleX);
                    add(129, "%.3f", o.scaleY);
                }
                if (noTouch) add(121, "%d", 1);
                break;
            }
        }
        if (editorLayer != 0) add(20, "%d", editorLayer);
        if (group > 0 && o.group > 0) {
            std::snprintf(buf, sizeof buf, "%d.%d", group, o.group);
            if (!s.empty() && s.back() != ';') s += ',';
            s += "57,";
            s += buf;
        } else if (group > 0) {
            add(57, "%d", group);
        } else if (o.group > 0) {
            add(57, "%d", o.group);
        }
        s += ';';
    }
    return s;
}

struct Counts {
    size_t fill = 0, outline = 0, glow = 0, inner = 0, details = 0, structures = 0, background = 0, animated = 0, triggers = 0;
    size_t total() const { return fill + outline + glow + inner + details + structures + background + triggers; }
};

inline Counts count(const std::vector<Obj>& objs) {
    Counts c;
    for (const auto& o : objs) {
        switch (o.role) {
            case Role::Fill: ++c.fill; break;
            case Role::Outline: ++c.outline; break;
            case Role::Glow: ++c.glow; break;
            case Role::Inner: ++c.inner; break;
            case Role::Top: case Role::Hang: case Role::Side: case Role::Ground: ++c.details; break;
            case Role::Structure: ++c.structures; break;
            case Role::Background: ++c.background; break;
            default: ++c.triggers; break;
        }
        if (!isTrigger(o.role) && o.group > 0) ++c.animated;
    }
    return c;
}

} // namespace deco
