#pragma once
// ============================================================================
// src/engine/ui/Element.h — UI::Element, binary-compatible layout
// ============================================================================
//
// Wave 7 lane A wrote the layout contract below; ENDGAME B.3 step 6 block A
// turned `UI::Element` into the REAL C++ class (bottom half of this file) and
// moved the field-identical POD to `RtUI::Element`, which is what the seven
// leaf headers that embed an Element BY VALUE now spell (an abstract class
// cannot be a member — see the B.3.6 banner below).
//
// ---------------------------------------------------------------------------
// SIZE IS PROVEN, NOT INFERRED:  sizeof(UI::Element) == 0xC0 (192), align 8.
// ---------------------------------------------------------------------------
// Two independent proofs, from the shipped Runtime-x64.dll:
//
//  1. `UI::ContainerDescription::CreateInstance` (retail 0x1800ccb90) allocates
//     BOTH the ContainerElement and its ClipRegionElement with
//     `MemoryAllocator::Allocate(_singleton, 0xd8, 8)`, calls
//     `Element::Element` on each, and then writes the derived fields starting
//     at +0xC0 and ending at +0xD4.
//  2. `?GetWidth@ContainerElement@UI@@QEBAHXZ` (0x180039ee0) is one
//     instruction: `MOV EAX,[RCX+0xC0]`, and `?GetHeight@` (0x180039ef0) reads
//     +0xC4. The first field a derived class owns is at +0xC0, so the base
//     ends there.
//
//  Corroboration: `Element::Element` (0x180115e50) initialises fields up to and
//  including the qword at +0xB8 and stops; B36_SLOTS.md section 1 reads
//  `MOV EDX,0xc0` in the slot-0 vector deleting destructor 0x180035c10.
//
// ---------------------------------------------------------------------------
// THE VTABLE — TWELVE SLOTS (B36_SLOTS.md section 1, `.rdata` qwords)
// ---------------------------------------------------------------------------
//   slot  off    method                       retail       note
//   ----  -----  ---------------------------  -----------  ---------------------
//    0    0x00   vector deleting destructor   0x180035c10  called as fn(this, 1)
//    1    0x08   TypeName()                   0x180035440
//    2    0x10   Type()                       0x180035420  used by FindElementType
//    3    0x18   Persist(IO::Stream&)         0x180116870
//    4    0x20   ComputeSize()                _purecall     PURE in Element
//    5    0x28   ComputeLayout(x,y,w,h)       0x1801168d0
//    6    0x30   Draw(SpriteContext&, byte)   0x1801168f0
//    7    0x38   Update(...)                  0x180116270
//    8    0x40   Enable(bool)                 0x180116910
//    9    0x48   Contains(x,y,UserInterface*) 0x1801169e0
//   10    0x50   SetColor(uint)               0x180035620
//   11    0x58   GetBorder()  RETSLOT         0x180035630
//
// ---------------------------------------------------------------------------
// INHERITANCE:  Object::Base -> Object::Internal -> UI::Element
// ---------------------------------------------------------------------------
// `?_ParentTypeName@Element@UI@@CAPEBDXZ` (0x180035410) returns
// "Object::Internal" (B36_SLOTS.md section 3, read byte-wise).
// `Element::Element` calls `Object::Internal::Internal` (0x1800bfd10) first,
// which stamps the two base vtables, writes +0x08/+0x0C and REGISTERS THE
// OBJECT IN THE GLOBAL HANDLE TABLE.
//
// PORTABILITY: no <windows.h>, per PLAN §4.1 decision 1.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <type_traits>

#include "../collection/Collection.h"
// ::UIString — the 0x10-byte UI::String value type (block D folds it into
// `UI::String` and leaves `using UIString = UI::String;` at global scope, so
// the member below keeps spelling `::UIString`).
#include "String.h"
#include "../object/Internal.h"        // Object::Internal, Object::InternalPtr<T>
#include "ElementDescription.h"        // UI::ElementDescription::Alignment, ExternalPtr

namespace System { class String; class Time; }
namespace Input  { class KeyMap; }

namespace UI {
class Element;
class Dialog;              // block B
class DialogInstance;      // block B
struct SpriteContext;      // block D (a struct in the tree today)
struct UserInterface;      // block E (a struct in the tree today)
}  // namespace UI

