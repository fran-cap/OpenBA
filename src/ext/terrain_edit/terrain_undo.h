#pragma once
// ============================================================================
// src/ext/terrain_edit/terrain_undo.h -- ext.terrain_edit, the undo stack (NOT RETAIL)
// ============================================================================
// docs/extensions/handins/TERRAINEDIT.md. Standard C++ only, so tests/unit/ext_terrain_edit_test.cpp can
// check it without the game. One stroke (a held left button) is one undo step: the height words it changed,
// each as it was before the stroke, and the terrain type of each cell it retyped, as it was before the
// stroke. The first record of an index wins, so a long stroke over the same ground keeps the original and
// not the last value.
//  Redo: each stroke also remembers how it was painted (the tool, and for every frame the cell under the
// cursor and the brush settings), so an Undo can hand the stroke to a redo list and a Redo can paint it again
// through the same brush code, which gives back the water and the textures too. The redo list is a fork in
// the road: it is dropped as soon as anything new is done (a new stroke, another world), and a stroke whose
// ground has something built on it since is not redone.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Ext {
namespace TerrainEdit {

constexpr size_t kMaxUndoStrokes = 30;   // the oldest stroke is dropped past this

// A water quad a stroke took out of the mesh (its six indices made into one repeated vertex, so nothing is
// drawn): where it was in the index array, and the indices it had.
struct KilledQuad {
    int      position;
    uint16_t indices[6];
};

// A resource a stroke planted: the cell and the entity that stands on it (compared, never dereferenced, so it
// stays standard C++). The address alone is no identity: the allocator hands a freed 0x48 block to the next
// entity, so a forester's sapling on the cell of a chopped planted tree can have the tree's address (review
// F-242). The entity's generation (Object::Internal::_generation, drawn at its birth) tells them apart.
struct PlantedResource {
    int       cell;
    uintptr_t entity;
    uint32_t  generation;

    // Whether the occupant `address` of generation `occupantGeneration` is still this resource.
    bool IsStanding(uintptr_t address, uint32_t occupantGeneration) const {
        return address != 0 && address == entity && occupantGeneration == generation;
    }
};

// One frame of a stroke as it was painted: the cell under the cursor and the brush settings of that frame.
struct ReplayFrame {
    int   cell;
    float radius;
    float strength;
    float density;
};

// How a stroke was painted: the tool (a Mode value), the resource the Plant tool had picked (by name), and
// the frames in order. The first frame is the press. The ground the stroke changed is kept too (the height
// vertices and the retyped cells, as an Undo found them), so a Redo can tell whether something has been built
// on it.
struct Replay {
    int         mode = 0;
    std::string plant;
    std::vector<ReplayFrame> frames;
    std::vector<int> vertices;   // stride * vy + vx
    std::vector<int> cells;      // row * size + col
};

struct Stroke {
    Replay replay;   // how it was painted, for Redo
    // vertex index (stride * vy + vx) -> the word before the stroke
    std::unordered_map<int, int16_t> heights;
    // cell index (row * size + col) -> the type before the stroke
    std::unordered_map<int, uint8_t> types;
    // cell index -> the type the stroke last wrote there (an undo leaves a cell alone that holds another)
    std::unordered_map<int, uint8_t> written;
    // The water mesh's sizes from before the stroke added water to it (the vertex, index and feature-range
    // counts), so an undo can cut the mesh back. Only strokes that added water have them. The ground
    // texture's bytes of the cells the stroke repainted (cell index dim * row + col -> R, G, B, A packed as
    // the four bytes are in memory), as they were before the stroke.
    std::unordered_map<int, uint32_t> texels;
    std::vector<PlantedResource> planted;   // the resource tool: removed by an undo while they still stand
    std::vector<KilledQuad> killedQuads;   // restored by an undo, before the mesh is cut back
    // The box of height-grid vertices the stroke wrote (where the town's water access is looked at again
    // after an undo).
    bool hasBox = false;
    int  boxMinX = 0;
    int  boxMinY = 0;
    int  boxMaxX = 0;
    int  boxMaxY = 0;
    bool hasWater = false;
    int  waterVertices = 0;
    int  waterIndices = 0;
    int  waterRanges = 0;
    int  waterRangesA = 0;   // the river-style ranges' count before the stroke
    bool Empty() const {
        return heights.empty() && types.empty() && !hasWater && texels.empty() && planted.empty();
    }
};

class UndoStack {
public:
    // The stroke being painted. A press starts a fresh one. Once it has changed something it ends the redo
    // list (a stroke that changed nothing leaves it); a stroke that is itself a Redo keeps the strokes still
    // waiting to be redone.
    void BeginStroke(bool keepRedo = false) {
        _current = Stroke();
        _open = true;
        _keepRedo = keepRedo;
    }

