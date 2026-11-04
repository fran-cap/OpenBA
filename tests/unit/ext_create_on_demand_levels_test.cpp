// ============================================================================
// tests/unit/ext_create_on_demand_levels_test.cpp -- ext.create_on_demand's level reader (CREATEONDEMAND)
// ============================================================================
// src/ext/create_on_demand/level_source.cpp leaves an image's skipped level Locators in the loading
// thread's PendingLevels, keyed by the image's address, for that image's backend Create to take. A
// Create that returns before it takes them (an out-of-range format) leaves them behind. Review F-233:
// the next ReadLevels that declined its image returned before it cleared them, so a later image
// allocated at the freed one's address matched the stale key (HasPendingLevels) and its Create
// recorded the old image's levels. The reader's TU is compiled here; it declines a count of 0
// before it reads the image or the stream.
// ============================================================================
#include "unit.h"

#include "../../src/ext/create_on_demand/level_source.cpp"

// The reader's other seams are not in the host build; the declined read below reaches none of them.
int32_t Video::ImageBuffer::Width() const { return 0; }
int32_t Video::ImageBuffer::Height() const { return 0; }
void RtVideo_SerializeBlobArray(IO::Serializer&, int, Collection::Array<uint8_t>*) {}
namespace TextureQuality {
int Drop() { return 0; }
Verdict ClassifyImage(const Video::ImageBuffer&, int32_t) { return Verdict::Off; }
void NoteImage(const Video::ImageBuffer&, int32_t, Verdict) {}
void NoteDropped(bool, int64_t, int64_t, int64_t) {}
}  // namespace TextureQuality
namespace Ext { namespace TextureQuality {
void ReadDropped(IO::Serializer&, int32_t, Video::ImageBuffer::LevelArray&, int) {}
}}  // namespace Ext::TextureQuality
namespace CreateOnDemand {
bool Locate(IO::Stream*, int32_t, Locator*) { return false; }
void Skip(IO::Stream*, int32_t) {}
}  // namespace CreateOnDemand

UNIT_TEST(ext_create_on_demand_declined_read_clears_stale_levels) {
    ::CreateOnDemand::PendingLevels& pending = ::CreateOnDemand::ThreadPendingLevels();
    // Image A's read succeeded and its Create never took the levels.
    alignas(Video::ImageBuffer) unsigned char storage[sizeof(Video::ImageBuffer)] = {};
    pending.Clear();
    pending.image = storage;
    pending.levels.resize(3);
    // Image B at A's address: a read the reader declines (no level blob) must not leave A's levels
    // keyed by B's address.
    // The three arguments are never read: a count of 0 returns before any of them is touched.
    alignas(IO::Serializer) unsigned char serStorage[sizeof(IO::Serializer)] = {};
    alignas(Video::ImageBuffer::LevelArray) unsigned char levelStorage[sizeof(Video::ImageBuffer::LevelArray)] = {};
    IO::Serializer& ser = reinterpret_cast<IO::Serializer&>(serStorage);   // CAST-OK: never read
    Video::ImageBuffer& b = reinterpret_cast<Video::ImageBuffer&>(storage);   // CAST-OK: never read
    Video::ImageBuffer::LevelArray& levels = reinterpret_cast<Video::ImageBuffer::LevelArray&>(levelStorage);   // CAST-OK: never read
    CHECK(!Ext::CreateOnDemand::ReadLevels(ser, b, 0, levels));
    CHECK(pending.image == nullptr);
    CHECK(pending.levels.empty());
    pending.Clear();
}