// ############################################################################
// RtUI::Element — the Wave 7 POD, MOVED here from `UI::Element` by B.3.6
// block A (B4b-1). Field-for-field identical to the class below and
// cross-asserted against it at the bottom of this file.
//
// WHY A POD SURVIVES, contrary to "prefer one definition and an alias": when
// block A ran, seven leaf headers held `Element base;` BY VALUE, and
// `UI::Element` is ABSTRACT (slot 4 `ComputeSize` is `_purecall` in retail),
// which a data member cannot be. The C blocks turned those leaves into real
// classes deriving from `UI::Element`; as of the B.3.6 closing pass the
// `RtUI::` PODs that still embed `RtUI::Element base;` are FOUR:
// `EditElement.h`, `NumberElement.h`, `ProgressElement.h`, `SpinnerElement.h`
// (`ComboElement.h`, `RadioElement.h` and `SliderElement.h` no longer do). The
// pointer members name the REAL class, so `pod->_parent` and
// `pod->_children[i]` are `UI::Element*` and no caller needs a bridge to walk
// the tree from a POD. The POD dies with the last of those four.
// ############################################################################





namespace UI {

// ---------------------------------------------------------------------------
// Element::_flags (+0x34) — every bit here is named by the exported one-line
// predicate that reads it, or by the setter that writes it. Bits NOT listed are
// NOT guessed.
// ---------------------------------------------------------------------------
enum ElementFlag : uint32_t {
    // bit 0. `?IsVisible@` (0x1800354e0) is `NOT EAX; AND EAX,1`, so the bit
    // means HIDDEN, not visible. `?Hide@` (0x180035830) sets it when its
    // argument is true and clears it when false; `?Show@` (0x1800357e0) is the
    // mirror. It is also the ONE bit whose change triggers the parent dirty
    // walk in Hide / Show / SetFlagsOnOff / Enable / SetToolTip.
    kElementFlagHidden        = 0x00000001,

    // bit 1. Tested together with bit 0 by `?Contains@` (0x1801169e0):
    // `TEST [RCX+0x34],0x3 / JNZ -> return false`. Not named by any accessor.
    kElementFlagNoHitTest     = 0x00000002,

    // bits 2 and 3, both read by the member `?GetAlignedPosition@`
    // (0x180115ff0), each OR'd with one of the two incoming bool arguments
    // before being forwarded to the static overload. Sticky "stretch"
    // overrides; not named by any accessor.
    kElementFlagAlignArg1     = 0x00000004,
    kElementFlagAlignArg2     = 0x00000008,

    // bits 4 and 5, tested as the mask 0x30 by `?GetPaddedSize@`
    // (0x180115f10): when set, the padded width / height is replaced by
    // Video::Device::_singleton's screen width / height scaled by the root
    // dialog's UI scale.
    kElementFlagSizeToScreenH = 0x00000010,
    kElementFlagSizeToScreenW = 0x00000020,
    // the mask GetPaddedSize tests first: either SizeToScreen bit
    kElementFlagSizeToScreen  = 0x00000030,

    // bit 18, tested by `?ComputeColor@` (0x180035490): halves the incoming
    // alpha before modulating.
    kElementFlagDimColor      = 0x00040000,

    // bits 16 and 19, set together (0x90000) by `?SetToolTip@` (0x180035740)
    // when the new tooltip is valid; bit 19 alone is cleared (BTR 0x13) when it
    // is not. Bit 16 gates ALL of Element::Update's event steps (tooltip, hover
    // enter / leave, every press / release / drag / drop event:
    // element_vtable.cpp `BT 0x10 / JNC exit`), so it means the element raises
    // events (it was once named kElementFlagToolTip16).
    kElementFlagRaisesEvents  = 0x00010000,
    kElementFlagToolTipValid  = 0x00080000,

    // bit 17. Element::Update sets it (BTS 0x11) while the mouse is inside the
    // element's (or its border's) rect and the clip region, clears it (BTR
    // 0x11) when it leaves; the widgets test it before reacting to the mouse.
    kElementFlagHovered       = 0x00020000,

