#pragma once
// ============================================================================
// src/ext/leaf_lod/leaf_lod_build.h -- ext.leaf_lod's canopy reduction, the pure part
// (docs/extensions/LEAF_LOD.md)
// ============================================================================
// NOT RETAIL. No engine types: the host unit test (tests/unit/ext_leaf_lod_test.cpp) runs all of it.
// leaf_lod.cpp feeds it a tree mesh's own vertex and index bytes, read back from the installed mesh at
// run time, and uploads what it builds. Nothing here is game content.
//
// THE VERTEX. The rigid mesh vertex is 24 bytes: the position as three int16 (/ 512), and for a billboard
// canopy the last four bytes are two int16 corner weights (the card's corner in card units, / 8192), which
// the billboard vertex program turns into the camera-facing offset. A vertex with both weights 0 is not a
// leaf card's (the trunk, the branches).
//
// THE CARDS. A triangle whose three vertices are leaf vertices belongs to a card; the cards of a list are
// the connected sets of such triangles.
//
// THE ORDER. The cards of a list are put in keep order once, by taking them away one at a time: always the
// card that costs least. The cost is how far the crown's outline has moved in without it, over 200
// directions (the root mean square of the moves, as shares of the crown's size, a direction counting 1
// straight up, 0.7 level and 0.1 straight down: the camera looks down on a crown) plus a little for the
// card's distance to its nearest neighbour left. So a card that only doubles a neighbour goes first and
// the cards that draw the outline stay to the end. Each step also records the largest single move so far
// (Order::giveUp): what the crown's outline has given up once that many cards are gone.
//
// THE CAP. A list stops losing cards where giveUp passes Shape::outline, whatever Shape::keepLast asks: a
// crown whose cards fill it (the birches) can lose more of them than one whose cards sit on a dome (the
// oak) before its shape reads differently.
//
// THE LEVELS. Progress 0 is the mesh's own canopy; at progress 1 the cards the share and the cap allow are
// gone. Between, a card does not vanish at once: it shrinks over kWindow cards' worth of progress. With
// many levels over the zoom range a card loses a few percent of its size per level, so a zoom reads as
// leaves drawing in, with no step to see and no second canopy drawn over the first. The cards that stay
// grow by Shape::grow (1 = the crown's total card area is kept).
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Ext {
namespace LeafLod {

constexpr int   kStride   = 24;     // the rigid mesh vertex, bytes
constexpr int   kCornerAt = 20;     // the two int16 corner weights' offset in it
constexpr float kWindow   = 4.0f;   // cards' worth of progress a leaving card shrinks over
constexpr float kGone     = 0.04f;  // a card under this share of its size is not drawn

// One triangle list of the level: DrawIndexed's four numbers (an index is relative to baseVertex).
struct List {
    int32_t baseVertex  = 0;
    int32_t vertexCount = 0;
    int32_t startIndex  = 0;
    int32_t indexCount  = 0;
    bool    canopy      = false;   // drawn with a billboard material: its leaf cards are reduced
};

struct Shape {
    float keepLast = 0.5f;   // the share of a list's cards left at progress 1
    float grow     = 0.0f;   // how much the staying cards grow: 1 keeps the total card area
    float outline  = 0.025f; // THE CAP: the outline move (a share of the crown's size) no list goes past
};

// One card for the order: its centre (any unit) and a tie-breaking id.
struct CardSite {
    float centre[3];
    uint32_t id;
};
// The keep order of `cards` (THE ORDER above); `radius` is a card's half size in the centres' unit.
struct Order {
    std::vector<int32_t> order;    // indices into `cards`, the card kept longest first
    std::vector<float>   giveUp;   // by cards gone - 1: the outline's largest move, as a share of the crown's size
};
Order KeepOrder(const std::vector<CardSite>& cards, float radius);
// How many of a list's `count` cards are gone at progress 1: the share's, capped by THE CAP.
float LeavingOf(const std::vector<float>& giveUp, int32_t count, float keepLast, float tolerance);

// What a mesh's geometry holds, worked out once: per list, its cards and their places in the keep order.
struct ListPlan {
    bool    copy  = true;    // not reduced: copied as it is (not a canopy, or its vertices out of range)
    bool    empty = false;   // its indices are out of the buffers' range: nothing drawn
    int32_t span  = 0;       // the list's vertices: 0 .. span - 1 past baseVertex
    int32_t cards = 0;
    std::vector<int32_t> cardOf;         // by vertex (local): its card, -1 = not a leaf card's
    std::vector<int32_t> triangleCard;   // by triangle: its card, -1 = not a card's
    std::vector<int32_t> place;          // by card: its place in the keep order (0 = kept longest)
    std::vector<float>   giveUp;         // Order::giveUp
};
struct Plan {
    std::vector<List>     lists;
    std::vector<ListPlan> plans;
    int cards = 0;   // leaf cards in every reduced list
};
Plan Analyse(const uint8_t* vertices, size_t vertexBytes, const uint16_t* indices, size_t indexCount,
             const std::vector<List>& lists);

struct Reduced {
    std::vector<uint8_t>  vertices;   // the level's whole vertex buffer, the leaf corners scaled
    std::vector<uint16_t> indices;    // every list's indices, one after another
    std::vector<List>     lists;      // the same lists, startIndex / indexCount into `indices`
    int drawn = 0;                    // leaf cards still drawn (shrinking ones included)
};

// The size share (0..1) of the card at place `place` of a keep order of `count` cards at `progress`, when
// `leaving` (LeavingOf) of them are gone at progress 1.
float ShareAt(int32_t place, int32_t count, float progress, float leaving);

// The canopy at `progress` (0..1), from the buffers `plan` was made from.
Reduced Reduce(const Plan& plan, const uint8_t* vertices, size_t vertexBytes, const uint16_t* indices,
               float progress, const Shape& shape);

// The level (0 = the mesh's own canopy, `levels` = progress 1) at camera distance `distance`.
int LevelAt(float distance, float start, float end, int levels);

}  // namespace LeafLod
}  // namespace Ext