    // The tool of the stroke being painted and its picked resource.
    void RecordTool(int mode, const std::string& plant) {
        if (!_open) return;
        _current.replay.mode = mode;
        _current.replay.plant = plant;
    }

    // One frame of the stroke being painted.
    void RecordFrame(int cell, float radius, float strength, float density) {
        if (_open) _current.replay.frames.push_back(ReplayFrame{ cell, radius, strength, density });
    }

    void RecordHeight(int vertex, int16_t before) {
        if (_open) _current.heights.emplace(vertex, before);   // emplace keeps the first value
    }

    void RecordType(int cell, uint8_t before) {
        if (_open) _current.types.emplace(cell, before);
    }

    void RecordPlanted(int cell, uintptr_t entity, uint32_t generation) {
        if (_open) _current.planted.push_back(PlantedResource{ cell, entity, generation });
    }

    void RecordWrittenType(int cell, uint8_t after) {
        if (_open) _current.written[cell] = after;
    }

    // Once per stroke: the water mesh's sizes before the stroke changed it, and every quad the stroke took
    // out of it.
    void RecordWater(int vertices, int indices, int ranges, int rangesA = 0) {
        if (!_open || _current.hasWater) return;
        _current.hasWater = true;
        _current.waterVertices = vertices;
        _current.waterIndices = indices;
        _current.waterRanges = ranges;
        _current.waterRangesA = rangesA;
    }

    void RecordBox(int minX, int minY, int maxX, int maxY) {
        if (!_open) return;
        _current.hasBox = true;
        _current.boxMinX = minX;
        _current.boxMinY = minY;
        _current.boxMaxX = maxX;
        _current.boxMaxY = maxY;
    }

    void RecordTexel(int cell, uint32_t before) {
        if (_open) _current.texels.emplace(cell, before);
    }

    // The stroke being painted (its vertices written so far).
    const Stroke& Current() const { return _current; }

    void RecordKilledQuad(int position, const uint16_t* indices) {
        if (!_open) return;
        KilledQuad q;
        q.position = position;
        for (int i = 0; i < 6; ++i) q.indices[i] = indices[i];
        _current.killedQuads.push_back(q);
    }

    // A release (or the tool going away): keep the stroke if it changed anything.
    void CommitStroke() {
        if (!_open) return;
        _open = false;
        if (_current.Empty()) return;
        if (!_keepRedo) ClearRedo();
        _strokes.push_back(std::move(_current));
        ++_version;
        _current = Stroke();
        if (_strokes.size() > kMaxUndoStrokes) _strokes.erase(_strokes.begin());
    }

    bool Open() const { return _open; }
    size_t Depth() const { return _strokes.size(); }

    // Stroke `i` counting from the top (0 is the most recent); null past the oldest.
    const Stroke* FromTop(size_t i) const {
        return i < _strokes.size() ? &_strokes[_strokes.size() - 1 - i] : nullptr;
    }

    // Changes whenever a stroke is added or taken off, or the stack is cleared.
    uint32_t Version() const { return _version; }

    // The most recent stroke, left on the stack; null when there is none.
    const Stroke* Top() const {
        return _strokes.empty() ? nullptr : &_strokes.back();
    }

    // The most recent stroke, removed from the stack. False when there is none.
    bool Pop(Stroke& out) {
        if (_strokes.empty()) return false;
        out = std::move(_strokes.back());
        _strokes.pop_back();
        ++_version;
        return true;
    }

    // ---- Redo ----
    // An Undo hands over how the stroke it reversed was painted, and what ground it changed.
    void PushRedo(Replay&& replay) {
        if (replay.frames.empty()) return;
        _redo.push_back(std::move(replay));
        if (_redo.size() > kMaxUndoStrokes) _redo.erase(_redo.begin());
        ++_redoVersion;
    }

    size_t RedoDepth() const { return _redo.size(); }

    // Redo stroke `i` counting from the next one to be redone; null past the last.
    const Replay* RedoFromTop(size_t i) const {
        return i < _redo.size() ? &_redo[_redo.size() - 1 - i] : nullptr;
    }

    // Changes whenever the redo list changes.
    uint32_t RedoVersion() const { return _redoVersion; }

    // The stroke to paint again, removed from the list. False when there is none.
    bool PopRedo(Replay& out) {
        if (_redo.empty()) return false;
        out = std::move(_redo.back());
        _redo.pop_back();
        ++_redoVersion;
        return true;
    }

    void ClearRedo() {
        _redo.clear();
        ++_redoVersion;
    }

    void Clear() {
        ++_version;
        _strokes.clear();
        ClearRedo();
        _current = Stroke();
        _open = false;
    }

private:
    std::vector<Stroke> _strokes;
    std::vector<Replay> _redo;
    uint32_t            _redoVersion = 0;
    uint32_t            _version = 0;
    Stroke              _current;
    bool                _open = false;
    bool                _keepRedo = false;
};

}  // namespace TerrainEdit
}  // namespace Ext
