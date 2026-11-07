// ============================================================================
// src/engine/video/save_image_to_file.cpp — Wave 18 lane VRES, row 7
//   ?SaveImageToFile@ImageBuffer@Video@@SA_NAEBVFilename@System@@HHPEAI@Z
//   retail 0x1800f2840, ordinal 3609.  static bool(const Filename&, int, int, uint*)
// ============================================================================
//
// Writes a 24-bit bottom-up BMP. The one caller is
// `src/game/Game/w2_takescreenshot.cpp:270`, the screenshot path.
//
// ---------------------------------------------------------------------------
// PORTING THIS DOES NOT CLOSE THE SCREENSHOT'S CUTOVER HOLE. Say it here so a
// reader cannot infer otherwise from the row being ported.
// ---------------------------------------------------------------------------
// The body below reaches four IO functions that nothing in this tree has
// ported, through `RT_EXPORT_ONCE` — and `ResolveExport` falls through to the
// raw DLL implementation for an unported symbol
// (`reference_converting_a_door_is_not_satisfying_it.md`). With the DLL absent
// those four resolve to null and the screenshot path dies exactly as it did
// before this file existed. Porting moves the hole from "no implementation" to
// "an implementation with four unresolved dependencies"; it does not close it.
//
// The four, named so the IO wave inherits them rather than rediscovering them:
//
//     ?CreatePath@FileSystem@IO@@QEBA_NAEBVFilename@System@@@Z     0x1800ae430  ord 2130
//     ??0BinaryFileStream@IO@@QEAA@XZ                              0x1800a8200  ord   55
//     ?Create@FileStream@IO@@IEAA_NAEBVFilename@System@@@Z         0x1800aa8f0  ord 1876
//     ?CloseFile@FileSystem@IO@@QEAAXAEAUFileInfo@12@@Z            0x1800accc0  ord 1726
//
// plus `??1BinaryFileStream@IO@@UEAA@XZ` (0x180023c80, ord 532), which is a
// five-instruction thunk onto `??1FileStream@IO@@UEAA@XZ` (0x1800aaab0). All
// five are IO rows, and IO is Wave 19's declared subject.
//
// `IEAA` on `?Create@FileStream@` means PROTECTED and `AEAA` on
// `?AdjustDelimiters@Filename@` means PRIVATE — neither means unexported. This
// DLL exports both (`reference_aeaa_means_private_not_unexported.md`), so every
// door below is a NAME door and not one of them is an address door.
//
// ---------------------------------------------------------------------------
// THE WAVE 6 HOLDBACK NOTE THIS REPLACES WAS SELF-CONTRADICTORY
// ---------------------------------------------------------------------------
// `src/engine/video/buffers.cpp:1415` held this row back with three reasons.
// Re-checked before inheriting any of them
// (`reference_holdback_blockers_are_stale_check_them_first.md`), and the note
// **disagreed with itself inside one comment block**: its opening line says
// *"One caller (src/game/Game/w2_takescreenshot.cpp, the screenshot path)"*
// and its closing paragraph says *"w2_takescreenshot.cpp calls a different,
// already-ported capture path — so leaving this on the DLL costs nothing."*
// Both halves sat in the same comment for four waves. The call site settles it:
// line 165 resolves this exact mangled name and line 270 calls it. "Grep the
// call site" is the cheap lesson; "the note already refuted itself" is cheaper.
//
// Its other two reasons:
//   * *"file I/O side effect — not diffable."* Not a real blocker: two
//     subjects, two paths, byte-compare the two files. That is the oracle in
//     `video_resources_probe.cpp` case 9.
//   * *"Wave 4 (IO) and System::String territory."* Half stale. The
//     `System::Filename` / `System::String` / allocator half is ported today,
//     and — the part that collapsed most of the work —
//     `src/engine/io/FileStream.h` has pinned `IO::FileStream` and
//     `IO::BinaryFileStream` completely since Wave 11 lane IOSTREAM: sizeof
//     0x50, `filename` at +0x18, `FileInfo` at +0x20, the Win32 handle at
//     +0x38, the byte cursor at +0x2c. Nothing here re-derives a layout.
//
// ---------------------------------------------------------------------------
// TWO ABI DETAILS THE DISASSEMBLY SETTLES AND A SIGNATURE WOULD GET WRONG
// ---------------------------------------------------------------------------
// 1. FOUR OF THE CALLEES RETURN A CLASS BY VALUE, so each has a HIDDEN RETURN
//    SLOT that does not appear in the C++ signature. Omitting it from the
//    typedef is a wild write that still returns the right answer
//    (`reference_hidden_retslot_wild_write.md`), so the slot is in every
//    typedef below. The disassembly also settles WHERE it goes, which differs
//    between the member and the free functions:
//      * members  (`GetPathFilename`, `GetPathDirectory`): RCX = `this`,
//        RDX = the return slot. 0x1800f288b/0x1800f2887 pass them that way.
//      * free operators (`System::operator+`): RCX = the return slot,
//        RDX and R8 = the operands.
// 2. `?GetPathFilename@` takes a `bool` in R8B, set to 1 here
//    (0x1800f2877 `MOV R8B,0x1`). It does NOT mean "with the extension" — see
//    the next section, which is what the disassembly actually says. The
//    argument name in the typedef below is `requireExtension` for that reason.
//
// ---------------------------------------------------------------------------
// IT SUBSTITUTES THE EXTENSION, IT DOES NOT APPEND ONE
// ---------------------------------------------------------------------------
// The `path` argument is expected to CARRY an extension, and the function
// replaces it. `?GetPathFilename@Filename@System@@QEBA?AV12@_N@Z` with the bool
// SET returns the base name with its extension REMOVED — it scans back for a
// `.` and takes the substring from the last separator up to `dot - 1`
// (0x1800c67bd..0x1800c6810) — and returns EMPTY when there is no `.` at all
// (0x1800c67a5: `TEST R10B,R10B / JZ` then `*ret = nullptr`). So the target is
// `directory + basename + ".bmp"`, and:
//
//   "…\Screenshot0.bmp"  ->  "…\Screenshot0.bmp"   (the real caller; a no-op)
//   "…\shot.png"         ->  "…\shot.bmp"          (extension REPLACED)
//   "…\shot"             ->  "…\.bmp"              (a file literally named .bmp)
//
// The third row is not hypothetical: lane VRES's oracle hit it on its first run
// by handing the function extensionless paths. BOTH sides produced it, which is
// how the evidence pointed at the probe rather than at this body. Recorded here
// because a caller that builds a path without an extension gets a silently
// wrong filename and a `true` return.
//
// PLATFORM. The three `WriteFile` calls are the only Win32 in the body and live
// in `video_resources_platform.cpp` behind `RtVideoWriteFileRaw`, so this file stays
// free of <windows.h> per PLAN §4.1 decision 1.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "io/FileStream.h"
#include "io/FileSystem.h"   // ENDGAME B.3.9 SS1: FileSystem::_singleton
#include "system/Filename.h"
// ENDGAME B.3.5 block C — the real `Video::ImageBuffer`, whose STATIC member
// this body now is.
#include "video/ImageBuffer.h"
#include "../collection/array_internal.h"
#include "../collection/array_destroy_internal.h"

