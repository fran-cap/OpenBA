// ============================================================================
// tests/unit/rb_video_abi_test.cpp — ENDGAME B.3.8 block RB, Video::ImageBuffer
//
// The exe instantiates `Video::ImageBuffer` itself (its table copy
// 0x14015c8c0: ModDialog, NoiseUI, TradeWork), so B38_RULES D1/D2 make it
// CONCRETE with a PUBLIC real constructor (0x1800f2580) whose body is
// `InitFields`, a destructor whose body is `ReleaseFields`, and slot 0 = the
// exe's scalar deleting dtor 0x1400129d0
// (~ImageBuffer; MemFree on flags & 1).
//
// PART A — construction runs the shared body under ImageBuffer's compiler
//   table (CompiledExternal() and the stamp first, External's three zero
//   stores before it), and the object ends on ImageBuffer's own table.
// PART B — slot 0 destroys (ReleaseFields under ImageBuffer's table) and frees.
// (PART C, the doors reaching the same bodies, went with the doors and the
// minted row at ENDGAME B.3.9 VS.) PART D (b39_vs_*) — B9-16: the retired
// row's shape (not abstract, public virtual dtor) and the ctor's
// retail stores.
//
// MARKERS: the class's virtuals, ~ImageBuffer and the two shared bodies live
// in src/engine/video/buffers.cpp, which does not link here.
// ============================================================================
#include "unit.h"
#include "abi.h"   // RT_ASM_LABEL
#include "dtor_check.h"

#include "video/ImageBuffer.h"
#include "graphics/GeneratedImageBuffer.h"   // B.3.9 GFX: FrameCopy's part moved to rb_graphics_abi_test.cpp

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <new>
#include <type_traits>

extern const void* const g_rbIbVtGroup[] RT_ASM_LABEL("_ZTVN5Video11ImageBufferE");
extern const void* const g_rbGiVtGroup[] RT_ASM_LABEL("_ZTVN8Graphics20GeneratedImageBufferE");

namespace {
const void* IbVt() { return &g_rbIbVtGroup[2]; }
const void* VptrOf(const void* p) { return *static_cast<const void* const*>(p); }
int g_init = 0, g_rel = 0;
const void* g_initVptr = nullptr;
const void* g_relVptr = nullptr;
}  // namespace

namespace Video {
int32_t           ImageBuffer::GetFileVersion() const { return 7; }
bool              ImageBuffer::Serialize(IO::Stream&) { return true; }
Object::External* ImageBuffer::Reconstruct() { return this; }
void              ImageBuffer::Destruct() {}
Object::External* ImageBuffer::Construct() { return this; }
bool ImageBuffer::Create(ImageType, Usage, Format, uint32_t, int32_t, int32_t,
                         FilterMode, AddressMode, AddressMode, AddressMode, LevelArray*) {
    return false;
}
ImageBuffer::~ImageBuffer() { ReleaseFields(); }   // buffers.cpp's shape
void ImageBuffer::InitFields() {
    ++g_init; g_initVptr = VptrOf(this);
    _kind = 2; _filter = 1; _sourceName._data = nullptr;
}
void ImageBuffer::ReleaseFields() { ++g_rel; g_relVptr = VptrOf(this); }
}  // namespace Video

namespace Graphics {
int32_t     GeneratedImageBuffer::GetFileVersion() const { return 7; }
bool        GeneratedImageBuffer::Serialize(IO::Stream&) { return true; }
}  // namespace Graphics

namespace {

// B3X W1: the same stand-in with a recording class-scope operator delete, so
// the free that retail's flags-1 arm made is proven, not assumed.
struct RbRecImage final : Video::ImageBuffer { DTOR_CHECK_RECORDING_DELETE };

// B3X W1: retail slot 0 (the exe's scalar deleting dtor 0x1400129d0) is the
// destructor pair. The `_ZTV` group is read RAW on purpose: [2] (the address
// point) is now D1, `void(*)(void*)`, which frees nothing; the flags-1 call
// retail made is [3], D0, the deleting destructor. This pins the new two-slot
// order directly.
UNIT_TEST(rb_imagebuffer_slot0_is_the_exe_scalar_deleting_dtor) {
    static_assert(dtor_check::HasClassDelete<Video::ImageBuffer>::value,
                  "D0 frees through a class-scope operator delete (MemFree)");
    CHECK(g_rbIbVtGroup[2] != g_rbIbVtGroup[3]);
    g_init = g_rel = 0;
    void* mem = Object::detail::InternalMemAlloc(sizeof(Video::ImageBuffer), 8);
    Video::ImageBuffer* ib = ::new (mem) Video::ImageBuffer();
    CHECK(VptrOf(ib) == IbVt());
    using D0Fn = void (*)(void*);
    D0Fn d0 = reinterpret_cast<D0Fn>(const_cast<void*>(g_rbIbVtGroup[3]));
    d0(ib);                                   // flags 1: destroy + MemFree (free here)
    CHECK_EQ(g_rel, 1);
    CHECK(g_relVptr == IbVt());
    // The free, proven: the stand-in's D0 (its compiler slot 1) hands the block
    // to its recording operator delete once, after ReleaseFields ran.
    g_rel = 0;
    RbRecImage* r = dtor_check::NewOnMalloc<RbRecImage>();
    dtor_check::ResetFrees();
    dtor_check::CallD0(r);
    CHECK_EQ(g_rel, 1);
    CHECK_EQ(dtor_check::g_frees, 1);
    CHECK(dtor_check::g_lastFree == r);
}

UNIT_TEST(b39_vs_imagebuffer_is_concrete_with_a_public_virtual_dtor) {
    CHECK(!std::is_abstract<Video::ImageBuffer>::value);
    // B3X W1: the destructor is public and virtual (retail slot 0 = D1 + D0).
    CHECK(std::is_destructible<Video::ImageBuffer>::value);
    CHECK(std::has_virtual_destructor<Video::ImageBuffer>::value);
    CHECK((std::is_base_of<Object::CompiledExternal, Video::ImageBuffer>::value));
}

// The retail stores of 0x1800f2580 the stand-in InitFields above does NOT make:
// External's three qwords (+0x08/+0x10/+0x18) are the compiler's base ctors, and
// the vptr is the class's own table from the first byte the body sees.
UNIT_TEST(b39_vs_imagebuffer_ctor_stores_and_table) {
    g_init = g_rel = 0;
    void* mem = Object::detail::InternalMemAlloc(sizeof(Video::ImageBuffer), 8);
    std::memset(mem, 0xCD, sizeof(Video::ImageBuffer));
    Video::ImageBuffer* ib = ::new (mem) Video::ImageBuffer();
    CHECK_EQ(g_init, 1);
    CHECK(g_initVptr == IbVt());
    CHECK(VptrOf(ib) == IbVt());
    uint64_t q;
    for (size_t off = 0x08; off < 0x20; off += 8) {
        std::memcpy(&q, static_cast<unsigned char*>(mem) + off, 8);
        CHECK_EQ(q, (uint64_t)0);
    }
    CHECK_EQ(ib->_kind, 2);
    CHECK_EQ(ib->_filter, 1);
    CHECK(ib->_sourceName._data == nullptr);
    // retail slot 0 with flags 0 (Reconstruct's arm) is the complete destructor:
    // destroy in place, keep the storage.
    ib->~ImageBuffer();
    CHECK_EQ(g_rel, 1);
    Object::detail::InternalMemFree(mem);
}

}  // namespace
