// src/ext/family_tree/family_picture.h -- the whole-town family tree as one picture (an SVG file)
//
// NOT RETAIL. Docs: docs/extensions/handins/FAMILYTREE.md section 4c. Standard C++ (no UI): the layout is
// arithmetic a unit test can check, the picture is text.
//
// THE LAYOUT. Rows are generations (a founder is generation 0; a child is one below its parents; the two
// people of a marriage always share a row). Spouses sit side by side as a cluster (a person with several
// marriages is a chain of them). The clusters form a forest: each cluster hangs under the first of its
// families the walk reaches, under which its own children's clusters hang in turn, and a cluster is centered
// over its children. Two lineages that marry join at the cluster of the marriage; the second family's line to
// it is simply longer.
//
// THE LINES. One family (the parents, the children) has one color, from the parents' marriage line down to
// its bus and the drops to the children, so where lines of different families cross or run side by side the
// colors say which belongs to which. Colors go round a palette in the order the families stand on the page,
// so neighbors differ. A marriage is a short dark line between the two boxes (an arc over the boxes in
// between when a person with several marriages has them not side by side).
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "ext/family_tree/family_tree.h"

namespace Ext {
namespace FamilyTree {

// more are not drawn (everyone falls back to the living and their ancestors first)
constexpr int kPictureMaxPeople = 6000;
constexpr int kPictureBoxHeight = 58;
constexpr int kPictureRowGap = 76;        // clear space between two rows of boxes
constexpr int kPictureSpouseGap = 36;
constexpr int kPictureSiblingGap = 24;
constexpr int kPictureMargin = 40;
constexpr int kPictureHeader = 100;
constexpr int kPictureLegend = 70;

struct PictureBox {
    uint32_t id = 0;
    int gen = 0;
    int x = 0;       // left edge, from the picture's left edge
    int w = 0;
    std::string job;   // the main job line under the name, empty without one (PictureLayout::jobs)
};

struct PictureFamily {
    uint32_t mother = 0;   // 0: not in the picture
    uint32_t father = 0;
    std::vector<uint32_t> kids;
};

struct PictureLayout {
    std::vector<PictureBox> boxes;
    std::vector<std::pair<uint32_t, uint32_t>> couples;   // the marriages drawn (the smaller id first)
    std::vector<PictureFamily> families;
    int gens = 0;
    // the picture's width without margins counted twice: the right edge of the last box + margin
    int width = 0;
    int people = 0;
    int boxHeight = kPictureBoxHeight;   // taller when the boxes carry a job line
    bool jobs = false;     // the boxes have a main job line
    // the town was too big: "everyone" fell back to the living and their ancestors, or was cut
    bool trimmed = false;
};

struct PictureOptions {
    bool livingOnly = false;   // the living, their ancestors and their husbands and wives
    int32_t now = -1;          // the month stamp of the picture's date, -1 none
    std::string village;       // the village's name, printed at the top; empty: none
    // a person whose descendants (and they themselves) are outlined in orange; 0: none
    uint32_t highlight = 0;
    std::string highlightName; // their name, for the line under the title
};

// The people the picture outlines: `options.highlight` and everyone who descends from them (empty when none
// is set).
std::vector<uint32_t> PictureHighlighted(const Tree& tree, const PictureOptions& options);
// The outline's color as "#rrggbb" and its width, the same in every format.
const char* PictureHighlightColor();
constexpr double kPictureHighlightWidth = 3.4;

// A main job under each name: how a job key reads on the picture (the game's own name for the profession).
struct PictureJobs {
    std::function<std::string(const std::string&)> nameOf;
};
constexpr int kPictureBoxHeightWithJob = 72;

// The picture's layout. An empty tree gives an empty layout. With `jobs`, each box has a third or fourth
// line, the job the person worked longest ("Farmer 12 y"), and the boxes are kPictureBoxHeightWithJob high.
PictureLayout BuildPictureLayout(const Tree& tree, bool livingOnly, const PictureJobs* jobs = nullptr);

// The picture's size and where its rows start, the same for every file format.
struct PictureCanvas {
    int width = 0;
    int height = 0;
    int top = 0;        // the first row's top edge
    int rowStep = 0;    // from one row's top edge to the next
    int RowY(int gen) const { return top + gen * rowStep; }
};
PictureCanvas CanvasFor(const PictureLayout& layout);

// The lines: a horizontal or vertical run, and the circles at the children. `color` is the palette index of
// the family (-1: a marriage, drawn dark). One family is one stroke, in the order they stand on the page.
struct PictureSegment {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};
struct PictureStroke {
    int color = -1;
    std::vector<PictureSegment> segments;
    std::vector<std::pair<int, int>> dots;   // where each child's line meets their box
};
std::vector<PictureStroke> BuildPictureStrokes(const PictureLayout& layout);

// A family palette entry as "#rrggbb"; the index wraps.
const char* PictureColor(int index);

// The words on the picture, shared by the formats.
std::string PictureTitle(const PictureOptions& options);
// "Village family tree" under the village's name, empty without one
std::string PictureKind(const PictureOptions& options);
std::string PictureSubtitle(const PictureLayout& layout, const PictureOptions& options);
std::string PictureName(const Person& person);            // the name, cut to fit its box
std::string PictureArrival(const Person& person);
std::string PictureDeath(const Person& person);           // empty for the living

// The SVG file's text.
std::string RenderPictureSvg(const Tree& tree, const PictureLayout& layout, const PictureOptions& options);

}  // namespace FamilyTree
}  // namespace Ext
