// ============================================================================
// src/engine/ui/spritecapture.cpp — WAVE 10 LANE UI4 — UI::SpriteCapture
// ============================================================================
//
// Two exports, both recorded UNREACHABLE by the wave-9 census with the blocker
// "dev capture tool; `_spriteCapture` is null in both harnesses":
//
//   ?AddSprite@SpriteCapture@UI@@QEAAXAEBVSprite@2@HHHHMI@Z   0x1800d8b20
//   ?AddScissor@SpriteCapture@UI@@QEAAXHHHH_N@Z               0x1800d8ef0
//
// ---------------------------------------------------------------------------
// THE BLOCKER WAS HALF TRUE, AND THE HALF THAT WAS FALSE IS THE USEFUL ONE
// ---------------------------------------------------------------------------
// It is true that no harness makes `SpriteContext::_spriteCapture` non-null.
// It does NOT follow that the subject is unreachable, because the guard reads
// a plain pointer field that the exe's own dev tool fills with an ORDINARY
// object: `SpriteCaptureUI::OnEvent` (exe 0x1400792b0) does
//
//     capture = MemAlloc(0x58, 8);
//     UI::SpriteCapture::SpriteCapture(capture);      // exported, 0x1800d9140
//     UI::UserInterface::BeginSpriteCapture(ui, capture, viewport);
//
// and `BeginSpriteCapture` (0x18011f4c0) is five stores — the capture pointer
// at UserInterface+0x110 and a viewport at +0x118. Every ingredient is an
// export. So the dependency was never unreachable; it was unREQUESTED. That is
// the distinction reference_synthetic_subject_beats_a_fake.md is about: no
// fake is needed here, because a REAL SpriteCapture can be built and driven.
//
//
// ---------------------------------------------------------------------------
// WHAT THE DISASSEMBLY SAYS THAT THE DECOMPILED C DOES NOT
// ---------------------------------------------------------------------------
// src/engine/ui/SpriteCapture.h carries the layout and the two facts that
// decide this file: the entry array's stride is 0x28 (so the stack block each
// body builds is the appended ELEMENT, not a dead local), and the stored
// `Entry::name` IS owned by the array because the entry array's element
// copier copy-constructs the String. Read that header before this file — the
// second fact was recorded backwards first, and the section says what caught
// it.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "collection/Collection.h" // ENDGAME B.3.2 step 2 — Array<T> bridge
#include "ui/Sprite.h"
#include "ui/SpriteCapture.h"
#include "ui/SpriteSheet.h"
#include "system/String.h"
#include "system/MemoryAllocator.h"
#include "../collection/array_internal.h" // ENDGAME B.3.2 step 2 — kSpec_CaptureEntry

namespace {

using UI::Sprite;
using UI::CaptureEntry;
using UI::SpriteCapture;

// ENDGAME B.3.2 step 2 — the two by-address SetSize forwarders that used to
// sit here are GONE.
//
// B4 X7-RT: SpriteCapture's three arrays are typed `Collection::Array` members
// (SpriteCapture.h), so both call sites below call the members' SetSizeFill:
//
//   0x18004bfc0  kSpec_Fill16  {0x10,8,Assign16(plain 16-byte copy),init=null,
//                destroy=null,byteCopy} == SpecFor<Sprite>(): `Sprite` is a
//                trivially copyable/destructible 0x10-byte POD, so the
//                trait-derived `Array<Sprite>` IS this row.
//   0x18004bde0  kSpec_CaptureEntry  NON-TRIVIAL: the assign leaves
//                `pad04` [0x04,0x08) unwritten and the destroy releases
//                `name`'s String refcount — stays the hand-derived
//                `ExplicitSpec`.
//
// (0x18004bde0 is kSpec_CaptureEntry, defined in array.cpp.)

// System::MemoryAllocator::MemAlloc / MemFree (0x1800c7a50 / 0x1800c7ad0),
// called directly. Retail spells both open in these bodies
// (`[0x1801fc718]+0x640`, Enter, Allocate, Leave); the two statics ARE that
// sequence.
inline void* MemAlloc(uint64_t bytes, int align) {
    return System::MemoryAllocator::MemAlloc(bytes, align);
}
inline void MemFree(void* p) {
    System::MemoryAllocator::MemFree(p);
}

// ---- the System::String refcount idiom -------------------------------------
// Layout is [int32 length][int32 refcount][chars…] with the String's stored
// pointer aimed at `chars`. Same idiom src/engine/system/String.h documents.
inline int32_t& StringRc(char* data) {
    return RtSystem::StringRefCount(data);
}
// Retail's release, instruction for instruction (0x1800d8fc2, 0x1800d8e7c and
// the two temporaries in AddSprite): decrement, and free the block eight bytes
// back only when the count reaches zero. The `slot = 0` store retail makes on
// the non-zero arm is to a stack local that dies at the RET and is not
// reproduced.
inline void ReleaseString(char* data) {
    if (!data) return;
    if (--StringRc(data) == 0)
        MemFree(data - 8);
}

}  // namespace

