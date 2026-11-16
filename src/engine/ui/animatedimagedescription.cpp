// ============================================================================
// src/engine/ui/animatedimagedescription.cpp — UI::AnimatedImageDescription
// ============================================================================
//
// The cutover boot's wall after UI::RibbonDescription:
//
//   [RtMint] FATAL: call through a NOT-YET-PORTED minted vtable slot
//   [RtMint]   vtable : ??_7AnimatedImageDescription@UI@@6B@
//   [RtMint]   slot   : 2
//   [RtMint]   target : ?Type@AnimatedImageDescription@UI@@UEBAIXZ
//
// ??_7AnimatedImageDescription@UI@@6B@ (retail 0x180156060), ten slots:
//   0  deleting dtor      0x1800369e0  AnimatedImageDescription.h (the class's own)
//   1  TypeName           0x1800366d0  here
//   2  Type               0x1800366b0  here
//   3  GetFileVersion     0x1800cb6a0  here  (returns 0x10204)
//   4  External::Compile  0x1800066e0  already ported
//   5  Serialize          0x1800cb5c0  here
//   6  Reconstruct        0x180036850  here
//   7  Destruct           0x180036880  here
//   8  CreateInstance     0x1800cb480  here
//   9  Construct          0x180036870  here
// plus ??1AnimatedImageDescription@UI@@UEAA@XZ 0x1800cb6b0 and the factory pair.
//
// THIS CLASS DERIVES FROM UI::ImageDescription (the ctor stamps
// ??_7ImageDescription@UI@@6B@ first; ~AnimatedImageDescription TAIL-JUMPS to
// ~ImageDescription; Serialize CALLS ImageDescription::Serialize).
//
// LAYOUT: src/engine/ui/AnimatedImageDescription.h — ImageDescription (0x80)
// plus `_frameNames` (Array<System::String>, +0x80) and `_framesPerSecond`
// (+0x90, ctor 30); sizeof 0x98.
//
// THE CONSTRUCTOR IS OUT OF LINE HERE, UNLIKE THE REST OF THIS FAMILY:
// `??0AnimatedImageDescription@UI@@QEAA@XZ` (0x1800cb6f0) is a real exported
// function, and `Construct` (0x180036870) is a four-instruction thunk:
//     TEST RCX,RCX ; JNZ 0x1800cb6f0 ; XOR EAX,EAX ; RET
// ENDGAME B.3.9 UI-A: it is the class's real ctor (the header); Construct and
// Reconstruct spell it. The FACTUI1 row and its door are retired.
//
// ENDGAME B.3.6 block C2: every slot body is a MEMBER of
// `UI::AnimatedImageDescription`. Converted: the base Serialize (by address
// 0x1801184b0) and the
// applier (a direct call of the extern "C" door `RtUi_ImageDescription_
// ApplyToElement`) are qualified `ImageDescription::` member calls — the
// applier is name-hidden by this class's slot-8 override; the per-frame
// `GetSpriteIndexFromName` (RT_EXPORT_ONCE) is the real `UI::SpriteSheet`
// member.
//
// ENDGAME B.3.9 block UI-A (B39_RULES D1/D2): the class is CONCRETE; the slot
// doors and the ??1 door, read only by the retired minted table and its
// RT_DELDTOR row, are gone.
// ============================================================================
#include <cstddef>
#include <cstdint>
#include <new>

#include "description_common.h"
#include "ui/AnimatedImageDescription.h"
#include "ui/AnimatedImageElement.h"
#include "ui/SpriteSheet.h"

namespace {

using namespace rtdesc;

}  // namespace