    // bit 20. Element::Update sets it (BTS 0x14) when it raises the tooltip
    // BEGIN event; while set the next frames raise MOVE, leaving clears it
    // (BTR 0x14).
    kElementFlagToolTipShown  = 0x00100000,

    // bit 21. A left press started while hovered: the button / check / combo
    // click arming, the slider / scroll thumb drag.
    kElementFlagPressed       = 0x00200000,

    // bit 22. `?WasActivated@` (0x1800354f0).
    kElementFlagWasActivated  = 0x00400000,

    // bit 23. `?IsDisabled@` (0x180035520), and the bit `?Enable@`
    // (0x180116910, vtable slot 8) sets/clears. `?IsToolTipValid@`
    // (0x180035640) also refuses a tooltip while it is set.
    kElementFlagDisabled      = 0x00800000,

    // bit 24. `?IsSystemDisabled@` (0x180035510) reads BYTE [RCX+0x37] & 1.
    // `?IsEnabled@` (0x180035500) is `TEST [RCX+0x34],0x1800000; SETZ` — i.e.
    // enabled means NEITHER bit 23 nor bit 24.
    kElementFlagSystemDisabled = 0x01000000,
    // `?IsEnabled@`'s mask: Disabled | SystemDisabled.
    kElementFlagAnyDisabled   = 0x01800000,

