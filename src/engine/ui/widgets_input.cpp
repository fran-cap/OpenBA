// ============================================================================
// src/engine/ui/widgets_input.cpp — Wave 7 lane D — Spinner/Combo/NumberEdit/Number/Edit/Slider/Radio/Progress
// ============================================================================
//
// Ports 17 of the 27 UI::*Element exports this lane owns (PLAN.md §4.2 wave 7,
// §6). Layouts and their evidence are in src/engine/ui/{SpinnerElement,
// ComboElement,NumberEditElement,NumberElement,EditElement,SliderElement,
// RadioElement,ProgressElement}.h. The full function table, the oracle
// argument per function, everything held back and why, and the
// `EditElement::SetText` phantom-name resolution are in
// docs/history/runtime_port/WAVE7_LANE_D.md.
//
// Not run by this lane, by instruction: the game, `make`, `tools/rt_run.sh`,
// `sa-run`, `git commit`. Every number in the lane doc is a *prediction of
// what the oracle will report*, not a measurement.
//
// ---------------------------------------------------------------------------
// THE SHARED "FIRST CHILD" PATTERN
// ---------------------------------------------------------------------------
// SpinnerElement, SliderElement and ProgressElement's own exports do not hold
// a dedicated pointer to the widget they wrap — they read
// `Element::_children._data[0]` (the FIRST child in the ordinary, inherited
// Element child array; Element.h, lane A) and IsDerivedFrom-check it before
// using it. There is NO NULL CHECK on `_children._data` itself in retail, and
// none is added here: a SliderElement (whose _children is empty) faults on
// that load in retail too — CLAUDE.md's "don't fix a faithful crash" rule.
//
// ---------------------------------------------------------------------------
// THE ORACLE
// ---------------------------------------------------------------------------
// Two shapes, argued per function in WAVE7_LANE_D.md §2:
//
//  (a) REPLAY for the seven pure getters: call ours, call the DLL's own body
//      on the SAME (unmodified) object and compare the results. Safe
//      because nothing here writes.
//
//  (b) PREFIX REPLAY for the ten mutators: memcpy the live object into a
//      same-sized LOCAL scratch of this lane's own struct BEFORE our body
//      runs, run our body on the real object, run the DLL's original on the
//      scratch copy, and byte-compare over sizeof(OurStruct). Because both
//      copies start byte-identical (a real memcpy of the SAME live bytes, not
//      a freshly-constructed guess), ANY divergence in that range is a real
//      behavioural difference regardless of how many of those bytes this lane
//      has NAMED — same argument WAVE6_LANE_A.md gives for Video::Context.
//      The one caveat, same shape as that lane's AddVertex note: where the
//      mutator reaches a CHILD object through a shared pointer (Spinner's
//      wrapped NumberEditElement, Radio's checkbox array), the scratch copy's
//      pointer is the SAME address, so replaying the original touches the
//      REAL child a second time. Every case here is checked to be idempotent
//      under a repeated identical call before being ported this way; see
//      WAVE7_LANE_D.md §2 for the per-function argument.
//
//      NOT done this way: anything that swaps a REFCOUNTED pointer (a
//      shared string, a shared allocation). Replaying the original there
//      would decrement/increment the SAME refcount a second time — wrong
//      regardless of whether the first decrement freed it — which is exactly
//      why `EditElement::SetText` is a documented NO-ORACLE port rather than
//      a prefix-replay one (WAVE7_LANE_D.md §5).
//
//      *** THAT RULE WAS BROKEN IN THREE PLACES IN THIS FILE, and Wave 9
//      lanes M and N found it the expensive way — the oracle handed the DLL a
//      pointer our own call had already released, on every diff run of
//      tutorials 1-3. The three were
//      NumberElement::SetValue (both overloads), which reach the private
//      NumberElement::CacheText, and NumberEditElement::SetMinMax, which
//      reaches EditElement::SetText. All three are now NO-ORACLE ports; the
//      full evidence is in the block above RtUi_NumberElement_SetValueInt and
//      in docs/history/runtime_port/WAVE9_LANE_N.md. The rule was broken because
//      NumberElement.h called the owning pointer at +0xD0 "padding, not
//      reversed by this lane" — a layout gap is not a documentation problem
//      when an oracle decision is taken from the header.
//
//      THE SWEEP (lane N, 2026-09-08). Every prefix replay in this file and in
//      the rest of runtime/src was re-checked against ONE criterion: does the
//      replayed body release or reassign an owning refcounted pointer that is
//      COPIED INTO the scratch? Reaching such a pointer through a shared
//      pointer to a LIVE object is safe — the refcounts stay correct and the
//      work is merely done twice (the idempotency argument this block already
//      makes). Holding a COPY of the pointer is not: ours may already have
//      freed it. Results:
//        * SpinnerElement::SetMinMax / SetValue — SAFE. They reach the
//          refcounted `_text` through `_children._data[0]`, a live child, not
//          through a copied field. SpinnerElement itself is {Element, int}.
//        * SpinnerElement::SetElementId / SetIncrement, RadioElement::
//          SetActiveRadio, ProgressElement::SetMax / SetCurrent — SAFE. No
//          refcounted field is released by any of those bodies.
//        * Element's ReplayGuard family (element.cpp) — SAFE; the two bodies
//          that DO touch a refcounted string (SetName, SetToolTipString) were
//          already shadow oracles with the same reasoning.
//        * widgets_layout.cpp's TextElement::SetParameter / SetText — already
//          NO-ORACLE, naming CacheText's refcount management as the reason.
//        * sprite.cpp's replay* family — already balances its extra reference
//          with ReleaseStringRef.
//      Nothing else in runtime/src prefix-replays an object that owns a
//      System::String. The three fixed here were the whole set.
//
//      sizeof(OurStruct) is a LOWER BOUND on the true retail object (this
//      lane has not reversed every byte of any of its eight classes), so the
//      oracle proves everything IN that range and is silent about anything
//      past it — stated once here rather than on every function.
// ============================================================================
//
// ---------------------------------------------------------------------------
// ENDGAME B.3 step 6, FRAGMENT PASS F1 (2026-09-25)
// ---------------------------------------------------------------------------
// Every body in this file now lives in its class's MEMBER (src/engine/ui/
// {Spinner,Combo,NumberEdit,Number,Edit,Radio,Progress,Slider}Element.h
// declare them). The registered doors keep their names and addresses and are
// one-line QUALIFIED delegations, collected at the bottom of the file above
// the registration. The `RtUI::` POD aliases this file used (`using RtUI::X`)
// and block A's two `&self->base` forwarding overloads are gone: the members
// reach the fields by name. Dependencies the old bodies reached through a
// function-local first-call cache (RULE B6-5) now go either to the member
// directly (NumberEditElement::SetValue, EditElement::SetText,
// NumberElement::CacheText, TextElement::CacheText, Element::FindElementType,
// UserInterface::CloseDialog, Object::Base::IsDerivedFrom) or through a
// named free body (B3R RT-UI: MemoryAllocator::MemFree and the Collection
// body 0x180010ab0).
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "system/String.h"
#include "ui/SpinnerElement.h"
#include "ui/ComboElement.h"
#include "ui/NumberEditElement.h"
#include "ui/NumberElement.h"
#include "ui/EditElement.h"
#include "../collection/array_internal.h" // ENDGAME B.3.2 step 2 — kSpec_StringRef etc.
#include "ui/RadioElement.h"
#include "ui/CheckElement.h"
#include "ui/ProgressElement.h"
#include "ui/SliderElement.h"
// WAVE 9 LANE UIW2. SliderElement/ProgressElement/ComboElement all write
// into a wrapped TEXT widget's own parameter array, so this lane uses lane
// E's TextElement offsets rather than making a second, independent reading
// of them. Sprite.h is for ComboElement::UpdateText's 16-byte sprite copy.
#include "ui/TextElement.h"
#include "ui/LabelElement.h"
#include "ui/Sprite.h"
#include "ui/UserInterface.h"
#include "ui/DialogInstance.h"
#include "ui/ComboDialog.h"     // ComboElement::_openDialog
#include "collection/Collection.h"
// WAVE 9 LANE UIW2 PART 2. The String primitives the two newly ported
// address-door bodies call as ordinary C++ rather than through a door:
// RtString_StrCompare, RtString_ReleaseInline and RtSystem_String_Assign.
#include "system/string_internal.h"
// B6-5 door caches (AtomicAddr / AtomicExport) and the ResolveAddr
// declaration this file used to spell for itself.
#include "element_common.h"

