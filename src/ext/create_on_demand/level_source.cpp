// ============================================================================
// src/ext/create_on_demand/level_source.cpp -- NOT RETAIL. ext.create_on_demand's level reader
// ============================================================================
// Class A (docs/extensions/handins/CREATEONDEMAND.md). Installed as Video::ImageBuffer::
// _extLevelSource (ImageBuffer.h has the seam's contract) while the row is on.
//
// Retail's PlatformSerialize reads, per level, the blob's size (Serialize(int&)), sizes the level
// array and reads the bytes into it (RtVideo_SerializeBlobArray), then calls Create with the levels.
// For a texture seeded from a packed resource file (usage 2, not degenerate: the images whose
// ImageBufferData::Create makes a texture from levels) this reads each size the same way and skips
// the bytes instead, leaving one Locator per level for the backend's Create on this thread
// (pending_levels.h). The level arrays stay empty: their memory is never allocated.
//
// A level the reader cannot locate (an impossible size) ends the skipping: it and every later level
// are read as retail reads them, and the levels already skipped are re-decoded into their arrays,
// so Create sees every byte, the eager way. Anything not a packed compressed stream is left to
// retail's read untouched (false, before anything is consumed).
//
// ext.texture_quality (docs/extensions/handins/TEXQUALITY.md): for an image whose top levels the run
// drops (render/common/texture_quality/texture_quality.h DropFor), the dropped levels' sizes are read
// and their bytes skipped like the rest, but no Locator is kept: the record holds the kept levels
// only, and they are the only ones ever re-decoded. A stream that is not locatable takes ext.
// texture_quality's eager read instead (its dropped levels skipped or thrown away). In the fallback
// above, a dropped level is read and freed, never re-decoded.
// ============================================================================

#include "ext/create_on_demand/level_source.h"

#include "create_on_demand/pending_levels.h"
#include "create_on_demand/stream_locate.h"
#include "ext/texture_quality/texture_quality.h"
#include "io/Serializer.h"
#include "texture_quality/texture_quality.h"
#include "video/ImageBuffer.h"

namespace Ext { namespace CreateOnDemand {

bool ReadLevels(IO::Serializer& ser, Video::ImageBuffer& image, int32_t count, Video::ImageBuffer::LevelArray& levels) {
    // An earlier image's levels its Create never took (it returned early) go now, whatever this
    // read does (review F-233): the entry is keyed by address alone, so an image built where that
    // one was freed would otherwise take them in its own Create.
    ::CreateOnDemand::ThreadPendingLevels().Clear();
    // The images ImageBufferData::Create turns into a texture from their levels.
    if (count <= 0 || image._kind != 2 || image.Width() == 0 || image.Height() == 0) return false;
    // ext.texture_quality: 0 unless the run drops levels and this image qualifies.
    const ::TextureQuality::Verdict verdict = ::TextureQuality::ClassifyImage(image, count);
    const int drop = verdict == ::TextureQuality::Verdict::Drop ? ::TextureQuality::Drop() : 0;
    ::TextureQuality::NoteImage(image, count, verdict);
    IO::Stream* const stream = ser._stream;
    {
        ::CreateOnDemand::Locator probe;
        if (!::CreateOnDemand::Locate(stream, 0, &probe)) {
            if (drop == 0) return false;
            Ext::TextureQuality::ReadDropped(ser, count, levels, drop);
            return true;
        }
    }
    ::CreateOnDemand::PendingLevels& pending = ::CreateOnDemand::ThreadPendingLevels();
    pending.Clear();
    pending.levels.reserve(static_cast<size_t>(count - drop));
    Collection::Array<uint8_t>* const e = levels.GetData();
    int64_t droppedBytes = 0;
    int64_t keptBytes    = 0;
    for (int32_t i = 0; i < count; ++i) {
        // Retail's size read (RtVideo_SerializeBlobArray seeds it with the empty array's size).
        int32_t size = e[i]._size;
        ser.Serialize(size, nullptr);
        ::CreateOnDemand::Locator loc;
        if (size >= 0 && ::CreateOnDemand::Locate(stream, size, &loc)) {
            ::CreateOnDemand::Skip(stream, size);
            if (i < drop) {   // ext.texture_quality: a dropped level, never re-decoded
                droppedBytes += size;
                continue;
            }
            keptBytes += size;
            pending.levels.push_back(std::move(loc));
            continue;
        }
        // Not locatable: this level and the rest the retail way, the skipped ones re-decoded.
        e[i].SetSize(size);
        ser.Serialize(size, e[i]._data, nullptr);
        RtVideo_SerializeBlobArray(ser, count - i - 1, e + i + 1);
        for (int32_t k = drop; k < i; ++k) {
            const ::CreateOnDemand::Locator& skipped = pending.levels[static_cast<size_t>(k - drop)];
            e[k].SetSize(skipped.len);
            ::CreateOnDemand::Redecode(skipped, e[k]._data);
        }
        pending.Clear();
        if (drop > 0) {
            // ext.texture_quality: the dropped levels this read decoded are given back (Create skips them).
            int64_t discarded = 0;
            for (int32_t k = i; k < drop && k < count; ++k) {
                discarded += e[k]._size;
                e[k].DestroyAndFree();
            }
            int64_t kept = 0;
            for (int32_t k = drop; k < count; ++k)
                kept += e[k]._size;
            ::TextureQuality::NoteDropped(false, droppedBytes, discarded, kept);
        }
        return true;
    }
    pending.image = &image;
    if (drop > 0) ::TextureQuality::NoteDropped(true, droppedBytes, 0, keptBytes);
    return true;
}

}}  // namespace Ext::CreateOnDemand