// video_resources_platform.cpp. Returns the DWORD `WriteFile` reported, which is what
// retail adds to the cursor; retail pre-zeroes that out-parameter and ignores
// WriteFile's BOOL entirely, so a failed write contributes zero. Reproduced.
extern "C" uint32_t RtVideoWriteFileRaw(Platform::FileHandle handle, const void* buf,
                                       uint32_t bytes);

namespace {

// The two BMP headers, as <wingdi.h> declares them (BITMAPFILEHEADER is packed
// to 2, which is what makes it 14 bytes). This file stays free of <windows.h>.
#pragma pack(push, 2)
struct BmpFileHeader {
    uint16_t type;        // +0x00  'BM'
    int32_t  size;        // +0x02
    uint16_t reserved1;   // +0x06
    uint16_t reserved2;   // +0x08
    int32_t  offBits;     // +0x0a
};
#pragma pack(pop)
static_assert(sizeof(BmpFileHeader) == 14, "BITMAPFILEHEADER is 14 bytes");
static_assert(offsetof(BmpFileHeader, offBits) == 10, "bfOffBits @ 10");

struct BmpInfoHeader {
    int32_t  size;            // +0x00
    int32_t  width;           // +0x04
    int32_t  height;          // +0x08
    uint16_t planes;          // +0x0c
    uint16_t bitCount;        // +0x0e
    uint32_t compression;     // +0x10
    uint32_t sizeImage;       // +0x14
    int32_t  xPelsPerMeter;   // +0x18
    int32_t  yPelsPerMeter;   // +0x1c
    uint32_t clrUsed;         // +0x20
    uint32_t clrImportant;    // +0x24
};
static_assert(sizeof(BmpInfoHeader) == 40, "BITMAPINFOHEADER is 40 bytes");
static_assert(offsetof(BmpInfoHeader, xPelsPerMeter) == 24, "biXPelsPerMeter @ 24");

// The header values the writer fills in by hand.
constexpr uint16_t kBmpSignature         = 0x4d42;   // bfType 'BM'
constexpr int32_t  kBmpPixelOffset       = 0x36;     // bfOffBits: the two headers, 14 + 40 bytes
constexpr int32_t  kBmpInfoHeaderSize    = 0x28;     // biSize: sizeof(BmpInfoHeader)
constexpr int32_t  kBmpPelsPerMeter96Dpi = 0xec4;    // 3780 px/m == 96 DPI
static_assert(kBmpPixelOffset == sizeof(BmpFileHeader) + sizeof(BmpInfoHeader), "bfOffBits");
static_assert(kBmpInfoHeaderSize == sizeof(BmpInfoHeader), "biSize");

// ---------------------------------------------------------------------------
// 0x180053da0 — Collection::Array<uint8>::SetSize(newSize, const uint8& fill)
//
// Transcribed in full rather than specialised to the one call this body makes,
// so the shrink and regrow arms are the same code even though this call site
// only ever drives the fresh-array arm.
//
// It is NOT REGISTERED at its address, for the reason array_destroy.cpp gives
// for the eight helpers it also declines to register: retail's only call to it
// here is an in-DLL `rel32` that our registry cannot intercept, nothing in this
// tree resolves 0x180053da0, and a registration with no caller dilutes the
// census's dead-port signal (`reference_uncalled_port_is_not_verified.md`). It
// is also NOT added to lane Q's spec table in array.cpp: that file is another
// lane's, and this is a byte-element instantiation with a FILL, which none of
// its existing rows carries.
//
// The growth rule is retail's, including the branch that looks like a typo and
// is not: appending EXACTLY ONE element to a NON-EMPTY array trebles the
// capacity, while any other growth takes the new size exactly.
void ArraySetSizeByte(rtcoll::ArrayHdr* a, int32_t newSize, uint8_t fill) {
    const int64_t oldSize = a->_size;                    // MOVSXD RBP,[RCX+8]
    if (newSize == oldSize) return;                      // CMP EDI,EBP / JZ

    uint8_t* const old = a->_data;                       // MOV RSI,[RCX]
    a->_size = newSize;
    if (newSize > a->_capacity) {
        a->_capacity = newSize;
        if (newSize == oldSize + 1 && oldSize != 0)      // CMP EDI,EAX / TEST EBP,EBP
            a->_capacity = newSize * 3;                  // LEA EAX,[RDI+RDI*2]
        a->_data = static_cast<uint8_t*>(
            rtcoll::MemAlloc(static_cast<uint64_t>(a->_capacity), 1));
    }

    // Retail starts the fill at oldSize when the buffer did NOT move and at 0
    // when it did — `XOR EAX,EAX / CMP RSI,[RBX] / CMOVZ EAX,EBP`. So a
    // reallocated array re-copies its old elements out of the OLD buffer and
    // then fills the tail, in one loop.
    int64_t i = (old == a->_data) ? oldSize : 0;
    for (; i < newSize; ++i) {
        uint8_t* dst = a->_data ? a->_data + i : nullptr;
        if (!dst) continue;                              // TEST RDX,RDX / JZ
        *dst = (i < oldSize) ? old[i] : fill;
    }

    if (old != a->_data && old != nullptr)
        rtcoll::MemFree(old);
}

}  // namespace