// ============================================================================
// The member bodies
// ============================================================================
namespace UI {

// 0x1800cb6a0: `MOV EAX,0x10204 ; RET` — a THIRD distinct value in this family.
int32_t AnimatedImageDescription::GetFileVersion() const { return 0x10204; }

// 0x180036870: the null test sat in the retired door; then a tail jump to the
// ctor, whose return value (this) IS this function's.
Object::External* AnimatedImageDescription::Construct() {
    return ::new (this) AnimatedImageDescription();            // 180036873 JNZ
}
// 0x180036850: slot0(this, 0) through the object's own table, then a TAIL JUMP
// to the same ctor.
Object::External* AnimatedImageDescription::Reconstruct() {
    this->~AnimatedImageDescription();                                           // 18003685e
    return ::new (this) AnimatedImageDescription();            // 180036868 JMP
}
// 0x180036880: `MOV RAX,[RCX] ; XOR EDX,EDX ; JMP [RAX]`.
void AnimatedImageDescription::Destruct() { this->~AnimatedImageDescription(); }

// ============================================================================
// ?Serialize@AnimatedImageDescription@UI@@UEAA_NAEAVStream@IO@@@Z  0x1800cb5c0
// ============================================================================
//   ImageDescription::Serialize(this, stream)          0x1801184b0, QUALIFIED
//   rw = encoding factory, read or write side by stream->mode
//   n = _frameNames._size ; rw->vt[0xa0](rw, &n, 0)
//   if (pair.stream->mode == 0) Array<String>::SetSize(&_frameNames, n)  0x180029a80
//   Array<String> element walk (&pair, n, _frameNames._data)            0x180029bb0
//   rw->vt[0xa0](rw, &_framesPerSecond, 0)
//   if (rw) rw->vt[0](rw, 1)
//
// The int at +0x90 is serialized AFTER the array, not before. 0x180029a80 is
// array.cpp's kSpec_StringRefZ and 0x180029bb0 the text-codec string-array
// walker (src/engine/io/text_codec.cpp).
bool AnimatedImageDescription::Serialize(IO::Stream& streamv) {
    IO::Stream* stream = &streamv;

    ImageDescription::Serialize(*stream);                       // 1800cb5da

    IO::Serializer ser(*stream, 0);                            // [RSP+0x28], [RSP+0x30]
    IO::ReaderWriter* rw = ser._readerWriter;                  // 1800cb60a

    int32_t n = _frameNames._size;                             // 1800cb60f
    rw->SerializeInt(n, nullptr);

    if (ser._stream->_direction == 0) {                        // 1800cb62f (re-read)
        rtcoll::RtColl_ArraySetSize_180029a80(&_frameNames, n);    // 1800cb645
    }

    // 0x180029bb0 reads three arguments; the body's fourth is unused.
    RtIo_SerializeStringArray(&ser, n, _frameNames._data, nullptr);

    rw->SerializeInt(_framesPerSecond, nullptr);

    return true;                                   // 1800cb67d: ~Serializer (rw's scalar delete)
}

// ============================================================================
// ?CreateInstance@AnimatedImageDescription@UI@@...   retail 0x1800cb480   (slot 8)
// ============================================================================
// RCX = this, RDX = the hidden InternalPtr<Element> return slot. R8 (the
// Dialog*) is never read: this class creates no child elements.
//
// AnimatedImageElement is 0x100 bytes (`MOV EDX,0x100` at 0x1800cb4c1) and its
// constructor is CALLED, not inlined: `??0AnimatedImageElement@UI@@QEAA@XZ`
// (0x1800cb3f0), the real ctor since B.3.9. The ctor RETURNS `this` and retail
// keeps the returned RAX (0x1800cb4f8).
//
// Then the applier (ImageDescription::CreateInstance, 0x180118570), then the
// per-frame table:
//     Array<Sprite>::SetSize(&elem->_frames, _frameNames._size)   0x1800370c0
//     for i in [0, _frameNames._size):
//         sheet = _spriteSheet                           ; RE-READ each iteration
//         idx   = sheet ? sheet->GetSpriteIndexFromName(_frameNames[i]) : -1
//         elem->_frames[i] = { sheet, idx }              ; idx as a QWORD, see below
//     elem->_framesPerSecond = _framesPerSecond
//
// THE SAME QWORD-FROM-A-DWORD-SLOT AS THE APPLIER. 0x1800cb54f and 0x1800cb555
// write `dword [RSP+0x30]`; 0x1800cb568 reads `qword [RSP+0x30]` and 0x1800cb56d
// stores all eight bytes at entry+0x08. The WIDTH is the faithful part.
Object::InternalPtr<Element>* AnimatedImageDescription::CreateInstance(
        Object::InternalPtr<Element>* ret, const Dialog*) const {
    void* raw = rtdesc::MemAlloc(0x100, 8);                    // 1800cb4c1

    AnimatedImageElement* elem = nullptr;
    if (raw != nullptr)                                        // 1800cb4eb
        elem = ::new (raw) AnimatedImageElement();             // 1800cb4f3, keeps RAX

    Object::InternalPtr<ImageElement> slot(elem);              // 1800cb500
    ImageDescription::CreateInstance(&slot);                   // 1800cb50d  (name-hidden: qualified)

    const int32_t frames = _frameNames._size;

    rtcoll::RtColl_ArraySetSize_1800370c0(&elem->_frames, frames);   // 1800cb51f

    for (int32_t i = 0; i < frames; ++i) {                     // 1800cb524 / 1800cb578
        // 1800cb530 sheet re-read; 1800cb534..1800cb555 the index (or -1) into
        // a DWORD stack slot; 1800cb564 / 1800cb56d the sheet and a QWORD
        // store of that slot (see rtdesc::SpriteFromName).
        elem->_frames._data[i] = rtdesc::SpriteFromName(_spriteSheet, _frameNames._data[i]);
    }

    elem->_framesPerSecond = _framesPerSecond;                 // 1800cb586

    new (static_cast<void*>(ret)) Object::InternalPtr<Element>(elem);   // 1800cb58c
    return ret;
}

}  // namespace UI

