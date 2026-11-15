#pragma once
// ============================================================================
// src/engine/ui/RadioDescription.h — UI::RadioDescription, the REAL C++ class
// ============================================================================
//
// ENDGAME B.3 step 6, block C2. Before this header the layout lived only as
// raw `+0x..` reads in src/engine/ui/radiodescription.cpp (RULE B6-8); they
// are MOVED here under the class.
//
// PARENT `UI::GroupDescription` — READ, not inferred (B36_SLOTS.md section 3):
//     ?_ParentTypeName@RadioDescription@UI@@CAPEBDXZ  @0x180047640 -> "GroupDescription"
// RULE B5-15 / B6-7 — the two literals, which DEFINE_TYPE (radiodescription.cpp) carries:
//     ?_TypeName@RadioDescription@UI@@CAPEBDXZ        @0x180047630 -> "RadioDescription"
//     ?_ParentTypeName@RadioDescription@UI@@CAPEBDXZ  @0x180047640 -> "GroupDescription"
// The derivation is also visible in the bytes: ~RadioDescription (0x1800d5b20)
// tail-jumps to ~GroupDescription and Serialize (0x1800d5a80) opens with
// GroupDescription::Serialize (0x180117d00).
//
// `??_7RadioDescription@UI@@6B@` @ 0x180157420, TEN slots:
//   [0] 0x180047930  (unexported) vector deleting dtor — the class's own (B.3.9)
//   [1] 0x180047670  TypeName
//   [2] 0x180047650  Type
//   [3] 0x1800d5b10  GetFileVersion  (`MOV EAX,0x1104`)
//   [4] 0x1800066e0  Object::External::Compile  INHERITED — NOT declared (B6-1.5)
//   [5] 0x1800d5a80  Serialize
//   [6] 0x1800477f0  Reconstruct
//   [7] 0x180047820  Destruct
//   [8] 0x1800d58d0  CreateInstance(const Dialog*) -> InternalPtr<Element>  RETSLOT (B6-2)
//   [9] 0x180047810  Construct       (override of GroupDescription's slot 9)
//
// ---------------------------------------------------------------------------
// LAYOUT — 0x80 bytes (`MOV EDX,0x80` at 0x180047962), GroupDescription's 0x78
// plus ONE dword and four bytes nothing writes.
// ---------------------------------------------------------------------------
// `??0RadioDescription@UI@@QEAA@XZ` (0x1800d5b50) inlines GroupDescription's
// nineteen stores and ends with `MOV dword [RCX+0x78],-1` at 0x1800d5bdf.
//   +0x78  _activeRadio  ctor -1; one kScalarInt in Serialize (0x1800d5a80);
//                        CreateInstance (0x1800d5a13..0x1800d5a1b) compares it
//                        with and stores it into `RadioElement::_activeIndex`
//                        (+0xd0), the field `?GetActiveRadio@RadioElement@UI@@`
//                        (0x1800d56e0, `MOV EAX,[RCX+0xd0]`) returns — the
//                        name is that accessor's.
//   +0x7c  _pad7c        written by nothing (alignment tail).
//
// (Until ENDGAME B.3.9:) ABSTRACT (slot 0 `= 0`), no real ctor: `??0RadioDescription` (0x1800d5b50)
// is the door `RtUi_RadioDescription_Ctor`; `Construct` and `Reconstruct`
// tail-jump into it.
//
// ENDGAME B.3 step 9 (block UI-A, B39_RULES D1/D2): CONCRETE. The real public
// ctor is retail's `??0RadioDescription@UI@@QEAA@XZ` (0x1800d5b50) minus its vptr stores;
// slot 0 is the class's own vector deleting dtor; ~RadioDescription is public and
// virtual. The factory pair is the class's own; the minted
// `??_7RadioDescription@UI@@6B@`, its RT_DELDTOR row, the factory-interpreter row and
// the doors are retired.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <type_traits>

#include "GroupDescription.h"
#include "../object/Internal.h"   // Object::detail::InternalMemFree

namespace UI {

class Dialog;
class Element;

class RadioDescription : public GroupDescription {
public:
    // ??0RadioDescription@UI@@QEAA@XZ 0x1800d5b50 — the REAL constructor:
    // GroupDescription's nineteen stores (inlined in retail), then
    // `MOV dword [RCX+0x78],-1` (0x1800d5bdf). +0x7c NOT written.
    RadioDescription() : GroupDescription() {
        _activeRadio = -1;
    }

    // ---- the ten slots, in measured order ---------------------------------
    // [0] 0x180047930 — MSVC's VECTOR deleting destructor, both arms exactly as
    // the retired RT_DELDTOR row ran them (`MOV EDX,0x80`, the element size).
    // `??1RadioDescription@UI@@UEAA@XZ` (0x1800d5b20). EMPTY
    // (radiodescription.cpp): restamp, then the tail JMP to ~GroupDescription,
    // which the compiler chains.
    ~RadioDescription() override;
    // ENDGAME B.3.9 TI-UI: ONE reflection block (B39 D6); `_typeInfo` is the
    // class's own record (DEFINE_TYPE in the class's TU), no longer minted.
    // retail: TypeName 0x180047670, Type 0x180047650, TypeId 0x180047660, _TypeName 0x180047630
    //         _ParentTypeName 0x180047640, CreateNewObject 0x180047760, CreateInPlaceObject 0x1800477e0
    DECLARE_TYPE(RadioDescription)
public:
    int32_t     GetFileVersion() const override;                  // [3] 0x1800d5b10
    // [4] Compile — Object::External's, inherited.
    bool        Serialize(IO::Stream& stream) override;                 // [5] 0x1800d5a80  (IO::Stream&)
    Object::External* Reconstruct() override;                     // [6] 0x1800477f0
    void        Destruct() override;                              // [7] 0x180047820
    Object::InternalPtr<Element>* CreateInstance(
        Object::InternalPtr<Element>* ret,
        const Dialog* dialog) const override;                     // [8] 0x1800d58d0  RETSLOT
    Object::External* Construct() override;                       // [9] 0x180047810

    // ---- exported, NOT PORTED as members ----------------------------------
    RadioDescription(const RadioDescription& src);                // 0x180047830  not ported
    RadioDescription& operator=(const RadioDescription& src);     // 0x1800478a0  not ported

private:

public:
    int32_t  _activeRadio;    // +0x78  ctor -1 (0x1800d5bdf)
    uint32_t _pad7c;          // +0x7c  written by nothing
};

static_assert(sizeof(RadioDescription) == 0x80,
              "UI::RadioDescription is 0x80 bytes (MOV EDX,0x80 at 0x180047962)");
static_assert(offsetof(RadioDescription, _pad7c) == 0x7C, "RadioDescription::_pad7c");
static_assert(alignof(RadioDescription) == 8, "UI::RadioDescription aligns to 8");
static_assert(std::has_virtual_destructor<RadioDescription>::value, "B3X W1: the normal-virtual cutover");
static_assert(!std::is_abstract<RadioDescription>::value, "B39 D1: concrete");
static_assert(offsetof(RadioDescription, _activeRadio) == sizeof(GroupDescription),
              "RadioDescription's own field begins at the end of GroupDescription");
static_assert(offsetof(RadioDescription, _activeRadio) == 0x78, "");

}  // namespace UI