    // bit 27. `?NeedsSizeCalc@` (0x180035460) is `SHR EAX,0x1b; AND AL,1`.
    // Element::Element sets it, and `?SetNeedsSizeCalc@` (0x180035470) ORs it
    // into this element AND every ancestor through +0x98.
    kElementFlagNeedsSizeCalc = 0x08000000,
};

// ############################################################################
// ENDGAME B.3 step 6 (block A) — `UI::Element`, the REAL C++ class
// ############################################################################
//
// PARENT `Object::Internal` — READ, not inferred:
//     ?_ParentTypeName@Element@UI@@CAPEBDXZ  @0x180035410 -> "Object::Internal"
//
// RULE B5-15 / B6-7 — THE TWO NAME LITERALS, COPIED FROM B36_SLOTS.md SECTION 3
// AND RECORDED, NOT USED. No `DEFINE_TYPE` is introduced in step 6 (B36_RULES
// section 7); slots 1/2 keep reading `?_typeInfo@Element@UI@@…` (0x1801fab30).
//     ?_TypeName@Element@UI@@CAPEBDXZ        @0x180035400 -> "Element"
//     ?_ParentTypeName@Element@UI@@CAPEBDXZ  @0x180035410 -> "Object::Internal"
//
// ABSTRACT, exactly as retail: slot 4 `ComputeSize` is `_purecall`
// (0x18007ec28) in `??_7Element@UI@@6B@`, and every one of the 27 subclasses
// overrides or inherits an override (RULE B6-1.3). Every live Element is one
// of those subclasses, built by its real constructor and wearing its COMPILER
// table (ENDGAME B.3.9 UI-D retired the minted `??_7Element@UI@@6B@`, its
// RT_DELDTOR row, the ??0 / ??1 doors and the slot doors).
//
// REAL CONSTRUCTOR AND DESTRUCTOR (ENDGAME B.3.8 block RB): protected; their
// bodies are the private `InitFields` / `ReleaseFields`. The destructor is
// Virtual (B3X W1). Slot 0 is the class's own vector deleting dtor
// (B9-4), reached only through a subclass that does not declare one.
//
// TWO SLOTS THAT PASS OR RETURN A CLASS, AND WHY BOTH TAKE A POINTER:
//   * slot 11 `GetBorder` returns `InternalPtr<Element>` BY VALUE through a
//     hidden return slot (RULE B6-2). GCC's ms_abi puts the hidden pointer
//     BEFORE `this`, MSVC after, so it is declared with the out-parameter in
//     RETAIL order: `InternalPtr<Element>* GetBorder(InternalPtr<Element>* ret)`.
//   * slot 7 `Update` takes `InternalPtr<DialogInstance>` BY VALUE, and retail
//     passes it as a POINTER TO THE CALLER'S TEMPORARY: `Element::Update`'s own
//     child loop does `MOV RAX,[R12] / LEA R9,[RBP+0x50] / MOV [RBP+0x50],RAX /
//     CALL [RAX+0x38]` (0x1801162c6..0x1801162e0). GCC passes the trivially
//     copyable 8-byte `Object::InternalPtr<T>` by VALUE in R9 (measured at -O2:
//     the callee reads the handle itself, not through it). Declared by value,
//     slot 7 would read the caller's temporary ADDRESS as the dialog. So it is
//     spelled `InternalPtr<DialogInstance>* dialog` — the retail register
//     contents. (B36_RULES section 2.2 and the block prompt spell it by value;
//     that is the MSVC source signature, not the ABI.)
//
// B6-5: nothing here is a static initialiser or a lazy cache. The door-
// resolving caches the member bodies need are namespace-scope std::atomic
// slots in element.cpp / element_vtable.cpp (rtuielem::AtomicExport).
// ############################################################################
class Element : public Object::Internal {
public:
    // ---- Element::UpdateState — the value type slot 7 receives -------------
    // `??0UpdateState@Element@UI@@QEAA@XZ` (0x180115600), all seven stores:
    //     MOV qword [RCX],0 ; MOV word [RCX+0x8],0 ; MOV qword [RCX+0xc],0
    //     MOV byte [RCX+0x20],0 ; MOV qword [RCX+0x18],-1
    // `??4UpdateState@Element@UI@@QEAAAEAU012@AEBU012@@Z` (0x180035880) copies
    // SIX QWORDS, +0x00..+0x2f, so sizeof is 0x30 and alignof 8 — the ctor's
    // last store (+0x20) is not the end of the object. Field names are lane
    // UI1's (userinterface_frame.cpp, which pinned the same 0x30 bytes from the
    // frame of `UserInterface::Update`); `Element::Update` reads +0x00..+0x07,
    // +0x0c, +0x10, +0x18 and +0x1c. +0x14 and +0x28 are NOT written by the
    // ctor; +0x0a/+0x0b and +0x21..+0x27 are written by neither body.
    struct UpdateState {
        uint8_t  dragActive;      // +0x00  = (_dragDropDialog != 0)
        uint8_t  buttonPressed;   // +0x01  -> event 2
        uint8_t  middlePressed;             // +0x02  -> event 4
        uint8_t  rightPressed;             // +0x03  -> event 3
        uint8_t  leftReleased;             // +0x04  -> event 5 / the drop
        uint8_t  middleReleased;             // +0x05  -> event 7
        uint8_t  rightReleased;             // +0x06  -> event 6
        uint8_t  leftDown;        // +0x07
        uint8_t  rightDown;             // +0x08  ctor 0 (word store with +0x09)
        uint8_t  middleDown;             // +0x09
        uint8_t  pad0a[2];        // +0x0a  written by nothing
        int32_t  dragDx;          // +0x0c  ctor 0 (qword store with +0x10)
        int32_t  dragDy;          // +0x10
        int32_t  wheel;           // +0x14  NOT written by the ctor
        int32_t  mouseX;          // +0x18  ctor -1 (qword store with +0x1c)
        int32_t  mouseY;          // +0x1c
        uint8_t  mouseCaptured;   // +0x20  ctor 0
        uint8_t  pad21[7];        // +0x21  written by nothing
        ::Input::KeyMap* config;  // +0x28  NOT written by the ctor; = Input::Configuration::_singleton->_keyMap (B4 P2 H4)

        // 0x180115600, transcribed store for store; the bytes it does not
        // write stay unwritten (no member initialisers).
        UpdateState() {
            dragActive = buttonPressed = middlePressed = rightPressed = leftReleased = middleReleased = rightReleased = leftDown = 0;
            rightDown = middleDown = 0;
            dragDx = dragDy = 0;
            mouseCaptured = 0;
            mouseX = mouseY = -1;
        }
        // 0x180035880 — six qword moves: the trivial copy.
        UpdateState& operator=(const UpdateState&) = default;
        UpdateState(const UpdateState&) = default;
    };