// ============================================================================
// ??1AnimatedImageDescription@UI@@UEAA@XZ   0x1800cb6b0 -- retail's body, moved
// here from the retired door (ENDGAME B.3.9 UI-A)
// ============================================================================
//     stamp ??_7AnimatedImageDescription@UI@@6B@             ; 1800cb6cd (compiler's)
//     Array<String>::~Array(this + 0x80)                     ; 1800cb6d4
//     JMP ~ImageDescription                                  ; 1800cb6e2 (compiler's)
// 0x180010ab0 is the Array destructor that ALSO destroys its elements
// (array_destroy.cpp's J2 entry point); the plain Array::~Array the other
// classes use is 0x180039440. Using the wrong one here would leak a
// System::String per frame name. It ZEROES the array header, so the
// compiler-emitted member `~Array` of the `Collection::Array<System::String>`
// member that runs after this body sees an empty array (no element destroy,
// MemFree(null)), exactly as it did over the old Array<char*> spelling.
namespace UI {
AnimatedImageDescription::~AnimatedImageDescription() {
    rtcoll::RtColl_ArrayDtorElem_180010ab0(&_frameNames);          // 1800cb6d4
}
}  // namespace UI

namespace UI {
// ?CreateNewObject@AnimatedImageDescription@UI@@SAPEAVBase@Object@@XZ  0x1800367c0
// The factory-interpreter row this replaces: MemAlloc(0x98, 8), retail's
// null branch, then the out-of-line ctor ??0AnimatedImageDescription (0x1800cb6f0).
Object::Base* AnimatedImageDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(AnimatedImageDescription), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) AnimatedImageDescription();
}
// ?CreateInPlaceObject@AnimatedImageDescription@UI@@SAPEAVBase@Object@@PEAX@Z  0x180036840
Object::Base* AnimatedImageDescription::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) AnimatedImageDescription();
}
}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::AnimatedImageDescription's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@AnimatedImageDescription@UI@@ (0x1801f9c88): name "AnimatedImageDescription", parent
// "ImageDescription" (retail ?_ParentTypeName@AnimatedImageDescription@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::AnimatedImageDescription, "ImageDescription", nullptr)
