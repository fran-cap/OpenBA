// ============================================================================
// src/engine/ui/animatedimageelement.cpp — UI::AnimatedImageElement
// ============================================================================
//
// 2026-09-12, vtable batch 3. `??_7AnimatedImageElement@UI@@6B@` (retail
// 0x180156000). The subclass overrides exactly ONE behavioural slot — Update —
// and inherits ImageElement's ComputeSize/ComputeLayout/Draw, whose three
// addresses the table repeats verbatim:
//
//   slot  method                       retail       where
//   ----  ---------------------------  -----------  --------------------------
//    0    vector deleting dtor         0x180036db0  AnimatedImageElement.h (the class's own)
//    1    TypeName                     0x180036ab0  here
//    2    Type                         0x180036a90  here
//    4    ImageElement::ComputeSize    0x180118650  imageelement.cpp
//    5    ImageElement::ComputeLayout  0x1801186f0  imageelement.cpp
//    6    ImageElement::Draw           0x180118850  imageelement.cpp
//    7    Update                       0x1800cb290  here
//
// plus ??1AnimatedImageElement@UI@@UEAA@XZ 0x1800cb3a0 and the factory pair
// (ENDGAME B.3.9 UI-A: the real ctor ??0 0x1800cb3f0 is in the header; the
// copy-ctor door and FACTUI1's ??0 door are retired).
//
// LAYOUT: src/engine/ui/AnimatedImageElement.h — ImageElement (0xE0) plus
// `_frames` (Array<Sprite>, +0xE0), `_framesPerSecond` (+0xF0, ctor 30) and
// `_accumTicks` (+0xF8); sizeof 0x100. (The file-local constants this file
// carried until B.3.6 moved there, RULE B6-8.)
//
// ENDGAME B.3.6 block C2: slots 1/2/7 are MEMBERS of
// `UI::AnimatedImageElement`; the registered doors are one-line QUALIFIED
// delegations. Update's base call (by address 0x180116270 before) is the
// qualified `ImageElement::Update`, i.e. `Element::Update`.
//
// ENDGAME B.3.9 block UI-A: the class is CONCRETE; the slot doors and the
// copy / ??1 doors, read only by the retired minted table and its RT_DELDTOR
// row, are gone.
// ============================================================================
#include "element_common.h"
#include "ui/AnimatedImageElement.h"
#include "system/Timer.h"   // ENDGAME B.3.9 SS1: Timer::_secondsPerTick

#include <cmath>

using namespace rtuielem;

namespace {

// `System::Timer::_secondsPerTick`, an exported DOUBLE (ordinal 4797, read at
// 0x1800cb2d2 as `MOVSD XMM3,[0x1801fe728]`). The same slot
// src/engine/audio/audio_scene_update.cpp reads. ENDGAME B.3.9 SS1
// (B39_RULES D7): the class static now, no lazy AtomicExport cache (B9-10).
double SecondsPerTick() {
    return ::System::Timer::_secondsPerTick;
}


}  // namespace

