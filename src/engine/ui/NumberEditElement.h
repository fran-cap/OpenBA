#pragma once
// ============================================================================
// src/engine/ui/NumberEditElement.h — UI::NumberEditElement layout
// ============================================================================
// Wave 7 lane D. NumberEditElement extends EditElement with exactly two proven
// fields:
//
//   +0xF8  _min  int32   ?SetMinMax@NumberEditElement@UI@@QEAAXHH@Z (0x1800d4b70) arg 1
//                        ?GetMinValue@ 0x180046440 = MOV EAX,[RCX+0xf8]
//   +0xFC  _max  int32   ?SetMinMax@ arg 2; ?GetMaxValue@ 0x180046430 = MOV EAX,[RCX+0xfc]
//
// ENDGAME B.3 step 6, block C3: this header used to pad from a compiler-sized
// `sizeof(EditElement) == 0xE8` up to +0xF8. Retail's EditElement is 0xF8
// (EditElement.h), so `_min` begins EXACTLY at the end of EditElement and the
// pad is gone. Size 0x100 (`MOV EDX,0x100` in the deleting dtor 0x180046540).
//
// ENDGAME B.3 step 9 (block UI-A, B39_RULES D1/D2): CONCRETE. The real public
// ctor is retail's `??0NumberEditElement@UI@@QEAA@XZ` (0x1800d4c70) minus its vptr stores;
// slot 0 is the class's own vector deleting dtor; ~NumberEditElement is public and
// virtual. The factory pair is the class's own; the minted
// `??_7NumberEditElement@UI@@6B@`, its RT_DELDTOR row, the factory-interpreter row and
// the doors are retired.
// ============================================================================

#include <type_traits>
#include <cstdint>
#include <cstddef>
#include "EditElement.h"
#include "../object/Internal.h"   // Object::detail::InternalMemFree

// ############################################################################
// ENDGAME B.3 step 6 (block C3) — `UI::NumberEditElement`, the REAL C++ class
// ############################################################################
//
// PARENT `UI::EditElement` — READ (B36_SLOTS.md section 3), RECORDED; DEFINE_TYPE in numbereditelement.cpp carries both:
//     ?_TypeName@NumberEditElement@UI@@CAPEBDXZ       @0x180046210 -> "NumberEditElement"
//     ?_ParentTypeName@NumberEditElement@UI@@CAPEBDXZ @0x180046220 -> "EditElement"
//
// `??_7NumberEditElement@UI@@6B@` @ 0x180156f70, THIRTEEN slots: [0] 0x180046540
// · [1] 0x180046250 TypeName · [2] 0x180046230 Type · [3..11] INHERITED
// (4..7 are EditElement's own bodies) · [12] 0x1800d4a60 FinalizeText — the
// override.
//
// CONCRETE since ENDGAME B.3.9 UI-A: the real ctor is ??0NumberEditElement
// (0x1800d4c70), also what NumberEditDescription::CreateInstance inlines.
// ############################################################################
namespace UI {

class NumberEditElement : public EditElement {
public:
    // ??0NumberEditElement@UI@@QEAA@XZ 0x1800d4c70 — the REAL constructor:
    // CALL ??0EditElement (EditElement()), then _min 0xffff8001, _max 0x8000.
    NumberEditElement() : EditElement() {
        _min = static_cast<int32_t>(0xffff8001u);     // -32767
        _max = 0x8000;                                //  32768
    }

    // [0] 0x180046540 — MSVC's VECTOR deleting destructor, both arms exactly as
    // the retired RT_DELDTOR row ran them (`MOV EDX,0x100`, the element size).
    // `??1NumberEditElement@UI@@UEAA@XZ` (0x1800d4c40). EMPTY
    // (numbereditelement.cpp): restamp, then the tail into ~EditElement, which
    // the compiler chains.
    ~NumberEditElement() override;
    // ENDGAME B.3.9 TI-UI: ONE reflection block (B39 D6); `_typeInfo` is the
    // class's own record (DEFINE_TYPE in the class's TU), no longer minted.
    // retail: TypeName 0x180046250, Type 0x180046230, TypeId 0x180046240, _TypeName 0x180046210
    //         _ParentTypeName 0x180046220, CreateNewObject 0x180046340, CreateInPlaceObject 0x1800463e0
    DECLARE_TYPE(NumberEditElement)
public:
    void FinalizeText() override;                                // [12] 0x1800d4a60

    // ---- non-virtual ---------------------------------------------------------
    int32_t GetMaxValue() const { return _max; }                 // 0x180046430
    int32_t GetMinValue() const { return _min; }                 // 0x180046440
    // The three bodies below are DEFINED in widgets_input.cpp (fragment pass).
    int32_t GetValue() const;                                    // 0x1800d4b50
    void    SetMinMax(int32_t minValue, int32_t maxValue);       // 0x1800d4b70
    void    SetValue(int32_t value);                             // 0x1800d4bb0

    NumberEditElement(const NumberEditElement& src);             // 0x180046450  not ported
    NumberEditElement& operator=(const NumberEditElement& src);  // 0x1800464b0  not ported
    static void* operator new(std::size_t bytes);                // 0x180046260  not ported
    static void* operator new(std::size_t bytes, void* place);   // 0x1800462d0  not ported
    static void  operator delete(void* p) { ::Object::detail::InternalMemFree(p); }                       // 0x1800462e0

private:

public:
    int32_t _min;   // +0xf8  ctor 0xffff8001 (-32767)
    int32_t _max;   // +0xfc  ctor 0x8000
};

static_assert(sizeof(NumberEditElement) == 0x100,
              "UI::NumberEditElement is 0x100 (MOV EDX,0x100 in 0x180046540)");
static_assert(alignof(NumberEditElement) == 8, "UI::NumberEditElement aligns to 8");
static_assert(std::has_virtual_destructor<NumberEditElement>::value, "B3X W1: the normal-virtual cutover");
static_assert(!std::is_abstract<NumberEditElement>::value, "B39 D1: concrete");
static_assert(offsetof(NumberEditElement, _min) == sizeof(EditElement),
              "_min begins exactly at the end of the 0xf8-byte EditElement");
static_assert(offsetof(NumberEditElement, _min) == 0xf8, "");
static_assert(offsetof(NumberEditElement, _max) == 0xfc, "");

}  // namespace UI