namespace {

// ============================================================================
// Dependencies this lane calls rather than reimplements.
// ============================================================================
//
// `?IsDerivedFrom@Base@Object@@QEBA_NI@Z` (0x1800c0390, ord 3110, exported) is
// `Object::Base::IsDerivedFrom` — a real member since B.3.3 — and every "first
// child" lookup below calls it as one, exactly where retail calls the export.

// The NumberEditElement type id every SpinnerElement export reads as
// `dword ptr [0x1801fa190]` (disassembly of 0x1800d7cb0 and siblings): the first
// dword of the class's `_typeInfo`, i.e. `UI::NumberEditElement::TypeId()`, read
// at the point of use as retail does (ENDGAME B.3.9 TI-UI).

// `NumberEditElement::SetValue` (0x1800d4bb0), used as a dependency by
// SpinnerElement::SetMinMax/SetValue and NumberEditElement::SetMinMax, is a
// member defined below and called as one.
//
// **THE HOLDBACK IS OVER, AND THE STATED BLOCKER WAS ALREADY STALE.** Wave 7
// lane D held this body back because it calls the variadic
// `System::String::Format`, "which this lane declined to reproduce"
// (WAVE7_LANE_D.md §3). `?Format@String@System@@SA?AV12@PEBDZZ` has in fact
// been PORTED since Wave 2 (src/engine/system/string.cpp registers it), so the
// blocker had already dissolved before Wave 9 opened -- the same shape as
// reference_holdback_blockers_are_stale_check_them_first.md, three for three in
// one session.
//
// `?SetText@EditElement@UI@@QEAAXAEBVString@System@@@Z` (0x1800d0db0) --
// SetValue's second call -- is `EditElement::SetText`, likewise a member below.

// ---------------------------------------------------------------------------
// WAVE 9 LANE UIW2, PART 2 — two of the tree's unregistered ADDRESS doors, both
// in this file, both now PORTED rather than resolved.
// ---------------------------------------------------------------------------
//
// An unregistered address door is the WORST failure shape at cutover: no
// `[RtUnresolved]` line, no named residue row, just a call through null. The
// integrator's triage named three of them in this cluster; two were these.
//
// *** BOTH SITES WERE JUSTIFIED BY A COMMENT THAT WAS FACTUALLY WRONG, AND THE
// WRONGNESS IS WHY THEY WERE ADDRESS DOORS AT ALL. *** The old text said
// 0x1801189f0 was "an unexported private helper" and that for 0x18003ffe0
// "neither showed up as an exported mangled name in this lane's pefile check".
// Re-run against the shipped export directory, 2026-09-08, both ARE exported:
//
//     0x1801189f0  ?CacheText@NumberElement@UI@@AEAAXXZ      ord 1679
//     0x18003ffe0  ??9String@System@@QEBA_NAEBV01@@Z         ord 1290
//     0x1800da130  ?AddTab@TabElement@UI@@QEAA?AV…@Z         ord 1589  (declined, see the lane doc)
//
// `AEAA` means PRIVATE, not unexported — this DLL exports its private members.
// `?CacheText@NumberElement@UI@@AEAAXXZ` is `UI::NumberElement::CacheText`
// (numberelement.cpp owns the body since vtable batch 4); both NumberElement
// setters call it as a member. `??9String@System@@` is
// `System::String::operator!=`, called as the class operator by
// EditElement::SetText below.

// `?MemFree@MemoryAllocator@System@@SAXPEAX@Z` (0x1800c7ad0) — the static free
// EditElement::SetText itself calls -- called by name (B3R RT-UI).

// ----------------------------------------------------------------------------
// WAVE 9 LANE UIW — the Collection::Array entry points ComboElement and
// RadioElement reach. ENDGAME B.3.2 step 2 — every forwarder that used to sit
// here is GONE. `ComboElement::_strings/_sprites/_userData` and
// `RadioElement::_checkBoxes` are real `Collection::Array<...>` members
// (ComboElement.h / RadioElement.h), but the first two of ComboElement's three
// are declared `Array<void*>` as a PLACEHOLDER element type ("element =
// System::String" / "= UI::Sprite" in the header's own comment) — the true
// per-element operations (a String refcount bump, a 0x10-byte Sprite stride)
// are NOT what `SpecFor<void*>()` would derive, so those two stay bridged
// through their hand-derived row rather than called as the field's own
// trait-derived member functions:
//
//   0x180034860  kSpec_StringRef  NON-TRIVIAL (assign copy-constructs a
//                String, destroy releases it) — ComboElement::_strings
//   0x18004bfc0  kSpec_Fill16     TRIVIAL, but stride 0x10 vs. the field's
//                own `void*` stride of 8 — the field type is simply the
//                wrong shape to call directly — ComboElement::_sprites
//
// `_userData` and `_checkBoxes` ARE trait-equivalent for their field's actual
// (and correctly declared, for `_checkBoxes`) element type, so those two call
// their own `SetSizeFill`/`~Array` directly:
//
//   0x180024fe0  kSpec_Fill8   == SpecFor<void*>()      — ComboElement::_userData
//   0x180048fe0  kSpec_Fill8Z  == SpecFor<Element*>()   — RadioElement::_checkBoxes
//
// The two `~Array` (J1) cleanups, 0x180037310/0x180024aa0, are spec-agnostic
// (a J1 body only frees the buffer and zeroes the header; `destroy` is null
// either way), so `.~Array()` on the field's own type is exactly those rows
// too, stride mismatch or not.
//
// 0x180010ab0 (the String cleanup for `_strings`) is DIFFERENT: it is
// array_destroy.cpp's J2_ENTRY driving `ReleaseStringsCounted` off a
// `DestroySpec` (`kD_Str_08`), not an `rtcoll::ArraySpec`, so there is no
// `kSpec_*` row the class template could take — per the batch rule, this
// "DtorElem" helper stays a free-function call, by name
// (rtcoll::RtColl_ArrayDtorElem_180010ab0, B3R RT-UI).

// Resolve SpinnerElement/SliderElement/ProgressElement's wrapped "value"
// widget: Element::_children._data[0], returned only if IsDerivedFrom(typeId)
// agrees, else null — exactly retail's shape. NO NULL CHECK on `_data` itself:
// an empty _children array (a SliderElement passed where a SpinnerElement was
// expected, say) faults here in retail too.
inline UI::Element* FirstChildIfDerived(const UI::Element* self, uint32_t typeId) {
    UI::Element* first = self->_children._data[0];
    if (first->IsDerivedFrom(typeId)) return first;
    return nullptr;
}

// The dirty-flag parent walk every SetX below that touches display state
// performs: `do { p->_flags |= NeedsSizeCalc; p = p->_parent; } while (p);`
// Written once here since five different functions share the exact shape.
inline void MarkNeedsSizeCalcUpToRoot(UI::Element* self) {
    for (UI::Element* p = self; p != nullptr; p = p->_parent) {
        p->_flags |= UI::kElementFlagNeedsSizeCalc;
    }
}

// ============================================================================
// WAVE 9 LANE UIW2 — dependencies for ComboElement::SetCurrentItem/UpdateText
// and the two "parameter into the wrapped TextElement" setters.
// ============================================================================
//
// ?CacheText@TextElement@UI@@AEAAXXZ (0x18011ec10) — TextElement's OWN
// CacheText, the SHARED base implementation, NOT NumberElement's private
// 0x1801189f0 (Wave 7 lane D disambiguated the two with get_xrefs_to). It is
// `UI::TextElement::CacheText` (textelement_cachetext.cpp) and is called as
// that member.

// ??4String@System@@QEAAAEAV01@AEBV01@@Z (0x1800c9810) is
// `System::String::operator=` (ENDGAME B.3.1), called on the typed elements
// (TextElement::_params and ComboElement::_strings are Array<System::String>).

// ?FindElementType@Element@UI@@QEAA?AV?$InternalPtr@VElement@UI@@@Object@@I@Z
// (0x180115760) is `UI::Element::FindElementType` (element.cpp) — EXACT type-id
// equality, not IsDerivedFrom; `QEAA`, NOT `QEBA` (a non-const member; the
// `QEBA` spelling is a phantom no export carries). Called as the member; the
// hidden return slot is the member's by-value `InternalPtr<Element>`.
//
// ?CloseDialog@UserInterface@UI@@QEAAXV?$InternalPtr@VDialogInstance@UI@@@Object@@@Z
// (0x180120f20) is `UI::UserInterface::CloseDialog` (userinterface_close.cpp,
// block E). The InternalPtr<DialogInstance> is passed BY ADDRESS even though the
// mangled name spells it by value (RULE B6-11): retail's `LEA RDX,[RSP+0x30]`
// at 0x180112564 is that indirection, and the member takes the pointer.

// The two type ids these bodies read — AND THE FINDING THAT MADE THIS LANE
// CHEAP. Both are EXPORTED DATA. Checked with pefile against the shipped export
// directory, 2026-09-08:
//
//   0x1801faee8  ?_typeInfo@TextElement@UI@@0VBaseTypeInfo@Object@@A        ord 4922
//   0x1801fa040  ?_typeInfo@LabelElement@UI@@0VBaseTypeInfo@Object@@A       ord 4877
//   0x1801fa190  ?_typeInfo@NumberEditElement@UI@@0VBaseTypeInfo@Object@@A  ord 4886
//
// The FIRST DWORD of a BaseTypeInfo is the type id: the same reading
// src/engine/achievement/achievement_manager.cpp:274 uses, and the one retail's
// own `MOV EDX,dword ptr [0x1801faee8]` at 0x1800d6cb9 proves.
// ENDGAME B.3.9 TI-UI: each is `X::TypeId()` now -- TextElement, LabelElement,
// NumberEditElement, and the checkbox's `dword ptr [0x1801fa9a8]` (CheckElement).

// ============================================================================
// WAVE 9 LANE UIW2 — the tail both "parameter into the wrapped TextElement"
// setters run once the child is resolved and the index is in range. Retail
// 0x1800d6cf0-0x1800d6d1c and 0x180119b80-0x180119bac are the SAME SEVEN
// INSTRUCTIONS in the same order:
//   dirty-walk from the CHILD (not from `this`) up the parent chain,
//   String::operator=(&child->_params[index], str),
//   TextElement::CacheText(child).
// ============================================================================
void WrappedTextSetParam(UI::TextElement* child, int32_t index, const System::String& str) {
    MarkNeedsSizeCalcUpToRoot(child);
    child->_params._data[index] = str;
    child->CacheText();
}

} // namespace

