// ============================================================================
// src/engine/ui/SpriteRecord.h -- NOT RETAIL: the finished sprite batches of one
// frame, kept so a later frame can draw them again
// ============================================================================
// The engine end of ext.render_pipe's replayed frames
// (docs/extensions/handins/SMOOTHREPLAY.md). `UI::SpriteContext::_extRecord` is
// null by default and in the faithful profile; while the game side points it at a
// SpriteRecord, SpriteContext::Flush appends every chunk it draws: the vertices it
// wrote (final: scaled, rotated, snapped by ext.sharp_ui) and the context state
// the chunk was drawn under. UserInterface::Draw adds the camera it set. Nothing
// here draws; the extension replays the list (src/ext/render_pipe/ui_layer.cpp).
//
// Plain data, standard C++ only: the unit tests build one by hand.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace Graphics { class MaterialInstance; }

namespace UI {

// One BeginVertices .. EndVertices chunk.
struct SpriteRecordBatch {
    Graphics::MaterialInstance* material = nullptr;   // the instance Flush set up for the batch
    bool     point      = false;   // Video::ImageBufferData::_extPointSampling when it was drawn
    int32_t  scissor[4] = {};      // Context::_scissor: x, y, width, height
    float    transform[16] = {};   // Context::_transforms[0] (a dialog's own scale, or identity)
    float    color[4]   = {};      // Context::_color
    int32_t  camera     = -1;      // index into SpriteRecord::cameras (-1: none recorded before it)
    int32_t  primitive  = 0;       // EndVertices' argument: 0 triangles, 2 lines
    int32_t  stride     = 0;       // the dynamic vertex's size, bytes
    int32_t  vertices   = 0;       // how many
    uint32_t firstByte  = 0;       // their offset in SpriteRecord::bytes
};

struct SpriteRecordCamera {
    float view[16];
    float proj[16];
};

struct SpriteRecord {
    std::vector<SpriteRecordBatch>  batches;
    std::vector<SpriteRecordCamera> cameras;
    std::vector<uint8_t>            bytes;

    void Clear() {
        batches.clear();
        cameras.clear();
        bytes.clear();
    }
    bool Empty() const { return batches.empty(); }

    // UserInterface::Draw's SetCamera: the batches after it are drawn under it.
    void AddCamera(const float* view, const float* proj) {
        SpriteRecordCamera c;
        std::memcpy(c.view, view, sizeof(c.view));
        std::memcpy(c.proj, proj, sizeof(c.proj));
        cameras.push_back(c);
    }

    // Flush, one chunk: room for `maxBytes` of vertices; the write cursor.
    uint8_t* BeginChunk(size_t maxBytes) {
        _chunkStart = bytes.size();
        bytes.resize(_chunkStart + maxBytes);
        return bytes.data() + _chunkStart;
    }
    // The chunk's vertices end at `cursor` (BeginChunk's pointer, advanced): `b` gets its place.
    void EndChunk(const uint8_t* cursor, SpriteRecordBatch b) {
        const size_t used = static_cast<size_t>(cursor - (bytes.data() + _chunkStart));
        bytes.resize(_chunkStart + used);
        if (used == 0 || b.stride <= 0) return;
        b.firstByte = static_cast<uint32_t>(_chunkStart);
        b.vertices  = static_cast<int32_t>(used / static_cast<size_t>(b.stride));
        b.camera    = static_cast<int32_t>(cameras.size()) - 1;
        batches.push_back(b);
    }

private:
    size_t _chunkStart = 0;
};

}  // namespace UI