    // ---- the twelve slots, in measured order ------------------------------
    // [0] 0x180035c10 — MSVC's VECTOR deleting destructor (retail; here
    // the virtual destructor, B3X W1) (`MOV EDX,0xc0`, the element size).
    // `??1Element@UI@@UEAA@XZ` (0x1801161a0). VIRTUAL (B3X W1); the compiler
    // restamps on entry and chains ~Array (a no-op on the header the body
    // zeroes) and ~Internal (retail's tail JMP) after it.
    ~Element() override { ReleaseFields(); }
    // ENDGAME B.3.9 TI-UI: ONE reflection block (B39 D6); `_typeInfo` is the
    // class's own record (DEFINE_TYPE in the class's TU), no longer minted.
    // retail: TypeName 0x180035440, Type 0x180035420, TypeId 0x180035430, _TypeName 0x180035400
    //         _ParentTypeName 0x180035410, CreateNewObject 0x180035450
    DECLARE_TYPE_INHERITED_INPLACE(Element)
public:
    void        Persist(IO::Stream& stream) override;                   // [3] 0x180116870  (IO::Stream&)
    virtual void ComputeSize() = 0;                               // [4] _purecall
    virtual void ComputeLayout(int32_t x, int32_t y,
                               int32_t w, int32_t h);             // [5] 0x1801168d0
    virtual void Draw(SpriteContext& context, uint8_t alpha) const; // [6] 0x1801168f0
    virtual void Update(const System::Time& time, UserInterface* ui,
                        Object::InternalPtr<DialogInstance>* dialog,
                        const UpdateState& state);                // [7] 0x180116270 — see banner
    virtual void Enable(bool enable);                             // [8] 0x180116910
    virtual bool Contains(int32_t x, int32_t y,
                          UserInterface* ui) const;               // [9] 0x1801169e0
    virtual void SetColor(uint32_t color);                        // [10] 0x180035620
    virtual Object::InternalPtr<Element>* GetBorder(
        Object::InternalPtr<Element>* ret) const;                 // [11] 0x180035630 RETSLOT

    // ---- PORTED non-virtual members (bodies in element.cpp) ----------------
    // Each keeps its registered door, now a QUALIFIED delegation. The retslot
    // ones return by value here (both ends of a C++ call are this GCC); the
    // door keeps retail's `(this, ret, …)` shape and placement-news the result.
    // `V?$InternalPtr` BY VALUE arrives by HIDDEN POINTER (RULE B6-11): retail
    // 0x180115df0 opens `MOV RAX,[RDX]` and hands that same RDX to SetSizeFill
    // as the fill (`MOV R8,RDX`), so the member takes the pointer.
    void AddChild(Object::InternalPtr<Element>* child);           // 0x180115df0
    void AdjustUpperLeft(int32_t dx, int32_t dy);                 // 0x180115840
    Object::InternalPtr<Element> Child(int32_t index);            // 0x180035540 RETSLOT (non-const)
    int32_t ChildCount() const;                                   // 0x180035530
    Object::InternalPtr<Element> FindElement(const System::String& name);  // 0x180115650 RETSLOT
    Object::InternalPtr<Element> FindElementType(uint32_t typeId);         // 0x180115760 RETSLOT
    void GetAlignedPosition(int32_t x, int32_t y, int32_t w, int32_t h,
                            int32_t offsetX, int32_t offsetY,
                            int32_t& outX, int32_t& outY,
                            int32_t& outW, int32_t& outH,
                            bool stretchX, bool stretchY);        // 0x180115ff0
    // THE STATIC 17-ARGUMENT OVERLOAD — the door decides which one a caller
    // meant (`…QEAAXHHHHHHAEAH000_N1@Z` vs `…SAXW4Alignment@…`).
    static void GetAlignedPosition(ElementDescription::Alignment alignment,
                                   int32_t x, int32_t y,
                                   int32_t containerW, int32_t containerH,
                                   int32_t offsetX, int32_t offsetY,
                                   int32_t leftPad, int32_t rightPad,
                                   int32_t topPad, int32_t bottomPad,
                                   int32_t& outX, int32_t& outY,
                                   int32_t& outW, int32_t& outH,
                                   bool stretchX, bool stretchY); // 0x1801158e0
    void GetPaddedSize(int32_t& outW, int32_t& outH);             // 0x180115f10
    Object::Base* GetUserData();                                  // 0x180115550
    void Hide(bool hide);                                         // 0x180035830
    void SetElementId(int32_t id);                                // 0x180115820
    void SetFlagsOnOff(uint32_t on, uint32_t off);                // 0x180116160
    void SetName(const System::String& name);                     // 0x1801154f0
    void SetNeedsSizeCalc();                                      // 0x180035470
    void SetPadding(int32_t left, int32_t right,
                    int32_t top, int32_t bottom);                 // 0x180115480 (left, RIGHT, TOP, bottom)
    void SetToolTip(::UIString tip);                              // 0x180035740
    void SetToolTipString(const System::String& text);            // 0x1800356e0
    void SetUserData(Object::Base* userData);                     // 0x180115560

