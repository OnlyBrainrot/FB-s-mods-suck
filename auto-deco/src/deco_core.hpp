// Auto Deco - geometry core.
//
// Everything in this file is plain C++ with no game or Geode dependency, so the
// placement logic can be unit-tested on any machine (see test/core_test.cpp).
//
// The idea: the mod never hard-codes which way a decoration sprite faces. It
// renders each piece once, measures where its "ink" is relative to the object's
// position (Shape), works out which side or corner is meant to touch the block
// (EdgeAnchor / CornerAnchor), and then rotates and offsets the piece so that
// side or corner lands exactly on the block edge. That makes any outline or
// glow set usable, including custom object IDs.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace deco {

constexpr float CELL = 30.f;
constexpr float HALF = CELL / 2.f;

struct Vec {
    float x = 0.f;
    float y = 0.f;
};

// Sides in clockwise order, so that "rotate by k * 90 degrees clockwise" moves
// side s to side (s + k) % 4. Same for corners.
enum Side : int { Up = 0, Right = 1, Down = 2, Left = 3 };
enum Corner : int { TopRight = 0, BottomRight = 1, BottomLeft = 2, TopLeft = 3 };

constexpr int SIDE_CELL[4][2] = {{0, 1}, {1, 0}, {0, -1}, {-1, 0}};
constexpr int CORNER_CELL[4][2] = {{1, 1}, {1, -1}, {-1, -1}, {-1, 1}};

inline Vec rotateCW(Vec v, int steps) {
    steps = ((steps % 4) + 4) % 4;
    for (int i = 0; i < steps; ++i) v = Vec{v.y, -v.x};
    return v;
}

inline int mod4(int v) {
    return ((v % 4) + 4) % 4;
}

// ---------------------------------------------------------------------------
// Sprite measurement
// ---------------------------------------------------------------------------

// Where a piece's visible pixels are, in points, relative to the object position.
struct Shape {
    bool valid = false;
    float minX = 0.f, maxX = 0.f, minY = 0.f, maxY = 0.f; // ink bounds
    float cx = 0.f, cy = 0.f;                              // ink centroid
};

// rgba: w*h RGBA8 pixels, row 0 = BOTTOM row. The bitmap covers pointsW x pointsH
// points and the object sits at its centre.
inline Shape measure(const std::uint8_t* rgba, int w, int h, float pointsW, float pointsH) {
    Shape s;
    if (!rgba || w <= 0 || h <= 0) return s;

    const float sx = pointsW / static_cast<float>(w);
    const float sy = pointsH / static_cast<float>(h);
    const int boundsThreshold = 26; // ~10%: ignores the faint tail of gradients

    double sum = 0.0, sumX = 0.0, sumY = 0.0;
    int minPX = w, maxPX = -1, minPY = h, maxPY = -1;

    for (int y = 0; y < h; ++y) {
        const std::uint8_t* row = rgba + static_cast<std::size_t>(y) * w * 4;
        for (int x = 0; x < w; ++x) {
            const std::uint8_t* p = row + x * 4;
            int weight = std::max(std::max<int>(p[0], p[1]), std::max<int>(p[2], p[3]));
            if (weight == 0) continue;
            sum += weight;
            sumX += weight * (x + 0.5);
            sumY += weight * (y + 0.5);
            if (weight >= boundsThreshold) {
                minPX = std::min(minPX, x);
                maxPX = std::max(maxPX, x);
                minPY = std::min(minPY, y);
                maxPY = std::max(maxPY, y);
            }
        }
    }

    if (sum <= 0.0 || maxPX < minPX || maxPY < minPY) return s;

    s.valid = true;
    s.minX = minPX * sx - pointsW / 2.f;
    s.maxX = (maxPX + 1) * sx - pointsW / 2.f;
    s.minY = minPY * sy - pointsH / 2.f;
    s.maxY = (maxPY + 1) * sy - pointsH / 2.f;
    s.cx = static_cast<float>(sumX / sum) * sx - pointsW / 2.f;
    s.cy = static_cast<float>(sumY / sum) * sy - pointsH / 2.f;
    return s;
}

// A straight piece: which side touches the block edge at rotation 0, and how far
// that side is from the object position.
struct EdgeAnchor {
    int side = Up;
    float dist = HALF;
};

// A corner piece: which corner touches the block corner at rotation 0, and the
// vector from the object position to that corner.
struct CornerAnchor {
    int corner = TopRight;
    Vec vec{HALF, HALF};
};