namespace UI {

// ============================================================================
// ?Update@AnimatedImageElement@UI@@   0x1800cb290
// ============================================================================
// Advance the animation clock, pick a frame, and publish it into the inherited
// UI::Sprite — marking the parent chain dirty only when the frame actually
// changed.
//
//   Element::Update(this, time, ui, &dlgCopy, state)
//   accum += *(int64*)time                            ; System::Time is its ticks
//   elapsed = (float)((double)accum * _secondsPerTick)
//   period  = (float)frameCount / (float)fps
//   if (elapsed >= period) { elapsed -= period;
//                            accum = (int64)((double)elapsed / _secondsPerTick) }
//   frame   = (int)((elapsed / period) * (float)frameCount)
//   sprite  = frames[frame]                           ; ONE 16-byte MOVUPS
//   if (sprite.sheet != _sprite.sheet || sprite.index != _sprite.index)
//        SetNeedsSizeCalc(this)
//   _sprite = sprite                                  ; the same MOVUPS
//
// *** TWO THINGS A TIDIED PORT GETS WRONG. ***
//
//  1. `frameCount` is read TWICE from memory (0x1800cb2da for the period and
//     0x1800cb318 for the frame index), both times after Element::Update has
//     returned. Caching it in one local would assert that the base Update
//     cannot change the frame list.
//  2. THE PARENT WALK CLOBBERS THE `this` REGISTER. Retail computes
//     `RCX = this + 0xc0` BEFORE the walk (0x1800cb340), walks through RBX
//     — which ends at nullptr — and then stores the sprite through the SAVED
//     RCX. The walk here uses its own cursor.
//
// The comparison is `sheet` (a full qword) and `index` (a dword) only; the
// 4 pad bytes at +0xcc are copied but never compared.
//
// The InternalPtr<DialogInstance> parameter arrives BY HIDDEN REFERENCE (RULE
// B6-11; `MOV RAX,[R9]` at 0x1800cb29a); retail copies the pointed-to value
// into its own home slot and forwards the address of THAT. The base call is
// QUALIFIED: this object wears the minted table, whose slot 7 is this body.
void AnimatedImageElement::Update(const System::Time& time, UserInterface* ui,
                                  Object::InternalPtr<DialogInstance>* dialog,
                                  const UpdateState& state) {
    Object::InternalPtr<DialogInstance> dlgCopy = *dialog;     // 1800cb2a5
    ImageElement::Update(time, ui, &dlgCopy, state);          // 1800cb2b7  Element::Update

    const double spt = SecondsPerTick();

    _accumTicks += time._ticks;                             // 1800cb2c2  System::Time's ticks

    // NOT RETAIL: with no frames retail converts NaN (0 / 0 * 0) to int and
    // reads the empty array (UB). Nothing to show: return.
    if (_frames._size <= 0) return;

    float elapsed = static_cast<float>(static_cast<double>(_accumTicks) * spt);
    const float period = static_cast<float>(_frames._size) /   // 1800cb2da
                         static_cast<float>(_framesPerSecond);

    if (elapsed >= period) {
        elapsed = elapsed - period;
        // Hardening (not in retail): retail wraps once, so a tick longer than
        // two periods (a resource fps far above 60 x the frame count) leaves
        // the clock past the period for good.
        if (period > 0.0f && elapsed >= period) elapsed = std::fmod(elapsed, period);
        _accumTicks = static_cast<int64_t>(static_cast<double>(elapsed) / spt);
    }

    // NOT RETAIL: retail truncates the position unchecked (1800cb32b) and
    // indexes `_frames` with it; a position at or past the frame count, a
    // negative one (negative fps) or NaN is an out-of-bounds read / a float
    // to int conversion out of range (UB). It is clamped to the frames.
    const float position = (elapsed / period) *
                           static_cast<float>(_frames._size);   // 1800cb318
    int frame;
    if (!(position >= 0.0f)) {
        frame = 0;
    } else if (position >= static_cast<float>(_frames._size)) {
        frame = _frames._size - 1;
    } else {
        frame = static_cast<int>(position);
    }

    const Sprite next = _frames._data[frame];                  // 1800cb33c  MOVUPS

    if (_sprite.spriteSheet != next.spriteSheet ||
        _sprite.spriteIndex != next.spriteIndex) {
        SetNeedsSizeCalc();                                    // 1800cb370..1800cb384
    }
    _sprite = next;                                            // 1800cb386  MOVUPS
}

}  // namespace UI

// ============================================================================
// ??1AnimatedImageElement@UI@@UEAA@XZ   0x1800cb3a0 -- retail's body, moved here
// from the retired door (ENDGAME B.3.9 UI-A)
// ============================================================================
// Own vtable (the compiler's entry restamp), destroy the frame array through
// retail's Array<Sprite> dtor (0x180037310, which ZEROES the header, so the
// compiler-emitted member `~Array` after this body is a no-op), then the BASE
// (ImageElement) restamp and ~Element -- the compiler's chain.
namespace UI {
AnimatedImageElement::~AnimatedImageElement() {
    rtcoll::RtColl_ArrayDtor_180037310(&_frames);
}
}  // namespace UI

namespace UI {
// ?CreateNewObject@AnimatedImageElement@UI@@SAPEAVBase@Object@@XZ  0x180036ba0
// The factory-interpreter row this replaces: MemAlloc(0x100, 8), retail's
// null branch, then the out-of-line ctor ??0AnimatedImageElement (0x1800cb3f0).
Object::Base* AnimatedImageElement::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(AnimatedImageElement), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) AnimatedImageElement();
}
// ?CreateInPlaceObject@AnimatedImageElement@UI@@SAPEAVBase@Object@@PEAX@Z  0x180036c20
Object::Base* AnimatedImageElement::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) AnimatedImageElement();
}
}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::AnimatedImageElement's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@AnimatedImageElement@UI@@ (0x1801f9cc0): name "AnimatedImageElement", parent
// "ImageElement" (retail ?_ParentTypeName@AnimatedImageElement@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::AnimatedImageElement, "ImageElement", nullptr)
