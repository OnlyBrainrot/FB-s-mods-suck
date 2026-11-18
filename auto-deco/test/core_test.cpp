// Host-side test for the placement core. No game needed:
//   c++ -std=c++20 -I src test/core_test.cpp -o core_test && ./core_test
#include "deco_core.hpp"

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <vector>

using namespace deco;

static int failures = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);          \
            ++failures;                                                  \
        }                                                                \
    } while (0)

static bool near(float a, float b, float eps = 0.6f) {
    return std::fabs(a - b) <= eps;
}

// Draw a synthetic sprite: 64x64 points at 2 px per point, object at the centre.
// fn(x, y) gets point coordinates relative to the object and returns alpha 0..255.
static Shape draw(const std::function<int(float, float)>& fn) {
    const int W = 128, H = 128;
    std::vector<std::uint8_t> px(W * H * 4, 0);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const float fx = (x + 0.5f) / 2.f - 32.f;
            const float fy = (y + 0.5f) / 2.f - 32.f;
            const int a = fn(fx, fy);
            auto* p = &px[(y * W + x) * 4];
            p[0] = p[1] = p[2] = p[3] = static_cast<std::uint8_t>(std::clamp(a, 0, 255));
        }
    }
    return measure(px.data(), W, H, 64.f, 64.f);
}

static bool inCell(float x, float y) {
    return x >= -15 && x < 15 && y >= -15 && y < 15;
}

static int countAt(const std::vector<Placed>& v, Piece piece, float x, float y, int rot, float eps = 0.6f) {
    int n = 0;
    for (const auto& p : v) {
        if (p.piece == piece && near(p.x, x, eps) && near(p.y, y, eps) && p.rotation == rot) ++n;
    }
    return n;
}

static int countOf(const std::vector<Placed>& v, Piece piece) {
    int n = 0;
    for (const auto& p : v) n += p.piece == piece;
    return n;
}