// ============================================================================
// 0x1800d8ef0  UI::SpriteCapture::AddScissor(int,int,int,int,bool)
// ============================================================================
//
// Records a scissor change as an entry whose `sheetIndex` is NEGATIVE and
// whose paired sprite is the invalid Sprite{null,-1}. `-1 - (int)fullScreen`
// is retail's `NEG byte / SBB EAX,EAX / DEC EAX` (0x1800d8f32): false -> -1,
// true -> -2. Retail's NEG writes back into the caller's argument home slot;
// that is a stack side effect on memory the caller owns and cannot be observed
// through the C ABI, so it is not reproduced.
//
// Two stores in this body are REDUNDANT and are kept because they are the
// body: `rotation`/`color` are zeroed at 0x1800d8f22 with the rest of the
// entry and zeroed AGAIN at 0x1800d8f7e. Same value, so the byte compare
// cannot see the difference — but a "cleanup" that dropped one would be a
// change to the instruction stream made for no reason.
namespace UI {

void SpriteCapture::AddScissor(int32_t x, int32_t y, int32_t right, int32_t bottom,
                               bool fullScreen) {
    // Every call does work -- this body has no early-out.

    // 0x1800d8f22..0x1800d8f2e — four qword stores covering name..color; the
    // name's is the member's own String() (B4 Y RT-UI), its release at the
    // end of the body the member's ~String (0x1800d8fc2).
    // `pad04` is deliberately NOT written: retail does not write it either,
    // and an oracle that compared it would report garbage
    // (reference_oracle_must_not_compare_unwritten_bytes.md).
    CaptureEntry e;
    e.x1 = 0; e.y1 = 0;
    e.x2 = 0; e.y2 = 0;
    e.rotation = 0.0f; e.color = 0;
    e.sheetIndex = -1 - static_cast<int>(fullScreen);

    // The empty name. Set() on a null slot allocates a fresh refcounted block.
    // The temporary lives in this block: its ~String at the closing brace is
    // retail's release at 0x1800d8f61..0x1800d8f7a, before the second store.
    {
        System::String tmp;                         // 0x1800d8f3c
        tmp.Set("");                                // System::String::Set 0x1800c95d0
        if (tmp._data) {
            e.name._data = tmp._data;
            ++StringRc(tmp._data);
        }
    }

    e.rotation = 0.0f; e.color = 0;   // the second, redundant store — see above
    e.x1 = x; e.y1 = y;
    e.x2 = right; e.y2 = bottom;

    Sprite fill;
    fill.spriteSheet = nullptr;
    fill.spriteIndex = -1;
    // pad0C is not written here either — retail's fill local is a qword zero at
    // [RBP-0x38] plus a dword -1 at [RBP-0x30], twelve bytes, exactly
    // kSpriteWrittenBytes.

    _sprites.SetSizeFill(_sprites._size + 1, &fill);            // 0x18004bfc0
    _entries.SetSizeFill(_entries._size + 1, &e);               // 0x18004bde0

    // `e`'s ~String here releases the LOCAL's reference (0x1800d8fc2). The
    // stored element has its own: the entry array's element copier
    // COPY-CONSTRUCTS the String (`INC dword ptr [RAX-0x4]` at 0x18004beac),
    // so the balance is exactly right and nothing is left dangling. See the
    // ownership section of src/engine/ui/SpriteCapture.h for how this was
    // nearly recorded backwards, and what caught it.
}

// ============================================================================
// 0x1800d8b20  UI::SpriteCapture::AddSprite(Sprite const&,int,int,int,int,
//                                           float,unsigned int)
// ============================================================================
//
// Three things happen, in retail's order:
//   (a) find the sprite's sheet in `_sheets`, appending it if new — the
//       append is Array<T>::SetSize INLINED by the compiler, not a call, so
//       it is written out below rather than delegated;
//   (b) build the entry, whose `name` comes from
//       SpriteSheet::GetSpriteNameFromIndex when the sprite index is valid and
//       from a freshly allocated "" when it is not;
//   (c) append the caller's Sprite to `_sprites` and the entry to `_entries`.
//
// THE SEARCH'S SUBJECT IS THE ExternalPtr VALUE, not the SpriteSheet object:
// `MOV R10,qword ptr [RDX]` (0x1800d8b67) loads Sprite+0x00 and compares it to
// each `_sheets` element with CMP on the whole qword. Two Sprites sharing a
// sheet dedup; two ExternalPtrs to the same sheet with different tag bits
// would not. Faithful means comparing the qword.
void SpriteCapture::AddSprite(const Sprite& sprite, int32_t x1, int32_t y1,
                              int32_t x2, int32_t y2, float rotation, uint32_t color) {
    // no early-out; see AddScissor

    // ---- (a) locate or append the sheet -------------------------------------
    const int oldSize = _sheets._size;
    int index = 0;
    bool found = false;
    if (oldSize > 0) {
        void* const key = sprite.spriteSheet;
        SpriteSheet* const* p = _sheets._data;
        for (index = 0; index < oldSize; ++index, ++p) {
            if (*p == key) { found = true; break; }
        }
    }
    if (!found) {
        index = oldSize;
        SpriteSheet* fill = sprite.spriteSheet;

        // Array<void*>::SetSize(oldSize + 1, fill), inlined by retail at
        // 0x1800d8b92..0x1800d8c9f. `newSize != oldSize` is retail's guard and
        // is always true here (newSize is oldSize + 1); it is kept because it
        // is the branch, and because a reader comparing this to the fourteen
        // SetSize bodies in array.cpp should see the same shape.
        const int newSize = oldSize + 1;
        if (newSize != oldSize) {
            SpriteSheet** oldData = _sheets._data;
            _sheets._size = newSize;
            if (newSize > _sheets._capacity) {
                _sheets._capacity = newSize;
                // The 3x growth heuristic, and note the condition: it fires
                // only on a one-element append to a NON-EMPTY array, so the
                // first append allocates exactly one slot.
                if (newSize == oldSize + 1 && oldSize != 0)
                    _sheets._capacity = newSize * 3;
                const int64_t bytes = static_cast<int64_t>(_sheets._capacity) * 8;
                _sheets._data = static_cast<SpriteSheet**>(MemAlloc(static_cast<uint64_t>(bytes), 8));
            }
            // Retail runs TWO cursors (EDX and RCX) that hold the same value
            // throughout; both are kept so the copy source reads as it does in
            // the disassembly.
            int32_t src = (oldData == _sheets._data) ? oldSize : 0;
            for (int64_t i = src; i < newSize; ++i, ++src) {
                SpriteSheet** dst = _sheets._data + i;
                SpriteSheet* const* from = (i < oldSize) ? (oldData + src) : &fill;
                *dst = *from;
            }
            if (oldData != _sheets._data && oldData != nullptr)
                MemFree(oldData);
        }
    }

    // ---- (b) build the entry ------------------------------------------------
    CaptureEntry e;                         // name: String(), 0x1800d8caa
    e.x1 = 0; e.y1 = 0;                     // 0x1800d8cae
    e.x2 = 0; e.y2 = 0;                     // 0x1800d8cb6
    e.rotation = 0.0f;                      // 0x1800d8cc1  XORPS/MOVSS
    e.color = 0;                            // 0x1800d8cc6
    e.sheetIndex = index;                   // 0x1800d8cca
    // `pad04` is not written — same window as AddScissor.

    char* name = nullptr;
    if (sprite.spriteIndex >= 0) {
        // SpriteSheet::GetSpriteNameFromIndex returns a System::String BY
        // VALUE, so the ABI is (this, hidden-return-slot, index) and the
        // hidden slot comes back in RAX. Getting the arity wrong here writes
        // through a sign-extended int — the failure mode
        // reference_hidden_retslot_wild_write.md describes.
        //
        // The member is retail's exported 0x1800032d0. The returned
        // temporary's own reference is released by its destructor at the end
        // of this block, exactly where retail releases the slot.
        System::String slot = sprite.spriteSheet
                                  ->GetSpriteNameFromIndex(sprite.spriteIndex);
        name = slot._data;
        if (name) { e.name._data = name; ++StringRc(name); }
    } else {
        // Retail does NOT call String::Set here — it open-codes an empty
        // string: MemAlloc(9, 4), length 0, refcount 1, one NUL copied from
        // the "" literal at 0x1801551f0.
        char* block = static_cast<char*>(MemAlloc(9, 4));
        char* data  = block + 8;
        RtSystem::StringLength(data) = 0;   // length
        RtSystem::StringRefCount(data) = 1;   // refcount
        data[0] = '\0';
        name = data;
        e.name._data = name;
        ++StringRc(name);
        ReleaseString(name);              // the local temporary
    }

    e.color    = color;                   // 0x1800d8e30
    e.rotation = rotation;                // 0x1800d8e39
    e.x1 = x1; e.y1 = y1;                 // 0x1800d8e43
    e.x2 = x2; e.y2 = y2;                 // 0x1800d8e4b

    // ---- (c) append ---------------------------------------------------------
    _sprites.SetSizeFill(_sprites._size + 1, &sprite);             // 0x18004bfc0
    _entries.SetSizeFill(_entries._size + 1, &e);                 // 0x18004bde0
    // `e`'s ~String: the same net-zero release as AddScissor's.
}

// ============================================================================
// 0x1800D8A60  UI::SpriteCapture::EndAddingSprites()      ord 2304
// WAVE 23 LANE TAIL
// ============================================================================
//
// A PURE function over `_entries`: no calls at all, no allocation, no
// virtuals, nothing outside the object. That is unusual enough in this
// namespace to state plainly, because it is what makes the body verifiable by
// an INPUT SWEEP against the shipped export rather than by waiting for a
// gameplay route (project_pure_functions_verify_by_sweep.md).
//
// Two passes over the same array:
//
//  1. BOUNDING BOX, over entries with `sheetIndex >= 0` only. Scissor entries
//     (`AddScissor` stores -1 or -2 at +0x00) are SKIPPED by `JL` at
//     0x1800D8A93 and contribute nothing to the extent.
//  2. REBASE, over EVERY entry including the scissors, subtracting the box
//     origin from all four coordinates.
//
// Retail:
//   1800d8a64  if (_entries._size <= 0) skip pass 1 entirely
//              minX = minY =  0x8000        ; +32768
//              maxX = maxY = -0x8000        ; -32768
//   1800d8a90  for each entry e:
//                  if (e.sheetIndex < 0) continue;
//                  minX = min(minX, e.x1);  maxX = max(maxX, e.x2);
//                  minY = min(minY, e.y1);  maxY = max(maxY, e.y2);
//   1800d8ac4  _width  = maxX - minX + 1;
//   1800d8ad4  _height = maxY - minY + 1;
//   1800d8af0  for each entry e (ALL of them):
//                  e.x1 -= minX; e.x2 -= minX;
//                  e.y1 -= minY; e.y2 -= minY;
//
// THE DEGENERATE CASE IS REPRODUCED, NOT REPAIRED. With zero entries - or with
// entries that are ALL scissors, which pass 1 skips - the seeds survive
// untouched and retail stores `_width = _height = -32768 - 32768 + 1 = -65535`.
// That is what the shipped DLL does and it is what this body does. It is
// exactly the shape CLAUDE.md's "don't fix a faithful crash with a null check"
// rule is about: clamping it here would not restore retail's behaviour, it
// would hide it from whatever reads `_width` next.
//
// THE SEEDS ARE SIGNED 32-BIT, and the min/max are `CMOVL`/`CMOVG` on dwords,
// so the whole computation is `int32_t`. Written as `int32_t` rather than
// `int` so the width there is stated, not inherited from the platform.
//
// THE TWO PASSES USE DIFFERENT CURSORS IN RETAIL (RAX walking the array in
// pass 1, an `R9` byte offset re-reading `_entries._data` from `R10` every
// iteration in pass 2). Both are just `entries[i]`; the second one's re-read
// matters only if something could reallocate the array mid-loop, and nothing
// in either pass calls anything at all. Written as one indexed loop each.
// ---------------------------------------------------------------------------
void SpriteCapture::EndAddingSprites() {
    const int32_t count = _entries._size;
    CaptureEntry* entries = _entries._data;

    // `contributed` counts the non-scissor entries pass 1 takes into the box;
    // nothing reads it.
    int32_t contributed = 0;

    int32_t minX =  0x8000, minY =  0x8000;
    int32_t maxX = -0x8000, maxY = -0x8000;

    for (int32_t i = 0; i < count; ++i) {
        const CaptureEntry& e = entries[i];
        if (e.sheetIndex < 0) continue;          // scissors excluded from the box
        ++contributed;
        if (e.x1 < minX) minX = e.x1;
        if (e.x2 > maxX) maxX = e.x2;
        if (e.y1 < minY) minY = e.y1;
        if (e.y2 > maxY) maxY = e.y2;
    }

    _width  = maxX - minX + 1;
    _height = maxY - minY + 1;

    // Pass 2 rebases EVERY entry, scissors included - see the banner.
    for (int32_t i = 0; i < count; ++i) {
        CaptureEntry& e = entries[i];
        e.x1 -= minX;
        e.x2 -= minX;
        e.y1 -= minY;
        e.y2 -= minY;
    }
}

// 0x1800d9010 ?BeginAddingSprites@SpriteCapture@UI@@QEAAXXZ — read this pass:
// the qword at +0x50 (width AND height) zeroed, then _sprites and _entries cut
// to size 0 through their NON-TRIVIAL rows (kSpec_SpriteIndexZ 0x1800370c0,
// then a tail-jump to kSpec_CaptureEntryZ 0x18004ba90). SpriteContext::Begin,
// into which retail inlines the same three steps, calls it.
void SpriteCapture::BeginAddingSprites() {
    _width  = 0;                                                       // 0x1800d901d
    _height = 0;                                                       //   (one qword)
    rtcoll::AsArray<&rtcoll::kSpec_SpriteIndexZ>(&_sprites)->SetSize(0);   // 0x1800370c0
    rtcoll::AsArray<&rtcoll::kSpec_CaptureEntryZ>(&_entries)->SetSize(0);  // 0x18004ba90
}

}  // namespace UI