inline EdgeAnchor edgeAnchor(const Shape& s, EdgeAnchor fallback) {
    if (!s.valid) return fallback;

    const float hx = (s.maxX - s.minX) / 2.f;
    const float hy = (s.maxY - s.minY) / 2.f;
    const float bx = (s.minX + s.maxX) / 2.f;
    const float by = (s.minY + s.maxY) / 2.f;
    EdgeAnchor a;

    if (std::min(hx, hy) < 0.35f * std::max(hx, hy)) {
        // A thin bar (outline line). It hugs whichever side of the cell it sits in.
        if (hy < hx) {
            a.side = by >= -0.5f ? Up : Down;
        } else {
            a.side = bx >= -0.5f ? Right : Left;
        }
    } else {
        // A gradient (glow). The bright side is where the centroid is pulled to.
        const float dx = (s.cx - bx) / std::max(hx, 0.01f);
        const float dy = (s.cy - by) / std::max(hy, 0.01f);
        if (std::fabs(dx) < 0.03f && std::fabs(dy) < 0.03f) {
            a.side = fallback.side; // symmetric blob: nothing to detect
        } else if (std::fabs(dy) >= std::fabs(dx)) {
            a.side = dy >= 0.f ? Up : Down;
        } else {
            a.side = dx >= 0.f ? Right : Left;
        }
    }

    switch (a.side) {
        case Up: a.dist = s.maxY; break;
        case Right: a.dist = s.maxX; break;
        case Down: a.dist = -s.minY; break;
        default: a.dist = -s.minX; break;
    }
    return a;
}

inline CornerAnchor cornerAnchor(const Shape& s, CornerAnchor fallback) {
    if (!s.valid) return fallback;

    const float hx = (s.maxX - s.minX) / 2.f;
    const float hy = (s.maxY - s.minY) / 2.f;
    const float bx = (s.minX + s.maxX) / 2.f;
    const float by = (s.minY + s.maxY) / 2.f;

    int sx = 1, sy = 1;
    if (hx < 6.f && hy < 6.f) {
        // A small dot (outline inner corner): it sits in the corner it belongs to.
        if (std::fabs(bx) < 0.5f && std::fabs(by) < 0.5f) {
            sx = CORNER_CELL[fallback.corner][0];
            sy = CORNER_CELL[fallback.corner][1];
        } else {
            sx = bx >= 0.f ? 1 : -1;
            sy = by >= 0.f ? 1 : -1;
        }
    } else {
        const float dx = (s.cx - bx) / std::max(hx, 0.01f);
        const float dy = (s.cy - by) / std::max(hy, 0.01f);
        if (std::fabs(dx) < 0.03f && std::fabs(dy) < 0.03f) {
            sx = CORNER_CELL[fallback.corner][0];
            sy = CORNER_CELL[fallback.corner][1];
        } else {
            sx = dx >= 0.f ? 1 : -1;
            sy = dy >= 0.f ? 1 : -1;
        }
    }

    CornerAnchor a;
    for (int c = 0; c < 4; ++c) {
        if (CORNER_CELL[c][0] == sx && CORNER_CELL[c][1] == sy) a.corner = c;
    }
    a.vec = Vec{sx > 0 ? s.maxX : s.minX, sy > 0 ? s.maxY : s.minY};
    return a;
}

// ---------------------------------------------------------------------------
// Grid
// ---------------------------------------------------------------------------

struct Rect {
    float minX = 0.f, minY = 0.f, maxX = 0.f, maxY = 0.f;
};

using Cell = std::pair<int, int>;
using CellSet = std::set<Cell>;

inline float positiveMod(float v, float m) {
    float r = std::fmod(v, m);
    return r < 0.f ? r + m : r;
}

// Blocks don't have to sit on the editor's default grid: the grid phase is taken
// from the most common block alignment in the selection.
inline Vec gridOrigin(const std::vector<Rect>& rects) {
    std::map<int, int> histX, histY;
    for (const auto& r : rects) {
        // quantise to half units; 60 half-units wrap around to 0
        histX[static_cast<int>(std::lround(positiveMod(r.minX, CELL) * 2.f)) % 60]++;
        histY[static_cast<int>(std::lround(positiveMod(r.minY, CELL) * 2.f)) % 60]++;
    }
    auto best = [](const std::map<int, int>& hist) {
        int key = 0, count = -1;
        for (const auto& [k, c] : hist) {
            if (c > count) {
                key = k;
                count = c;
            }
        }
        return key / 2.f;
    };
    return Vec{best(histX), best(histY)};
}

// Every grid cell that is completely covered by one of the rects.
inline CellSet cellsFromRects(const std::vector<Rect>& rects, Vec origin, std::size_t maxCells = 200000) {
    CellSet cells;
    const float tol = 1.f;
    for (const auto& r : rects) {
        const int i0 = static_cast<int>(std::ceil((r.minX - origin.x - tol) / CELL));
        const int i1 = static_cast<int>(std::floor((r.maxX - origin.x + tol) / CELL)) - 1;
        const int j0 = static_cast<int>(std::ceil((r.minY - origin.y - tol) / CELL));
        const int j1 = static_cast<int>(std::floor((r.maxY - origin.y + tol) / CELL)) - 1;
        for (int i = i0; i <= i1; ++i) {
            for (int j = j0; j <= j1; ++j) {
                cells.insert({i, j});
                if (cells.size() >= maxCells) return cells;
            }
        }
    }
    return cells;
}

// ---------------------------------------------------------------------------
// Generation
// ---------------------------------------------------------------------------

enum class Piece : int { Fill, OutlineEdge, OutlineInner, GlowEdge, GlowOuter, GlowInner };

struct Placed {
    Piece piece;
    float x;
    float y;
    int rotation; // degrees clockwise, 0 / 90 / 180 / 270
};