int main() {
    // --- measurement ---------------------------------------------------------
    // outline line drawn along the top of a 30x30 cell
    Shape lineTop = draw([](float x, float y) { return inCell(x, y) && y >= 13.5f ? 255 : 0; });
    EdgeAnchor aTop = edgeAnchor(lineTop, {Down, 0});
    CHECK(aTop.side == Up);
    CHECK(near(aTop.dist, 15));

    // same line, but the sprite is drawn along the left of the cell
    Shape lineLeft = draw([](float x, float y) { return inCell(x, y) && x < -13.5f ? 255 : 0; });
    EdgeAnchor aLeft = edgeAnchor(lineLeft, {Up, 0});
    CHECK(aLeft.side == Left);
    CHECK(near(aLeft.dist, 15));

    // a line sprite that is only the line, centred on the object
    Shape lineOnly = draw([](float x, float y) { return std::fabs(x) < 15 && std::fabs(y) < 0.75f ? 255 : 0; });
    EdgeAnchor aOnly = edgeAnchor(lineOnly, {Down, 0});
    CHECK(aOnly.side == Up);
    CHECK(near(aOnly.dist, 0.75f));

    // glow gradient, brightest along the bottom of the cell
    Shape glowBottom = draw([](float x, float y) {
        return inCell(x, y) ? static_cast<int>(255 * (1.f - (y + 15) / 30.f)) : 0;
    });
    EdgeAnchor gBottom = edgeAnchor(glowBottom, {Up, 0});
    CHECK(gBottom.side == Down);
    CHECK(near(gBottom.dist, 15));

    // glow gradient, brightest along the right, only 15 wide
    Shape glowRight = draw([](float x, float y) {
        return (x >= 0 && x < 15 && std::fabs(y) < 15) ? static_cast<int>(255 * (x / 15.f)) : 0;
    });
    EdgeAnchor gRight = edgeAnchor(glowRight, {Up, 0});
    CHECK(gRight.side == Right);
    CHECK(near(gRight.dist, 15));

    // outer-corner glow, brightest in the bottom-left corner
    Shape cornerBL = draw([](float x, float y) {
        if (!inCell(x, y)) return 0;
        const float d = std::sqrt((x + 15) * (x + 15) + (y + 15) * (y + 15));
        return static_cast<int>(255 * std::max(0.f, 1.f - d / 30.f));
    });
    CornerAnchor cBL = cornerAnchor(cornerBL, {TopRight, {15, 15}});
    CHECK(cBL.corner == BottomLeft);
    CHECK(near(cBL.vec.x, -15) && near(cBL.vec.y, -15));

    // inner-corner glow: bright along the top and right edges
    Shape innerTR = draw([](float x, float y) {
        if (!inCell(x, y)) return 0;
        const float d = std::min(15 - x, 15 - y);
        return static_cast<int>(255 * std::max(0.f, 1.f - d / 30.f));
    });
    CornerAnchor cTR = cornerAnchor(innerTR, {BottomLeft, {-15, -15}});
    CHECK(cTR.corner == TopRight);
    CHECK(near(cTR.vec.x, 15) && near(cTR.vec.y, 15));

    // outline inner-corner dot in the top-left of the cell
    Shape dotTL = draw([](float x, float y) { return (x >= -15 && x < -13.5f && y >= 13.5f && y < 15) ? 255 : 0; });
    CornerAnchor cDot = cornerAnchor(dotTL, {BottomRight, {15, -15}});
    CHECK(cDot.corner == TopLeft);
    CHECK(near(cDot.vec.x, -15) && near(cDot.vec.y, 15));

    // nothing drawn -> fallback
    Shape empty = draw([](float, float) { return 0; });
    CHECK(!empty.valid);
    CHECK(edgeAnchor(empty, {Left, 7}).side == Left);

    // --- rotation helper -----------------------------------------------------
    Vec up{0, 1};
    CHECK(near(rotateCW(up, 1).x, 1) && near(rotateCW(up, 1).y, 0));
    CHECK(near(rotateCW(up, 2).y, -1));
    CHECK(near(rotateCW(up, -1).x, -1));

    // --- grid ----------------------------------------------------------------
    // three blocks in an L, in editor coordinates:  A(0,0) B(1,0) C(1,1)
    std::vector<Rect> rects = {
        {0, 90, 30, 120},
        {30, 90, 60, 120},
        {30, 120, 60, 150},
        {200, 97, 230, 111}, // a slab: does not fill a cell, must be ignored
    };
    Vec origin = gridOrigin(rects);
    CHECK(near(origin.x, 0) && near(origin.y, 0)); // 90 is a multiple of 30
    CellSet cells = cellsFromRects(rects, origin);
    CHECK(cells.size() == 3);
    CHECK(cells.count({0, 3}) && cells.count({1, 3}) && cells.count({1, 4}));

    // off-grid selection keeps its own phase
    std::vector<Rect> shifted = {{7, 95, 37, 125}, {37, 95, 67, 125}};
    Vec so = gridOrigin(shifted);
    CHECK(near(so.x, 7) && near(so.y, 5));
    CHECK(cellsFromRects(shifted, so).size() == 2);

    // a 2x scaled block covers four cells
    CHECK(cellsFromRects({{0, 0, 60, 60}}, {0, 0}).size() == 4);

    // --- generation with "sprites drawn the awkward way round" ----------------
    Anchors anchors;
    anchors.outlineEdge = aLeft;   // line sprite drawn on the left
    anchors.outlineInner = cDot;   // dot drawn top-left
    anchors.glowEdge = gBottom;    // glow bright at the bottom
    anchors.glowOuter = cBL;       // corner glow bright bottom-left
    anchors.glowInner = cTR;       // inner glow bright top-right

    auto placed = generate(cells, origin, Options{}, anchors);

    CHECK(countOf(placed, Piece::Fill) == 3);
    CHECK(countAt(placed, Piece::Fill, 15, 105, 0) == 1);

    // L of 3 cells has 8 exposed edges
    CHECK(countOf(placed, Piece::OutlineEdge) == 8);
    // A's top edge: sprite's line is on the Left, needs to face Up -> 90 cw.
    // Full-cell sprite, so it stays centred on the block.
    CHECK(countAt(placed, Piece::OutlineEdge, 15, 105, 90) == 1);
    // A's left edge: already facing Left
    CHECK(countAt(placed, Piece::OutlineEdge, 15, 105, 0) == 1);
    // A's bottom edge: Left -> Down is 270 cw
    CHECK(countAt(placed, Piece::OutlineEdge, 15, 105, 270) == 1);
    // C's right edge: Left -> Right is 180
    CHECK(countAt(placed, Piece::OutlineEdge, 45, 135, 180) == 1);

    // one notch: in B's top-left corner. Dot is drawn top-left -> no rotation.
    CHECK(countOf(placed, Piece::OutlineInner) == 1);
    CHECK(countAt(placed, Piece::OutlineInner, 45, 105, 0) == 1);

    // glow: the air cell above A is tucked into the L (solid below and to the right)
    CHECK(countOf(placed, Piece::GlowInner) == 1);
    // corner point is that cell's bottom-right = (30,120); sprite is bright top-right,
    // must face bottom-right -> 90 cw; the piece ends up centred in the air cell.
    CHECK(countAt(placed, Piece::GlowInner, 15, 135, 90) == 1);

    // straight glow under A: air cell (0,2), block above it. Sprite bright at the
    // bottom must face Up -> 180; centred in the air cell.
    CHECK(countAt(placed, Piece::GlowEdge, 15, 75, 180) == 1);
    // straight glow left of A: bright side must face Right -> 270 cw
    CHECK(countAt(placed, Piece::GlowEdge, -15, 105, 270) == 1);
    // 6 straight pieces: under A, under B, left of A, right of B, right of C, above C
    CHECK(countOf(placed, Piece::GlowEdge) == 6);

    // outer corners: bottom-left of A, top-left of A, bottom-right of B,
    // top-right of C, top-left of C
    CHECK(countOf(placed, Piece::GlowOuter) == 5);
    // below-left of A: block corner at (0,90) is this air cell's top-right corner.
    // Sprite bright bottom-left must face top-right -> 180.
    CHECK(countAt(placed, Piece::GlowOuter, -15, 75, 180) == 1);

    // --- a thin line sprite is pulled to the edge rather than centred ---------
    Anchors thin = anchors;
    thin.outlineEdge = aOnly; // thin line centred on the object, anchored Up
    auto placed2 = generate(CellSet{{0, 0}}, Vec{0, 0}, Options{false, true, false}, thin);
    CHECK(countOf(placed2, Piece::OutlineEdge) == 4);
    CHECK(countAt(placed2, Piece::OutlineEdge, 15, 29.25f, 0, 0.3f) == 1);   // top
    CHECK(countAt(placed2, Piece::OutlineEdge, 29.25f, 15, 90, 0.3f) == 1);  // right
    CHECK(countAt(placed2, Piece::OutlineEdge, 15, 0.75f, 180, 0.3f) == 1);  // bottom
    CHECK(countAt(placed2, Piece::OutlineEdge, 0.75f, 15, 270, 0.3f) == 1);  // left

    if (failures == 0) std::printf("all core tests passed\n");
    return failures == 0 ? 0 : 1;
}