namespace UI {

// ============================================================
// UI::NumberEditElement::SetValue  retail 0x1800d4bb0  (Wave 9 lane B3)
// ============================================================
//
//   int v = this->_min;                        // +0xF8
//   if (value >= this->_min) {
//       v = value;
//       if (value > this->_max) v = this->_max;    // +0xFC, CMOVG
//   }
//   String tmp; String::Format(&tmp, "%i", v);     // 0x1800c9730, literal 0x180154848
//   EditElement::SetText(this, tmp);               // 0x1800d0db0
//   tmp.Release();
//
// The clamp is written the way retail branches it rather than as
// `std::clamp(value, min, max)`, because the two are NOT the same function when
// `_min > _max`: retail's below-min branch (0x1800d4bcc `JL`) returns `_min`
// without consulting `_max` at all, so a table configured min=10 max=5 answers
// 10 for `value=0` and 5 for `value=20`. A clamp() would answer 5 for both.
// Nothing in the game is known to configure an inverted range; the point is
// that the port must not decide that for it.
//
// The format literal at 0x180154848 reads "%i" (bytes 25 69 00), confirmed by
// direct memory read against the shipped DLL rather than assumed from context.
//
// NO ORACLE. SetText mutates the element's text, rebuilds its cached sprite
// text and can resize the widget; there is no independent replica to build.
// This body is UNVERIFIED beyond the disassembly it transcribes.
void NumberEditElement::SetValue(int32_t value) {
    int32_t clamped = _min;
    if (value >= _min) {
        clamped = value;
        if (value > _max) clamped = _max;
    }

    const System::String tmp = System::String::Format("%i", clamped);

    SetText(tmp);   // EditElement::SetText, non-virtual

    // retail's inlined String::Release, the asymmetric one (src/engine/
    // system/String.h), is `tmp`'s destructor at the end of this body.
}

// ============================================================================
// SpinnerElement — 7/7 ported
// ============================================================================

// ---- ?GetMaxValue@SpinnerElement@UI@@QEBAHXZ  0x1800d7cb0 ------------------
int32_t SpinnerElement::GetMaxValue() const {
    const NumberEditElement* child = static_cast<const NumberEditElement*>(
        FirstChildIfDerived(this, UI::NumberEditElement::TypeId()));
    return child->_max;   // NO NULL CHECK — retail has none either.
}

// ---- ?GetValue@SpinnerElement@UI@@QEBAHXZ  0x1800d7cf0 ---------------------
int32_t SpinnerElement::GetValue() const {
    const NumberEditElement* child = static_cast<const NumberEditElement*>(
        FirstChildIfDerived(this, UI::NumberEditElement::TypeId()));
    return System::String::ToInteger(child->_text._data);
}

// ---- ?IsActive@SpinnerElement@UI@@QEBA_NXZ  0x1800d7c30 --------------------
bool SpinnerElement::IsActive() const {
    const NumberEditElement* child = static_cast<const NumberEditElement*>(
        FirstChildIfDerived(this, UI::NumberEditElement::TypeId()));
    return child->_active != 0;
}

// ---- ?SetElementId@SpinnerElement@UI@@QEAAXH@Z  0x1800d7e10 ----------------
// Writes the CHILD's elementId AND the spinner's own — Element::_elementId is
// the same inherited field on both (Element.h +0x88), so no dedicated
// SpinnerElement field is needed for this. (It HIDES Element::SetElementId,
// 0x180115820, exactly as the two exports are distinct in retail.)
void SpinnerElement::SetElementId(int32_t id) {
    NumberEditElement* child = static_cast<NumberEditElement*>(
        FirstChildIfDerived(this, UI::NumberEditElement::TypeId()));
    child->_elementId = id;
    _elementId = id;
}

// ---- ?SetIncrement@SpinnerElement@UI@@QEAAXH@Z  0x1800d7d30 ----------------
// One instruction; the whole body is inline in SpinnerElement.h.

// ---- ?SetMinMax@SpinnerElement@UI@@QEAAXHH@Z  0x1800d7d40 ------------------
// Writes the child's min/max directly, then re-clamps the child's CURRENT
// text value through it by calling NumberEditElement::SetValue.
void SpinnerElement::SetMinMax(int32_t minValue, int32_t maxValue) {
    NumberEditElement* child = static_cast<NumberEditElement*>(
        FirstChildIfDerived(this, UI::NumberEditElement::TypeId()));
    child->_min = minValue;
    child->_max = maxValue;
    const int32_t clamped = System::String::ToInteger(child->_text._data);
    child->SetValue(clamped);
}

// ---- ?SetValue@SpinnerElement@UI@@QEAAXH@Z  0x1800d7dc0 --------------------
void SpinnerElement::SetValue(int32_t value) {
    NumberEditElement* child = static_cast<NumberEditElement*>(
        FirstChildIfDerived(this, UI::NumberEditElement::TypeId()));
    child->SetValue(value);
}

// ============================================================================
// ComboElement — 7/7 ported
// ============================================================================

// ---- ?GetCurrentItem@ComboElement@UI@@QEBAHXZ  0x180075160 -----------------
int32_t ComboElement::GetCurrentItem() const { return _currentItem; }

// ---- ?GetItemCount@ComboElement@UI@@QEBAHXZ  0x180075180 -------------------
int32_t ComboElement::GetItemCount() const { return _strings._size; }

// ---- ?GetItemData@ComboElement@UI@@QEBAPEAXH@Z  0x180075170 ----------------
void* ComboElement::GetItemData(int32_t index) const {
    return _userData._data[index];   // NO BOUNDS CHECK — retail has none.
}

// ============================================================================
// ?AddItem@ComboElement@UI@@QEAAHAEBVSprite@2@AEBVString@System@@PEAX@Z
//                                   retail 0x1800750b0   (WAVE 9 LANE UIW)
// ============================================================================
//
// THE BODY, 0x1800750b0..0x180075112, is eleven instructions of orchestration:
//
//     AddItem(this, Sprite const& sprite, String const& str, void* userData):
//         0x180034860(&this->_strings,  _strings._size  + 1, &str);
//         0x18004bfc0(&this->_sprites,  _sprites._size  + 1, &sprite);
//         0x180024fe0(&this->_userData, _userData._size + 1, &userData);
//         return this->_strings._size - 1;
//
// THE ARGUMENT SHUFFLE IS THE ONE THING TO GET RIGHT, and it is easy to read
// backwards. `R8` is NEVER SET before the first call — retail relies on the
// incoming `String const&` already being in it. `RDX` (the Sprite) is saved to
// RBX at entry and becomes the SECOND call's R8. `R9` (userData) is spilled to
// its home slot at entry and the THIRD call's R8 is the ADDRESS of that slot,
// not the value: the element type is `void*` and the helper takes a POINTER to
// the fill. Getting that last one wrong stores a stack address as the item's
// user data and nothing complains until the caller reads it back.
//
// THE RETURN IS READ FROM MEMORY, not computed. `MOV EAX,[RDI+0xf0]; DEC EAX`
// re-reads `_strings._size` AFTER all three appends, so it is the post-append
// size minus one — which equals the index just written. If a growth helper had
// failed to grow, retail would return the stale index and so does this.
int32_t ComboElement::AddItem(const UI::Sprite& sprite, const System::String& text,
                              void* userData) {
    rtcoll::AsArray<&rtcoll::kSpec_StringRef>(&_strings)
        ->SetSizeFill(_strings._size + 1, &text);                  // 0x180034860
    rtcoll::AsArray<&rtcoll::kSpec_Fill16>(&_sprites)
        ->SetSizeFill(_sprites._size + 1, &sprite);                // 0x18004bfc0
    _userData.SetSizeFill(_userData._size + 1, &userData);         // 0x180024fe0

    return _strings._size - 1;
}

// ============================================================================
// ?RemoveAllItems@ComboElement@UI@@QEAAXXZ  retail 0x180075120  (LANE UIW)
// ============================================================================
//
//     RemoveAllItems(this):
//         0x180010ab0(&this->_strings);    ; destroy every String, free, zero
//         0x180037310(&this->_sprites);    ; ~Array
//         0x180024aa0(&this->_userData);   ; ~Array
//         this->_currentItem = -1;
//
// Lane D read the last store correctly as -1 and it is -1 (`MOV dword ptr
// [RBX+0x118],0xffffffff`) — worth confirming explicitly, because the SIBLING
// `SetCurrentItem` resets an out-of-range index to 0 rather than -1, and a
// reader who assumes the two agree gets one of them wrong.
//
// **`_openDialog` (+0x120) IS NOT TOUCHED.** Removing every item while a combo
// popup is open leaves the popup pointer dangling in retail. That looks like an
// omission and it is not this port's business to repair it — CLAUDE.md's rule
// on faithful crashes applies to faithful *leaks* for the same reason.
void ComboElement::RemoveAllItems() {
    rtcoll::RtColl_ArrayDtorElem_180010ab0(&_strings);   // 0x180010ab0 — J2, not convertible; see the banner
    _sprites.~Array();                     // retail 0x180037310 (J1)
    _userData.~Array();                    // retail 0x180024aa0 (J1)
    _currentItem = -1;
}

// ============================================================================
// ComboElement::UpdateText — `?UpdateText@ComboElement@UI@@AEAAXXZ`, retail
// 0x180112180 (`AEAA` private, and EXPORTED — ComboElement.h)
// ============================================================================
// It finds the LABEL widget the combo displays its current item in, then pushes
// both the current item's STRING and its SPRITE into it.
//
// TWO THINGS A READER WILL WANT TO CHECK AGAINST THE LISTING, because both look
// like transcription slips and neither is:
//
//   1. `_children._data[4]`, NOT [0]. 0x18011219b/0x1801121a7 are
//      `MOV RBX,[RCX+0xa0]` then `MOV RBX,[RBX+0x20]` — 0x20/8 = 4. Every other
//      wrapped-widget lookup in this file is children[0]; this one is the fifth
//      child. Ghidra's C prints both loads as one expression, which is the
//      shape reference_ghidra_c_output_vs_disassembly.md warns about.
//   2. NEITHER index into `_strings` NOR into `_sprites` is bounds-checked
//      against the arrays' own sizes. Retail guards only the ITEM COUNT at the
//      top (+0xf0) and, for the sprite, the sprite's own validity. A
//      `_currentItem` of -1 — which ?RemoveAllItems@ sets — would index [-1].
//      That is unreachable in practice only because the +0xf0 guard makes the
//      array non-empty and SetCurrentItem never leaves -1 behind; it is not a
//      check retail performs, so it is not one this port performs.
void ComboElement::UpdateText() {
    if (_strings._size <= 0) return;

    const uint32_t labelType = UI::LabelElement::TypeId();

    // children[4], then: EXACT type match short-circuits the search entirely
    // (0x1801121bb `CMP EAX,R12D` against the virtual Type(), slot 2).
    Element* start = _children._data[4];
    Element* found = nullptr;
    if (start->Type() == labelType) {
        found = start;
    } else {
        // Retail re-reads `start->_children._data` on every iteration
        // (0x1801121f0 `MOV RAX,[R15]`) while holding `&_size` fixed in R14;
        // transcribed with the same re-read for the same reason
        // Element::RemoveChildren does.
        for (int32_t i = 0; i < start->_children._size; ++i) {
            Element* c = start->_children._data[i];
            if (c == nullptr) continue;
            found = c->FindElementType(labelType).Get();
            if (found != nullptr) break;
        }
    }

    // 0x180112234: the found node is re-tested with IsDerivedFrom, which is
    // redundant after an exact-type FindElementType and is transcribed anyway.
    // It is NOT redundant on the short-circuit path above, where `start` was
    // matched on its own virtual Type().
    // NOT RETAIL: retail calls IsDerivedFrom first (0x18011223d) and tests
    // for null after it (0x18011224f), so a fifth child that holds no label
    // (a malformed or modded combo layout) is a member call through null (UB,
    // and a fault in retail). The null test runs first here.
    if (found == nullptr) return;
    if (!found->IsDerivedFrom(labelType)) return;
    LabelElement* label = static_cast<LabelElement*>(found);

    // ---- the STRING half (0x180112258-0x1801122a5) -------------------------
    if (label->_params._size > 0) {
        MarkNeedsSizeCalcUpToRoot(label);
        label->_params._data[0] = _strings._data[_currentItem];
        label->CacheText();
    }

    // ---- the SPRITE half (0x1801122aa-0x180112306) -------------------------
    // `_sprites` is Array<UI::Sprite>, 16 bytes per element — retail computes
    // the offset as `(currentItem + currentItem) * 8`, which is the same thing
    // and is why the listing shows an `ADD RCX,RCX` before an `RCX*8` scale.
    // The two guards ARE UI::Sprite::IsValid() taken apart: index >= 0
    // (0x1801122bb `JL`) and sheet != null (0x1801122c2 `JZ`).
    // B.4 LEFT: ComboElement.h spells `_sprites` Array<void*> over 16-byte
    // UI::Sprite elements (block R-UI-a's header).
    const ::Sprite* src = _sprites._data + _currentItem;
    if (src->spriteIndex >= 0 && src->spriteSheet != nullptr) {
        ::Sprite* dst = &label->_sprite;   // LabelElement +0x100
        // Retail compares the sheet pointer and the index, then stores all
        // 16 bytes EITHER WAY — the dirty walk is what the comparison gates,
        // not the store (0x1801122ee `JZ` jumps straight to the MOVUPS at
        // 0x180112306). pad0C rides along in the 16-byte store and is
        // deliberately not part of the comparison.
        if (dst->spriteSheet != src->spriteSheet || dst->spriteIndex != src->spriteIndex)
            MarkNeedsSizeCalcUpToRoot(label);
        *dst = *src;
    }
}

// ---- ?SetCurrentItem@ComboElement@UI@@QEAAXH@Z  0x180112540 ---------------
//
// The body itself allocates nothing and frees nothing. Note what it does NOT
// do: after handing `_openDialog` to CloseDialog it simply ZEROES the field
// (0x18011256e) with no release and no destructor call — Object::InternalPtr is
// a handle, not a refcounted pointer, and the callee only reads the slot.
//
// The out-of-range branch resets `_currentItem` to 0, NOT to -1 (0x1801125a8,
// `MOV dword ptr [RBX+0x118],0x0`). ?RemoveAllItems@ is the only thing that
// leaves -1. Getting this backwards would put the combo on the wrong item
// silently, so it is called out rather than left to the reader.
void ComboElement::SetCurrentItem(int32_t index) {
    if (_openDialog != nullptr) {
        // The local slot retail builds at [RSP+0x30] and passes by address.
        DialogInstance* const open = _openDialog;
        Object::InternalPtr<DialogInstance> dialogSlot(open);
        // B.4 LEFT: DialogInstance::_ui is `void*` (block R-UI-a's header).
        open->_ui->CloseDialog(&dialogSlot);   // +0x10
        _openDialog = nullptr;
    }

    if (_currentItem == index) return;

    const bool inRange = (index >= 0 && index < _strings._size);
    _currentItem = inRange ? index : 0;
    UpdateText();
}

// ============================================================================
// NumberEditElement — 3/3 ported
// ============================================================================

// ---- ?GetValue@NumberEditElement@UI@@QEBAHXZ  0x1800d4b50 ------------------
int32_t NumberEditElement::GetValue() const {
    return System::String::ToInteger(_text._data);
}

// ---- ?SetMinMax@NumberEditElement@UI@@QEAAXHH@Z  0x1800d4b70 ---------------
// Same shape as SpinnerElement::SetMinMax, minus the child indirection:
// write min/max on `this` directly, then re-clamp through SetValue.
void NumberEditElement::SetMinMax(int32_t minValue, int32_t maxValue) {
    _min = minValue;
    _max = maxValue;
    const int32_t clamped = System::String::ToInteger(_text._data);
    SetValue(clamped);
}

// ============================================================================
// NumberElement — 2/2 ported
// ============================================================================
//
// BOTH NumberElement SETTERS ARE NO-ORACLE PORTS (Wave 9, lane M's Finding 1;
// applied by lane N): both end in the private NumberElement::CacheText
// (0x1801189F0), which REPLACES the refcounted System::String at +0xD0, so a
// prefix replay handed the DLL a pointer our own call had already released.
// Correctness rests on matching the disassembly instruction for instruction:
// 0x180118D50 and 0x180118DB0 are transcribed field for field below, including
// retail's `ucomiss`/`jp` NaN behaviour in the float guard (an unordered
// compare falls through to the write, which `!(a == b)` reproduces). The full
// evidence is in docs/history/runtime_port/WAVE9_LANE_N.md.

// ---- ?SetValue@NumberElement@UI@@QEAAXH@Z  0x180118d50 ---------------------
void NumberElement::SetValue(int32_t value) {
    if (!(_mode == 0 && _intValue == value)) {
        _intValue = value;
        _floatValue = 0.0f;   // retail zeroes the raw dword; 0 == 0.0f bitwise.
        _mode = 0;
        MarkNeedsSizeCalcUpToRoot(this);
        CacheText();
    }
}

// ---- ?SetValue@NumberElement@UI@@QEAAXM@Z  0x180118db0 ---------------------
void NumberElement::SetValue(float value) {
    if (!(_mode == 1 && _floatValue == value)) {
        _floatValue = value;
        _intValue = 0;
        _mode = 1;
        MarkNeedsSizeCalcUpToRoot(this);
        CacheText();
    }
}

// ============================================================================
// EditElement — ?SetText@EditElement@UI@@QEAAXAEBVString@System@@@Z 0x1800d0db0
// ============================================================================
// NO REPLAY ORACLE. This one swaps a REFCOUNTED shared string: it decrements
// the OLD value's refcount (freeing it via MemFree if that count hits zero) and
// increments the NEW one's. Correctness rests on matching the decompile
// instruction-for-instruction, recorded in WAVE7_LANE_D.md §5.
void EditElement::SetText(const System::String& text) {
    if (_active == 0) {
        // ??9String@System@@QEBA_NAEBV01@@Z  retail 0x18003ffe0 — the body is
        // System::String::operator== negated, so this is the class operator.
        const bool notEqual =
            _text != text;
        if (notEqual) {
            MarkNeedsSizeCalcUpToRoot(this);
        }

        char* oldPtr = _text._data;
        char* newPtr = text._data;
        if (oldPtr != newPtr) {
            if (oldPtr != nullptr) {
                int32_t* refcount = &RtSystem::StringRefCount(oldPtr);
                *refcount -= 1;
                if (*refcount == 0) {
                    System::MemoryAllocator::MemFree(oldPtr - 8);
                }
            }
            _text._data = newPtr;
            if (newPtr != nullptr) {
                RtSystem::StringRefCount(newPtr) += 1;
            }
        }
    }
}

// ============================================================================
// RadioElement — 3/3 ported
// ============================================================================

// ---- ?SetActiveRadio@RadioElement@UI@@QEAAXH@Z  0x1800d56f0 ----------------
// Clears the "checked" byte (+0xd8, `CheckElement::_checked` — a field of the
// CHECKBOX child class, not RadioElement's own) on every checkbox except the
// new active index.
void RadioElement::SetActiveRadio(int32_t index) {
    if (index != _activeIndex) {
        _activeIndex = index;
        const int32_t count = _checkBoxes._size;
        for (int32_t i = 0; i < count; ++i) {
            if (i != _activeIndex) {
                // FindCheckBoxes only appends elements that passed the
                // CheckElement type test (or null — see below).
                static_cast<CheckElement*>(_checkBoxes._data[i])->_checked = 0;
            }
        }
    }
}

// ============================================================================
// ?FindCheckBoxes@RadioElement@UI@@  — BOTH overloads   (WAVE 9 LANE UIW)
//   public  0x180047c00  ?FindCheckBoxes@RadioElement@UI@@QEAAXXZ
//   private 0x1800d5750  ?FindCheckBoxes@RadioElement@UI@@AEAAXV?$InternalPtr@…
// ============================================================================
//
// THE PRIVATE ONE, 0x1800d5750..0x1800d5812:
//
//     FindCheckBoxes(this, InternalPtr<Element>* p):
//         typeId = *(uint32*)0x1801fa9a8;          ; READ FIRST, before the vcall
//         e = *p;
//         if (typeId == e->vtbl[2](e)) {           ; Type(), a VIRTUAL call
//             e2 = *p;                              ; RE-READ
//             found = IsDerivedFrom(e2, *(uint32*)0x1801fa9a8) ? e2 : nullptr;
//             0x180048fe0(&this->_checkBoxes, _checkBoxes._size + 1, &found);
//         }
//         for (i = 0; i < ((Element*)*p)->_children._size; ++i) {
//             child = ((Element*)*p)->_children._data[i];
//             if (!child) continue;
//             InternalPtr<Element> tmp = child;     ; zeroed, then stored
//             FindCheckBoxes(this, &tmp);           ; RECURSE
//         }
//
// FOUR THINGS THAT ARE NOT TIDINESS:
//
//  1. **The type test is a VIRTUAL `Type()` call, not `IsDerivedFrom`** — and
//     then `IsDerivedFrom` runs anyway, on the same object, against the same
//     id, purely to decide whether the stored pointer is the element or NULL.
//     `Type()` is EXACT equality, `IsDerivedFrom` walks the base chain, so the
//     second call can only ever say yes here. Retail does both and so does this.
//  2. **The global is read TWICE**, at 0x1800d575f and again at 0x1800d577b.
//     Reproduced as two reads.
//  3. **`*p` is RE-READ at every use** — before the vcall, after it, before the
//     loop, and again at the bottom of every iteration (0x1800d57ed
//     `MOV RAX,[RSI]`). The loop BOUND is re-read with it. So a child whose
//     recursion reparented the tree is seen by the next iteration.
//  4. **NO NULL CHECK on `*p` or on its vtable.** A null element faults on the
//     `MOV RAX,[RCX]` in retail too. Not guarded here — CLAUDE.md's rule.
void RadioElement::FindCheckBoxes(Object::InternalPtr<Element>* root) {
    const uint32_t typeId = UI::CheckElement::TypeId();          // read BEFORE the vcall
    const uint32_t actual = root->Get()->Type();       // slot 2, virtual

    if (typeId == actual) {
        Element* e2 = root->Get();                     // retail re-reads
        Element* found = e2->IsDerivedFrom(UI::CheckElement::TypeId()) ? e2 : nullptr;
        // retail 0x180048fe0 — `kSpec_Fill8Z`, and NOT trait-equivalent: its
        // `Assign8Z` stores a ZERO into the slot and THEN the value (the
        // smart-pointer element's inlined `operator=`), two writes where a
        // trait-derived placement copy is one. array.cpp reproduces that
        // deliberately, so the hand-derived row is what this append must
        // reach, not `_checkBoxes`'s own trait-derived SetSizeFill.
        rtcoll::AsArray<&rtcoll::kSpec_Fill8Z>(&_checkBoxes)
            ->SetSizeFill(_checkBoxes._size + 1, &found);
    }

    for (int32_t i = 0; i < root->Get()->_children.GetSize(); ++i) {
        Element* child = root->Get()->_children.GetData()[i];
        if (child == nullptr) continue;
        Object::InternalPtr<Element> tmp;              // retail zeroes it first
        tmp = Object::InternalPtr<Element>(child);
        FindCheckBoxes(&tmp);
    }
}

// The public overload is six instructions: it puts `this` into a stack slot and
// hands the ADDRESS of that slot to the private one. Note it starts the walk at
// the RadioElement ITSELF, so a RadioElement that were somehow its own checkbox
// type would add itself — retail does not special-case it and neither does this.
void RadioElement::FindCheckBoxes() {
    Object::InternalPtr<Element> slot(this);
    FindCheckBoxes(&slot);
}

// ============================================================================
// ProgressElement — SetMaximumValue / SetCurrentValue are one instruction each
// and live inline in ProgressElement.h; SetProgressTextParameter below.
// ============================================================================

// ============================================================================
// WAVE 9 LANE UIW2 — SliderElement::SetParameter,
//                    ProgressElement::SetProgressTextParameter
// ============================================================================
// Full write-up, closure set-diff and HONEST LIMITS:
//   docs/history/runtime_port/WAVE9_LANE_UIW2.md
//
//   ?SetParameter@SliderElement@UI@@QEAAXHAEBVString@System@@@Z
//   ?SetProgressTextParameter@ProgressElement@UI@@QEAAXHAEBVString@System@@@Z
//       QEAA X  H  AEBVString@System@@  -> returns VOID (no hidden slot),
//       takes (this, int, const String&) = RCX, EDX, R8. Three integer slots,
//       all register-passed. The disassembly agrees: `MOVSXD RDI,EDX`
//       sign-extends the int, `MOV RSI,R8` keeps the reference.
//
// NO REPLAY ORACLE on either: both end in `System::String::operator=` on a slot
// of a CHILD object reached through a pointer a byte-for-byte scratch copy
// would share, so a replay would run the refcount acquire/release TWICE on the
// one live string.

// ---- ?SetParameter@SliderElement@UI@@QEAAXHAEBVString@System@@@Z  0x1800d6ca0
//
// The child is `_children._data[0]` cast through IsDerivedFrom against
// TextElement's type id — the same shape SpinnerElement's seven exports use.
void SliderElement::SetParameter(int32_t index, const System::String& text) {
    TextElement* child = static_cast<TextElement*>(
        FirstChildIfDerived(this, UI::TextElement::TypeId()));

    // Retail 0x1800d6ccf and 0x1800d6cd4 are TWO separate exits — `JZ` on the
    // cast result and then `JZ` on the pointer itself. The second is redundant
    // (a successful IsDerivedFrom implies non-null) and is transcribed anyway,
    // because it is what makes this function DIFFER from its Progress sibling
    // below. See that function's header.
    if (child == nullptr) return;
    if (index < 0) return;
    if (index >= child->_params._size) return;

    WrappedTextSetParam(child, index, text);
}

// ---- ?SetProgressTextParameter@ProgressElement@UI@@QEAAXHAEBVString@System@@@Z
//      0x180119b30
//
// THE SAME PROGRAM AS THE SLIDER ABOVE EXCEPT FOR ONE BRANCH, AND THAT
// DIFFERENCE IS THE INTERESTING PART OF THIS PORT.
//
//   Slider   0x1800d6ccd  TEST AL,AL   / JZ done   <- cast failed, bail
//            0x1800d6cd1  TEST RBX,RBX / JZ done   <- and bail on a null child
//   Progress 0x180119b62  XOR EDI,EDI
//            0x180119b66  CMOVNZ RDI,RBX          <- child = ok ? first : NULL
//            0x180119b6c  TEST ESI,ESI / JS done  <- only the NEGATIVE index exits
//            0x180119b6e  CMP ESI,dword ptr [RDI+0xd0]   <- READS THROUGH IT ANYWAY
//
// *** RETAIL HARD-FAULTS HERE WHERE THE SLIDER RETURNS QUIETLY. *** With a
// first child that is not a TextElement and a non-negative index,
// SetProgressTextParameter reads address 0x00000000000000d0 and dies. The
// CMOVNZ is not a null check; it is a cast, and retail then asserts the cast
// succeeded by dereferencing.
//
// This port REPRODUCES THAT, which means the load below is deliberately through
// a possibly-null pointer. It is spelled as byte arithmetic on a `uint8_t*`
// rather than `child->_params._size` so the compiler emits the load instead of
// concluding the pointer cannot be null and deleting the branch around it.
//
// DO NOT "FIX" THIS WITH A NULL CHECK. Retail's unguarded deref is the
// assertion that a ProgressElement's first child is a TextElement. If this
// faults, the thing to find is who built a ProgressElement whose first child
// is not a text widget.
void ProgressElement::SetProgressTextParameter(int32_t index, const System::String& text) {
    TextElement* child = static_cast<TextElement*>(
        FirstChildIfDerived(this, UI::TextElement::TypeId()));

    if (index < 0) return;
    // Faithful to 0x180119b6e — see the header. `child` may be null here.
    const int32_t count = child->_params._size;
    if (index >= count) return;

    WrappedTextSetParam(child, index, text);
}

}  // namespace UI