    // ---- PORTED; DEFINED in another block's file (moved there by the
    // fragment pass), whose registered door delegates to the member.
    void RemoveChildren();                                        // 0x180115570  widgets_layout.cpp
    System::String GetToolTipString() const;                      // 0x1800356a0  tooltipdialog.cpp (RETSLOT)

    // ---- one-instruction accessors, from the disassembly quoted on each
    // field of RtUI::Element above. Exported, registered nowhere; inline
    // because each is the whole body.
    int32_t  GetLeftPad() const      { return _leftPad; }         // 0x1800355e0
    int32_t  GetRightPad() const     { return _rightPad; }        // 0x180035600
    int32_t  GetTopPad() const       { return _topPad; }          // 0x1800355f0
    int32_t  GetBottomPad() const    { return _bottomPad; }       // 0x180035610
    int32_t  GetLayoutWidth() const  { return _layoutWidth; }     // 0x1800355c0
    int32_t  GetLayoutHeight() const { return _layoutHeight; }    // 0x1800355d0
    int32_t  GetLayoutLeft() const   { return _layoutLeft; }      // 0x180035580
    int32_t  GetLayoutTop() const    { return _layoutTop; }       // 0x1800355a0
    int32_t  GetLayoutRight() const  { return _layoutRight; }     // 0x180035590
    int32_t  GetLayoutBottom() const { return _layoutBottom; }    // 0x1800355b0
    uint32_t GetElementFlags() const { return _flags; }           // 0x1800354d0
    int32_t  GetMouseCursorIndex() const { return _mouseCursorIndex; } // 0x180115740
    int32_t  GetElementId() const    { return _elementId; }       // 0x180115830
    bool IsVisible() const  { return (_flags & kElementFlagHidden) == 0; }            // 0x1800354e0
    bool IsDisabled() const { return (_flags & kElementFlagDisabled) != 0; }          // 0x180035520
    bool IsSystemDisabled() const { return (_flags & kElementFlagSystemDisabled) != 0; } // 0x180035510
    bool IsEnabled() const  { return (_flags & (kElementFlagDisabled | kElementFlagSystemDisabled)) == 0; } // 0x180035500
    bool WasActivated() const { return (_flags & kElementFlagWasActivated) != 0; }   // 0x1800354f0
    bool NeedsSizeCalc() const { return (_flags & kElementFlagNeedsSizeCalc) != 0; } // 0x180035460

    // ---- exported, NOT PORTED anywhere in the tree: declared, deliberately
    // undefined (nothing in the tree calls these through a member).
    Object::InternalPtr<Element> Child(int32_t index) const;     // 0x180035560 RETSLOT (const)
    uint32_t ComputeColor(uint32_t color, uint8_t& alpha) const;  // 0x180035490
    Object::ExternalPtr<ElementDescription> GetDescription() const; // 0x1800357d0 RETSLOT
    ::UIString GetToolTip() const;                                // 0x180035680 RETSLOT
    Object::ExternalPtr<Dialog> GetToolTipDialog() const;         // 0x180035670 RETSLOT
    bool IsToolTipValid() const;                                  // 0x180035640
    void SetAlignment(ElementDescription::Alignment alignment);   // 0x180115750
    void SetDialogParent(Object::InternalPtr<DialogInstance> dialog); // 0x180115630
    void SetMinSize(int32_t w, int32_t h);                        // 0x1801154a0
    void SetParent(Object::InternalPtr<Element> parent);          // 0x1801154d0
    void Show(bool show);                                         // 0x1800357e0

    Element(const Element& src);                                  // 0x1800358c0  not ported
    Element& operator=(const Element& src);                       // 0x180035a50  not ported