// ============================================================================
// The body — now the STATIC member `Video::ImageBuffer::SaveImageToFile`
// (ENDGAME B.3.5 block C). Retail's `SA_N` says static, and the first parameter
// is `AEBVFilename@System@@`, a REFERENCE; the old free function took a pointer
// because the registered door's ABI hands one over, and that pointer-to-reference
// step now happens in the door at the bottom of this file, where it belongs.
// ============================================================================
namespace Video {

bool ImageBuffer::SaveImageToFile(const System::Filename& pathRef,
                                  int32_t width, int32_t height,
                                  uint32_t* pixels) {
    const System::Filename* const path = &pathRef;
    // ---- 1. build "<dir><name>.bmp" and adjust its delimiters --------------
    //
    // Four temporaries, constructed in this order and released in exactly the
    // reverse one at the end of the block. Each is initialised to null here;
    // retail's are raw stack slots, but every one of the four callees is a
    // constructor writing into its return slot and none reads it first, so the
    // initialisation is unobservable — and leaving them indeterminate would be
    // UB on our side for no gain.
    // ENDGAME B.3.1 step 2: the four are real System::String/Filename objects
    // now, so the four explicit releases at the end of this block are their
    // destructors — and C++ destroys locals in reverse declaration order, which
    // IS retail's release order (withExt, joined, dirPart, namePart). The inner
    // scope is what ends their lives at the point retail ends them; `target`
    // outlives them and is declared outside it.
    System::Filename target;    // retail [RBP-0x79]
    {
        const System::Filename namePart = path->GetPathFilename(true);  // [RSP+0x30]
        const System::Filename dirPart  = path->GetPathDirectory();     // [RBP-0x71]
        const System::String   joined   = dirPart.AsString() + namePart.AsString();
        const System::String   withExt  = joined + ".bmp";              // [RBP-0x69]

        // 0x1800f28bf: the Filename ADOPTS the concatenation's buffer and takes
        // a reference; it does not copy. `target` stays null when `withExt` is.
        if (withExt._data != nullptr) {
            target._data = withExt._data;
            ++RtSystem::StringRefCount(target._data);   // retail's inlined AddRef on the adopted block
        }
        System::FilenamePrivateAccess::AdjustDelimiters(target);
    }

    // ---- 2. the two BMP headers -------------------------------------------
    //
    // 14-byte BITMAPFILEHEADER and 40-byte BITMAPINFOHEADER, both built by
    // hand. Retail zeroes the file header with three stores (8+4+2) and the
    // info header with a `memset(p, 0, 0x28)` call into the CRT at 0x1800881b0.
    // B.4 (R-VIDEO): the two headers are their structs (the file header packed
    // to 2, as <wingdi.h> declares it), zeroed by value-initialisation.
    BmpFileHeader fileHeader = {};
    BmpInfoHeader infoHeader = {};

    // Row stride: 3 bytes per pixel, rounded DOWN to a multiple of 4 after
    // adding one pixel's worth — `LEA EAX,[RDI+1] / LEA R12D,[RAX+RAX*2] /
    // AND R12D,0xfffffffc`. All 32-bit, so it is written 32-bit here.
    const int32_t rowBytes =
        static_cast<int32_t>((3u * static_cast<uint32_t>(width + 1)) & 0xfffffffcu);

    fileHeader.type    = kBmpSignature;                 // bfType 'BM'
    fileHeader.size    = rowBytes * height + kBmpPixelOffset;   // bfSize
    fileHeader.offBits = kBmpPixelOffset;               // bfOffBits

    infoHeader.size   = kBmpInfoHeaderSize;
    infoHeader.width  = width;
    infoHeader.height = height;
    // Retail writes planes, bitCount and compression as ONE eight-byte store of
    // 0x0000000000180001 (0x1800f29d0). Split into its fields here: the bytes
    // are identical, and compression's zero half is already there.
    infoHeader.planes   = 1;
    infoHeader.bitCount = 24;
    infoHeader.xPelsPerMeter = kBmpPelsPerMeter96Dpi;   // 3780 == 96 DPI
    infoHeader.yPelsPerMeter = kBmpPelsPerMeter96Dpi;

    // ---- 3. open --------------------------------------------------------
    //
    // NOTE THE ARGUMENT: CreatePath is given `path`, the function's ORIGINAL
    // argument, not the `.bmp` target built above (0x1800f29e6 `MOV RDX,R14`).
    ::IO::FileSystem* fs = ::IO::FileSystem::_singleton;
    fs->CreatePath(*path);   // 0x1800ae430

    // The stream is a stack object retail constructs, uses and destroys in
    // place: a local, built by its real constructor (0x1800a8200) and destroyed
    // at the end of the function BEFORE `target` (declared first), which is
    // retail's order (section 6).
    IO::BinaryFileStream stream;

    // Retail calls FileStream::Create, NOT BinaryFileStream::Create.
    bool ok = stream.IO::FileStream::Create(target);   // B.3.9 IO: the member

    if (ok) {
        // The Win32 handle and the byte cursor of the stream's FileInfo
        // (FileStream.h asserts both against three independent bodies).
        Platform::FileHandle const handle = stream._file.winHandle;
        int32_t& cursor    = stream._file.cursor;

        cursor += static_cast<int32_t>(RtVideoWriteFileRaw(handle, &fileHeader, 14));
        cursor += static_cast<int32_t>(RtVideoWriteFileRaw(handle, &infoHeader, kBmpInfoHeaderSize));

        // ---- 4. the pixels, bottom-up, 32bpp -> 24bpp ----------------------
        // ENDGAME B.3.2: default ctor == retail's three zero stores; the
        // explicit free below zeroes the header, so ~Array adds nothing.
        rtcoll::ArrayHdr rowBuf;
        ArraySetSizeByte(&rowBuf, rowBytes, 0);

        // The padding bytes at the end of each row are written ONCE, by the
        // zero fill above, and never touched by the copy loop — so every row
        // carries the same zero padding. That is retail's behaviour and it is
        // why the buffer is filled rather than merely allocated.
        int32_t row = height - 1;
        if (row >= 0) {
            // Retail computes the first row's offset with a 32-BIT multiply and
            // only then sign-extends (`IMUL EAX,EDI / CDQE`), so the overflow
            // behaviour on a very large image is 32-bit. Reproduced rather than
            // widened.
            const int64_t rowStep = -static_cast<int64_t>(width);
            const uint32_t* src = pixels + static_cast<int64_t>(row * width);

            for (; row >= 0; --row) {
                if (width > 0) {
                    uint8_t* dst = rowBuf._data;
                    const uint32_t* s = src;
                    for (int32_t i = 0; i < width; ++i) {
                        // Pixel bytes 0, 1, 2 in order — the low 24 bits of
                        // each 32-bit (little-endian) pixel, copied unswapped.
                        const uint32_t px = *s;
                        dst[0] = static_cast<uint8_t>(px);
                        dst[1] = static_cast<uint8_t>(px >> 8);
                        dst[2] = static_cast<uint8_t>(px >> 16);
                        s   += 1;
                        dst += 3;
                    }
                }
                cursor += static_cast<int32_t>(
                    RtVideoWriteFileRaw(handle, rowBuf._data,
                                        static_cast<uint32_t>(rowBytes)));
                src += rowStep;
            }
        }

        // ---- 5. close, then release the row buffer ------------------------
        //
        // Retail closes the file HERE and the destructor below re-tests the
        // same handle field. Both are reproduced: CloseFile resets it to the
        // -1 sentinel, which is what stops the second close.
        if (stream._file.handle != RtIO::FileInfo::kHandleNone) {
            fs->CloseFile(stream._file);   // 0x1800accc0
        }
        ok = true;   // 0x1800f2b23 `MOV DIL,1`, after the close, unconditionally

        if (rowBuf._data) rtcoll::MemFree(rowBuf._data);
        rowBuf._data = nullptr;
        rowBuf._size = 0;
        rowBuf._capacity = 0;
    }

    // ---- 6. destroy the stream, then the Filename -------------------------
    //
    // Reached from BOTH paths: the `Create` failure arm jumps straight here
    // (0x1800f2a13).
    //
    // *** RETAIL INLINES THE DESTRUCTOR; THIS RUNS THE CLASS'S OWN ONE, AND THE
    // TWO WERE COMPARED INSTRUCTION BY INSTRUCTION BEFORE THE SUBSTITUTION. ***
    // Porting a destructor by assuming an exported body matches an inlined one
    // is how a prior lane nearly shipped a leak under a byte-identity claim
    // (`reference_a_written_recipe_can_be_wrong.md`), so the check came first:
    //
    //   ??1BinaryFileStream@IO@@UEAA@XZ (0x180023c80) is five instructions and
    //   tail-jumps to ??1FileStream@IO@@UEAA@XZ (0x1800aaab0), which does, in
    //   order: stamp ??_7FileStream (0x180154960); CloseFile if the handle is
    //   not -1; ~FileInfo(this+0x20) — releasing the Strings at FileInfo +0x28
    //   then +0x20; release `filename` at +0x18; stamp ??_7Stream
    //   (0x1801549a0); free the Encoding at +0x10 through 0x180006a10, which is
    //   `if (p) { Enter; Free(_singleton, p); Leave; }`.
    //
    //   The inlined copy at 0x1800f2b6f does the same six steps in the same
    //   order on the same offsets — stream+0x48 and +0x40 are exactly
    //   FileInfo's two Strings, and its encoding free is that same
    //   Enter/Free/Leave written out. The only difference is that the inlined
    //   copy stamps FileStream's table directly instead of arriving there
    //   through the thunk, which is the same store.
    //
    // `stream`'s destructor runs at the return, then `target`'s: retail's order.
    return ok;
}

}  // namespace Video
