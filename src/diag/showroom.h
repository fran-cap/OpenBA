// src/diag/showroom.h
// BANISHED_SHOWROOM=<list file> — a building showroom for visual A/B checks (issue #13).
//
// The list file names one building template per line ("Template\X.rsc"; '/' works too,
// '#' starts a comment). A new game with the flag set:
//   1. places every listed template, COMPLETED, through the stress town's siting and
//      placement path (StartTown_StressSpawnStructures in src/game/Game/starttown.cpp),
//      a quarter map east of the town with a 4-tile free ring each, logging one
//      "[Showroom] placed" line per building with its tile and world position;
//   2. keeps the world-gen scatter (trees, rocks, bushes) out of the rectangle around them;
//   3. pauses the game, waits for the world to settle, then pins the camera
//      (the BANISHED_CAMERA_PIN machinery in camera_input.cpp) on each placed building's
//      model bounds, from two opposite headings, and takes one screenshot per heading
//      through the retail TakeScreenshot route;
//   4. moves each shot out of the Documents Screenshots folder to
//      showroom/NN_<name>_yNNN.bmp in the working directory (the game dir) and logs
//      "[Showroom] DONE" at the end.
//
// Same seed + same list = the same layout and framing, so two runs (e.g. two texture
// encodings) give image pairs to compare by eye. Pixel diffs between runs measure
// nothing (CLAUDE.md), so nothing here diffs.
//
// Trees and months (the line forms are in showroom_list.h; example and run command in
// tools/showroom/README.md):
//   - "tree <species>" plants one fully grown tree of that species (oak, birch, pine from
//     Template\NaturalResourceTree.rsc, the mesh picked by species name; the orchard trees
//     from the start condition's tree pool). The trees stand in rows of up to 6 at 6-tile
//     spacing, a quarter map east of the town, and each logs "[Showroom] tree" with the
//     template and mesh it resolved to. A tree is shot from the first heading only.
//   - "month m" / "months m1 m2 ..." shoots every entry once per month (0 = start of Early
//     Spring, one month = 300 s of year time): before the first shot of a month the
//     calendar's year time is set and the weather updated, as the debug day slider does.
//     File names then carry an "m<month>_" prefix, and each month adds one wide shot of the
//     tree grid, showroom/m<month>_wide_y045.bmp.
//   - Known limit: the game is paused, so only what reads the calendar directly (the shader
//     season stamp, the weather's season step) follows the month; snow build-up and fruit
//     ripening are sim state and stay where the new game started.
//
// Forest and bench (issue #253, a repeatable "screen full of trees" GPU bench view):
//   - "forest <species|mixed> <columns> <rows> <spacing>" plants a grid of fully grown trees
//     through the same route as "tree" (world-gen's per-cell placement, so the forest is real
//     game trees to every renderer path), a quarter map east of the town; the siting moves or
//     shrinks the grid deterministically when it would cross water, buildings or the map edge.
//     One "[Showroom] forest" line says what was planted; in the screenshot sequence the
//     forest adds one shot per month pass, showroom/[m<month>_]forest_y045.bmp.
//   - "bench <dist> [yaw] [sweep]" replaces the screenshot sequence: the game is not paused,
//     no shot is taken and the showroom never ends the run; the camera stays on the forest's
//     centre at <dist> for the whole run (with `sweep`, moving as BANISHED_CAMERA_PIN's
//     ,sweep does). Run it under BANISHED_STRESS=1: the stress town builds as usual, its
//     [Stress] windows and its quit after BANISHED_STRESS_FRAMES are the bench's clock.
#pragma once

extern "C" {

bool        Showroom_Enabled();          // the flag is set and the list has entries or a forest
int         Showroom_Count();
const char* Showroom_Path(int index);    // "Template\X.rsc", or a tree's species ("oak")
const char* Showroom_Name(int index);    // "X" / "tree_oak", for logs and file names
bool        Showroom_IsTree(int index);  // a "tree <species>" entry

// starttown.cpp: one call per list entry after its placement attempt. placed=false
// leaves the entry out of the camera sequence; `why` says what stopped it. worldBounds
// points at the model's world box (min xyz, max xyz), which must outlive the run (the
// ModelComponent's own Geometry::_worldBounds); null frames the footprint instead.
void Showroom_NotePlaced(int index, bool placed, int tileX, int tileY, int w, int h, int mapDim,
                         const float* worldBounds, const char* why);

// starttown.cpp's resource scatter: true for a cell inside the showroom rectangle (every
// placed footprint plus its free ring), which the scatter leaves bare.
bool Showroom_InArea(int x, int y);

// The "forest" line: false when the list has none; otherwise its species ("mixed" or one
// species, lower case), grid and spacing in tiles.
bool Showroom_Forest(const char** species, int* columns, int* rows, int* spacing);

// starttown.cpp, once after planting the forest: its tile bounds (inclusive), the grid it
// planted, how many trees stand, the species tally text for the log, and the world box of the
// tree nearest the centre (as for Showroom_NotePlaced; null when none stands). Extends the bare
// rectangle over the forest and logs the "[Showroom] forest" line.
void Showroom_NoteForest(int x0, int y0, int x1, int y1, int mapDim, int columns, int rows, int planted,
                         const char* tally, const float* centreBounds);

// camera_input.cpp: a "bench ... sweep" line moves the pinned camera around the forest.
bool Showroom_BenchSweep();

// GameUpdateLoop::Update, once per frame: drives the sequence. Returns true on the frame
// a screenshot should be requested (the caller raises the TakeScreenshot request bit).
bool Showroom_Tick();
bool Showroom_WantsPause();

// camera_input.cpp, inside the camera pin: the current shot's camera focus, or false
// when no showroom shot is being framed.
bool Showroom_CameraFocus(float* x, float* y, float* z, float* dist, float* yaw);

}