    // Reflection statics (retail's DECLARE_TYPE block). `_typeInfo` is still the
    // MINTED record TypeId reads by name.
    // Abstract: retail's factory is `xor eax,eax; ret` (element.cpp).

protected:
    // `??0Element@UI@@QEAA@XZ` (0x180115e50) — the REAL constructor: the
    // compiler stamps Base -> Internal -> Element where retail's `CALL
    // ??0Internal` and `??_7Element` store are; the body is `InitFields`.
    Element() { InitFields(); }

private:
    void InitFields();      // 0x180115e50 minus ??0Internal + vptr store (element.cpp)
    void ReleaseFields();   // 0x1801161a0 minus restamp + ~Internal tail (element.cpp)


public:
    // ---- the 0xB0 bytes Element adds after Object::Internal -----------------
    // PUBLIC for the reason `Object::Internal`'s are: unconverted bodies reach
    // them at these offsets. Names and TYPES are RtUI::Element's, unchanged, so
    // every includer of this header compiles untouched; the per-field evidence
    // is on the POD above.
    char*    _name;              // +0x10  System::String body (H5: raw char* storage)
    int32_t  _leftPad;           // +0x18
    int32_t  _rightPad;          // +0x1c
    int32_t  _topPad;            // +0x20
    int32_t  _bottomPad;         // +0x24
    int32_t  _minWidth;          // +0x28
    int32_t  _minHeight;         // +0x2c
    int32_t  _alignment;         // +0x30  ElementDescription::Alignment
    uint32_t _flags;             // +0x34  ElementFlag
    uint32_t _color;             // +0x38
    int32_t  _mouseCursorIndex;  // +0x3c
    Dialog*  _toolTipDialog;     // +0x40  ExternalPtr<UI::Dialog> (B4 H4: typed)
    ::UIString _toolTip;         // +0x48
    char*    _toolTipString;     // +0x58  System::String body (H5: raw char* storage)
    int32_t  _layoutWidth;       // +0x60  NOT written by the ctor
    int32_t  _layoutHeight;      // +0x64  NOT written by the ctor
    int32_t  _layoutLeft;        // +0x68
    int32_t  _layoutTop;         // +0x6c
    int32_t  _layoutRight;       // +0x70
    int32_t  _layoutBottom;      // +0x74
    int32_t  _unreadIndex;         // +0x78  ctor -1
    uint32_t _pad7c;             // +0x7c  never written
    Object::Base* _userData;     // +0x80  ?GetUserData@ 0x180115550 (B4 H4: typed)
    int32_t  _elementId;         // +0x88  ctor -1
    uint32_t _pad8c;             // +0x8c  never written
    DialogInstance* _dialogParent; // +0x90  InternalPtr<UI::DialogInstance> (B4 H4: typed)
    Element* _parent;            // +0x98
    Collection::Array<Element*> _children;   // +0xa0
    Element* _border;            // +0xb0  InternalPtr<UI::Element> (B4 P2 H4: typed)
    const ElementDescription* _description; // +0xb8  ExternalPtr<UI::ElementDescription>; CreateInstance 0x180115ad0 stores its const `this` (B4 H4)
};

static_assert(sizeof(Element) == 0xC0,
              "UI::Element is 0xC0 bytes (MOV EDX,0xc0 in its slot-0 vector "
              "deleting destructor 0x180035c10)");
static_assert(offsetof(Element, _pad7c) == 0x7C, "Element::_pad7c");
static_assert(offsetof(Element, _pad8c) == 0x8C, "Element::_pad8c");
static_assert(alignof(Element) == 8, "UI::Element aligns to 8 (the vptr)");
static_assert(sizeof(Element::UpdateState) == 0x30,
              "UpdateState is 0x30 (operator= 0x180035880 copies six qwords)");
static_assert(offsetof(Element::UpdateState, dragActive) == 0x0, "UpdateState::dragActive");
static_assert(offsetof(Element::UpdateState, buttonPressed) == 0x1, "UpdateState::buttonPressed");
static_assert(offsetof(Element::UpdateState, middlePressed) == 0x2, "UpdateState::middlePressed");
static_assert(offsetof(Element::UpdateState, rightPressed) == 0x3, "UpdateState::rightPressed");
static_assert(offsetof(Element::UpdateState, leftReleased) == 0x4, "UpdateState::leftReleased");
static_assert(offsetof(Element::UpdateState, middleReleased) == 0x5, "UpdateState::middleReleased");
static_assert(offsetof(Element::UpdateState, rightReleased) == 0x6, "UpdateState::rightReleased");
static_assert(offsetof(Element::UpdateState, middleDown) == 0x9, "UpdateState::middleDown");
static_assert(offsetof(Element::UpdateState, pad0a) == 0xA, "UpdateState::pad0a");
static_assert(offsetof(Element::UpdateState, pad21) == 0x21, "UpdateState::pad21");
static_assert(alignof(Element::UpdateState) == 8, "UpdateState aligns to 8");
static_assert(std::is_trivially_copyable<Element::UpdateState>::value,
              "operator= 0x180035880 is a plain six-qword copy");
static_assert(offsetof(Element::UpdateState, leftDown)      == 0x07, "");
static_assert(offsetof(Element::UpdateState, rightDown)           == 0x08, "");
static_assert(offsetof(Element::UpdateState, dragDx)        == 0x0c, "");
static_assert(offsetof(Element::UpdateState, dragDy)        == 0x10, "");
static_assert(offsetof(Element::UpdateState, wheel)         == 0x14, "");
static_assert(offsetof(Element::UpdateState, mouseX)        == 0x18, "");
static_assert(offsetof(Element::UpdateState, mouseY)        == 0x1c, "");
static_assert(offsetof(Element::UpdateState, mouseCaptured) == 0x20, "");
static_assert(offsetof(Element::UpdateState, config)        == 0x28, "");

// The retail offsets, measured on the class (the Wave 7 set, unchanged).
static_assert(offsetof(Element, _factoryIndex)     == 0x08, "Internal::_factoryIndex (POD _handleIndex)");
static_assert(offsetof(Element, _generation)       == 0x0c, "Internal::_generation (POD _handleId)");
static_assert(offsetof(Element, _name)             == 0x10, "");
static_assert(offsetof(Element, _leftPad)          == 0x18, "");
static_assert(offsetof(Element, _rightPad)         == 0x1c, "");
static_assert(offsetof(Element, _topPad)           == 0x20, "");
static_assert(offsetof(Element, _bottomPad)        == 0x24, "");
static_assert(offsetof(Element, _minWidth)         == 0x28, "");
static_assert(offsetof(Element, _minHeight)        == 0x2c, "");
static_assert(offsetof(Element, _alignment)        == 0x30, "");
static_assert(offsetof(Element, _flags)            == 0x34, "");
static_assert(offsetof(Element, _color)            == 0x38, "");
static_assert(offsetof(Element, _mouseCursorIndex) == 0x3c, "");
static_assert(offsetof(Element, _toolTipDialog)    == 0x40, "");
static_assert(offsetof(Element, _toolTip)          == 0x48, "");
static_assert(offsetof(Element, _toolTipString)    == 0x58, "");
static_assert(offsetof(Element, _layoutWidth)      == 0x60, "");
static_assert(offsetof(Element, _layoutHeight)     == 0x64, "");
static_assert(offsetof(Element, _layoutLeft)       == 0x68, "");
static_assert(offsetof(Element, _layoutTop)        == 0x6c, "");
static_assert(offsetof(Element, _layoutRight)      == 0x70, "");
static_assert(offsetof(Element, _layoutBottom)     == 0x74, "");
static_assert(offsetof(Element, _unreadIndex)        == 0x78, "");
static_assert(offsetof(Element, _userData)         == 0x80, "");
static_assert(offsetof(Element, _elementId)        == 0x88, "");
static_assert(offsetof(Element, _dialogParent)     == 0x90, "");
static_assert(offsetof(Element, _parent)           == 0x98, "");
static_assert(offsetof(Element, _children)         == 0xa0, "");
static_assert(offsetof(Element, _border)           == 0xb0, "");
static_assert(offsetof(Element, _description)      == 0xb8, "");
static_assert(offsetof(Element, _name) == sizeof(Object::Internal),
              "Element's own fields begin at the end of the Object::Internal base");

// The class and the POD describe the same bytes.

} // namespace UI