struct Options {
    bool fill = true;
    bool outline = true;
    bool glow = true;
};

struct Anchors {
    EdgeAnchor outlineEdge;
    CornerAnchor outlineInner;
    EdgeAnchor glowEdge;
    CornerAnchor glowOuter;
    CornerAnchor glowInner;
};

inline Vec cellCentre(Cell c, Vec origin) {
    return Vec{origin.x + c.first * CELL + HALF, origin.y + c.second * CELL + HALF};
}

// Put an edge piece so its anchor side lies on the edge whose midpoint is `mid`,
// facing direction `side` (as seen from the piece).
inline Placed placeEdge(Piece piece, const EdgeAnchor& a, Vec mid, int side) {
    const int k = mod4(side - a.side);
    return Placed{
        piece,
        mid.x - a.dist * SIDE_CELL[side][0],
        mid.y - a.dist * SIDE_CELL[side][1],
        k * 90,
    };
}

// Put a corner piece so its anchor corner lies on `point`, facing `corner`
// (as seen from the piece).
inline Placed placeCorner(Piece piece, const CornerAnchor& a, Vec point, int corner) {
    const int k = mod4(corner - a.corner);
    const Vec v = rotateCW(a.vec, k);
    return Placed{piece, point.x - v.x, point.y - v.y, k * 90};
}

inline std::vector<Placed> generate(const CellSet& solid, Vec origin, const Options& opt, const Anchors& anchors) {
    std::vector<Placed> out;
    auto has = [&](int i, int j) { return solid.count({i, j}) != 0; };

    if (opt.fill) {
        for (const auto& c : solid) {
            const Vec p = cellCentre(c, origin);
            out.push_back(Placed{Piece::Fill, p.x, p.y, 0});
        }
    }

    if (opt.outline) {
        for (const auto& c : solid) {
            const Vec p = cellCentre(c, origin);
            // a line on every side that faces air
            for (int s = 0; s < 4; ++s) {
                if (has(c.first + SIDE_CELL[s][0], c.second + SIDE_CELL[s][1])) continue;
                const Vec mid{p.x + HALF * SIDE_CELL[s][0], p.y + HALF * SIDE_CELL[s][1]};
                out.push_back(placeEdge(Piece::OutlineEdge, anchors.outlineEdge, mid, s));
            }
            // a dot wherever two lines from neighbouring blocks leave a notch
            for (int q = 0; q < 4; ++q) {
                const int qx = CORNER_CELL[q][0], qy = CORNER_CELL[q][1];
                if (!has(c.first + qx, c.second) || !has(c.first, c.second + qy)) continue;
                if (has(c.first + qx, c.second + qy)) continue;
                const Vec corner{p.x + HALF * qx, p.y + HALF * qy};
                out.push_back(placeCorner(Piece::OutlineInner, anchors.outlineInner, corner, q));
            }
        }
    }

    if (opt.glow) {
        // every air cell touching a solid cell (including diagonally)
        CellSet air;
        for (const auto& c : solid) {
            for (int di = -1; di <= 1; ++di) {
                for (int dj = -1; dj <= 1; ++dj) {
                    if (!has(c.first + di, c.second + dj)) air.insert({c.first + di, c.second + dj});
                }
            }
        }

        for (const auto& c : air) {
            const Vec p = cellCentre(c, origin);
            bool orth[4];
            int count = 0;
            for (int s = 0; s < 4; ++s) {
                orth[s] = has(c.first + SIDE_CELL[s][0], c.second + SIDE_CELL[s][1]);
                if (orth[s]) ++count;
            }

            if (count == 0) {
                // only touching a block corner: rounded outer-corner glow
                for (int q = 0; q < 4; ++q) {
                    const int qx = CORNER_CELL[q][0], qy = CORNER_CELL[q][1];
                    if (!has(c.first + qx, c.second + qy)) continue;
                    const Vec corner{p.x + HALF * qx, p.y + HALF * qy};
                    out.push_back(placeCorner(Piece::GlowOuter, anchors.glowOuter, corner, q));
                }
                continue;
            }

            if (count == 2) {
                // tucked into an L: one inner-corner glow instead of two overlapping edges
                bool placed = false;
                for (int s = 0; s < 4 && !placed; ++s) {
                    if (orth[s] && orth[(s + 1) % 4]) {
                        const int q = s; // sides (s, s+1) meet at corner s
                        const Vec corner{p.x + HALF * CORNER_CELL[q][0], p.y + HALF * CORNER_CELL[q][1]};
                        out.push_back(placeCorner(Piece::GlowInner, anchors.glowInner, corner, q));
                        placed = true;
                    }
                }
                if (placed) continue;
            }

            for (int s = 0; s < 4; ++s) {
                if (!orth[s]) continue;
                const Vec mid{p.x + HALF * SIDE_CELL[s][0], p.y + HALF * SIDE_CELL[s][1]};
                out.push_back(placeEdge(Piece::GlowEdge, anchors.glowEdge, mid, s));
            }
        }
    }

    return out;
}

} // namespace deco
