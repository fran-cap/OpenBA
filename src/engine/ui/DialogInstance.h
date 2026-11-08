#pragma once
// ============================================================================
// src/engine/ui/DialogInstance.h — UI::DialogInstance + UI::DialogController
// ============================================================================
//
// Wave 7 lane B (docs/history/runtime_port/WAVE7_LANE_B.md).
//
// `UI::DialogInstance` is one LIVE dialog: the runtime counterpart of the
// resource-side `UI::Dialog` (Dialog.h). It owns an Element tree root, an array
// of `UI::DialogController`s, an array of timers, and a linked list of child
// dialogs. Every exe dialog class (MainMenuDialog, ModDialog, ToolUI, …)
// derives from it, which is why its layout is load-bearing far outside this
// lane.
//
// Inheritance:  Object::Base -> Object::Internal -> UI::DialogInstance
//   `??_7DialogInstance@UI@@6B@` is EXPORTED, at 0x180156d70 (ordinal 1420).
//
// `UI::DialogController` is in this header rather than one of its own because
// it has no independent existence: it is created, owned, iterated and destroyed
// by `DialogInstance`, and every field of it below was pinned from a
// `DialogInstance` method.
//
// ---------------------------------------------------------------------------
// SIZE — SETTLED AT 0xb8 (ENDGAME B.3.6 block B)
// ---------------------------------------------------------------------------
// The "0xc0 upper bound" hedge that stood here is closed by two independent
// witnesses: the `MOV EDX,0xb8` in the class's own slot-0 vector deleting
// destructor (0x1800458c0, B36_SLOTS.md section 1), and the 20 exe
// DialogInstance subclasses in engine/types/ui/Dialogs.h that all start their
// own members at +0xb8. `UI::ToolTipDialog` is 0xc0 because it ADDS two dwords
// at +0xb8/+0xbc (its copy ctor 0x18004d180 copies exactly those) — it is not
// the base's size. `UI::UserInterface::BeginToolTip` (0x180120860) allocates
// that 0xc0 and runs the BASE ctor on it, then stamps ToolTipDialog's table.
//
// What the replay oracle in dialog.cpp compares is
// `kDialogInstanceCtorExtent` = 0xb4 — the ctor-initialised prefix — and the
// reason that is safe is spelled out there: the scratch copy is a memcpy of the
// LIVE object, so the padding holes below hold identical bytes on both sides
// and are not "bytes neither side wrote"
// (reference_oracle_must_not_compare_unwritten_bytes.md).
//
// THE BASE CTOR LEAVES FOUR HOLES UNINITIALISED, listed here because a future
// oracle that builds a DialogInstance from scratch MUST skip them:
//   +0x4c..0x4f   LinkedList's tail padding (Collection.h calls it `_pad14`)
//   +0x64..0x67   after the int32 at +0x60
//   +0x99..0x9f   after the bool at +0x98
//   +0xb4..        after the int32 at +0xb0
//
// ---------------------------------------------------------------------------
// EVIDENCE FOR EVERY OFFSET
// ---------------------------------------------------------------------------
// Retail addresses from the export directory of the shipped
// C:/GOG Games/Banished/Runtime-x64.dll, read with pefile before any body was
// written.
//
//   off   field              proven by                                  retail
//   ----  -----------------  -----------------------------------------  -----------
//   0x00  Object::Internal   ctor calls Object::Internal::Internal
//                            first, then stamps ??_7DialogInstance      0x180114870
//   0x10  _ui                ctor param 1 (UserInterface*)              0x180114870
//   0x18  _dialog            ctor param 2 (Dialog const*)               0x180114870
//   0x20  _flags             ctor seeds it from _dialog->+0x40;
//                            ?SetFlagsOnOff@ is `(_flags|on) & ~off`    0x180114690
//   0x24  _result            THE MODAL RESULT CODE (was misnamed `_flags2`
//                            until B.3.6 block B renamed it).
//                            Wave 10B lane SHOW: `?GetResult@DialogInstance@UI@@QEBAHXZ`
//                            (0x1800455c0) is `MOV EAX,[RCX+0x24]; RET` and
//                            `?SetDialogResult@DialogInstance@UI@@QEAAXH@Z`
//                            (0x1800455d0) is `MOV [RCX+0x24],EDX; RET`. It is
//                            also the value `?ShowDialog@UserInterface@UI@@`
//                            RETURNS (0x1801202e5), and every MessageDialog
//                            button writes it (src/game/Dialog/messagedialog.cpp
//                            Slot10..13 -> 1/0/2/3).
//                            ctor zeroes it                             0x180114870
//   0x28  _parent            ctor param 3, an InternalPtr<DialogInstance>
//                            passed BY VALUE, so the ctor takes *param  0x180114870
//   0x30  _dataSource        ctor param 4 (Object::Base*); the ctor
//                            forwards it as AddController's 2nd arg     0x180114870
//   0x38  _children          Collection::LinkedList<InternalPtr<
//                            DialogInstance>>: head +0x38, tail +0x40,
//                            count +0x48. The ctor PREPENDS itself to
//                            its parent's list — see the note below.    0x180114870
//   0x50  _root              ?GetRoot@ is one line: `*out = this[0x50]` 0x180045580
//   0x58  _spriteSheet       ctor copies _dialog->+0x20                 0x180114870
//   0x60  _spriteIndex       ctor: SpriteSheet::GetSpriteIndexFromName,
//                            or -1 when the sheet is null               0x180114870
//   0x68  _timers            Collection::Array<DialogTimer>: data +0x68,
//                            size +0x70, capacity +0x74. ?StartTimer@
//                            walks it with a stride of 0x18 and
//                            ?StopTimer@ swap-downs by 0x18.       0x1801137c0/730
//   0x78  _controllers       Collection::Array<DialogController*>: data
//                            +0x78, size +0x80, capacity +0x84. Five
//                            methods iterate exactly this pair.         0x180113320
//   0x88  _upperLeftX        ?SetUpperLeft@ delta base                  0x180114a60
//   0x8c  _upperLeftY        ?SetUpperLeft@ delta base                  0x180114a60
//   0x90  _hoverMouseX/Y     ctor sets the qword to -1 (both ints -1). ?Update@
//                      (0x180113980) compares the UpdateState mouse to this pair
//                      and stores it back, so the pair is "where the mouse was
//                      last frame while over this dialog" -- the tooltip settle.
//                            reads it. NAME IS A GUESS, flagged.        0x180114870
//   0x98  _hovering          ctor sets the byte to 0.                    0x180114870
//                            NAME PROVEN 2026-09-09 (Wave 10C lane UIF6), not a
//                            guess any more: ?IsHovering@DialogInstance@UI@@QEBA_NXZ
//                            (0x1800455a0) is the whole function
//                              MOVZX EAX, byte ptr [RCX+0x98] / RET
//                            -- a one-line exported accessor, which this tree
//                            treats as the strongest class of field evidence
//                            (UserInterface.h "FIELD EVIDENCE"). ?EndHover@
//                            (0x180113d10) clears the same byte and emits UI
//                            event 0x11, and UI::Element::Update gates the
//                            tooltip BEGIN/MOVE arm on it being non-zero
//                            (src/engine/ui/element_vtable.cpp:735).
//   0xa0  _hoverTime         ctor zeroes it; ?EndHover@ (0x180113d10) rewrites it
//                            unconditionally with (int64)(0.0 / _secondsPerTick),
//                            i.e. a System::Time tick count, NOT a pointer. Retyped
//                            from void* by Wave 10C lane UIF6; same 8 bytes, same
//                            zero store, so no layout change.
//   0xa8  _userData          ctor param 5 (void*)                       0x180114870
//   0xb0  _userFlags         ctor param 6 (int)                         0x180114870
//
// THE CHILD LIST IS A PREPEND, AND THAT MATTERS. The ctor's insert reads
//
//     if (parent->[0x38] == 0)  parent->[0x40] = node;
//     else { parent->[0x38]->[+8] = node;  node->[+0x10] = parent->[0x38]; }
//     parent->[0x38] = node;  parent->[0x48]++;
//
// which is ambiguous on its own — it is equally consistent with
// {tail@0x38, head@0x40} + append and with {head@0x38, tail@0x40} + prepend.
// The tie-break is `?GetFirstDialog@UserInterface@UI@@…` (0x180031150), which
// is literally `MOV RAX,[RCX]; MOV [RDX],RAX; RET` — the FIRST element lives at
// the list's +0x00. So +0x38 is the head, +0x40 is the tail, node+0x08 is
// `prev` and node+0x10 is `next`, and this matches
// src/engine/collection/Collection.h's LinkedList exactly, INCLUDING its
// otherwise-unpinned `_pad14`, which is the +0x4c hole above. Recorded because
// the naive reading of the ctor alone gets it backwards.
//
// PORTABILITY: no <windows.h>, per PLAN.md §4.1 decision 1.
// ============================================================================

#include <cstdint>
#include <cstddef>

#include "../collection/Collection.h"
#include "../object/Internal.h"
#include "../object/External.h"
#include "DialogController.h"
#include "Element.h"   // B4: Element::UpdateState (Update's parameter), _root's fields

namespace RtUI {

// ============================================================
// UI::DialogController — one behaviour object bolted onto a dialog.
//
// Created by `DialogInstance::AddController` (0x1801133b0): it looks the type
// id up in `Object::Factory::_singleton`'s AVL tree, calls the factory's vtable
// slot +0x20 to construct one, checks `IsDerivedFrom(DialogController)`, then
// fills the four fields below and calls the controller's OWN vtable slot +0x20.
//
//   off   field         proven by
//   ----  ------------  ----------------------------------------------------
//   0x00  Object::Internal   the IsDerivedFrom check and the vtable calls
//   0x10  _config       AddController stores its ExternalPtr<
//                       DialogControllerConfig> argument here     0x1801133b0
//   0x18  _dialog       AddController stores the owning DialogInstance*
//   0x20  _container    ?GetContainer@ is one line: `*out = this[0x20]`
//                                                                 0x18003b700
//   0x28  _dataSource   AddController stores its Object::Base* argument
//
// SIZE 0x30 — asserted below from the deleting dtor 0x18003b800's
// `MOV EDX,0x30` and the 38 exe panel structs that start at +0x30.
//
// Vtable slots this lane dispatches through, all read off the retail
// disassembly of the five DialogInstance iteration loops:
//   +0x10  GetTypeId()                 — ?GetController@   0x18011335b
//   +0x38  OnUpdateLayout()            — ?OnUpdateLayout@  0x1801153cb
//   +0x40  OnUpdate(const Time&)       — ?OnUpdate@        0x18011543e
//   +0x48  OnElementEvent(...)         — ?OnElementEvent@  0x180114f5e
// (+0x10 is `Object::Internal`'s Type slot — src/engine/object/Internal.h
// calls the same offset kInternalVtblType, an independent confirmation.)
// ============================================================

enum : int {
    kControllerVtblGetTypeId       = 0x10,
    kControllerVtblOnUpdateLayout  = 0x38,
    kControllerVtblOnUpdate        = 0x40,
    kControllerVtblOnElementEvent  = 0x48,
};

// ============================================================
// One entry of DialogInstance::_timers. Stride 0x18, pinned twice over:
// ?StartTimer@ (0x1801137c0) builds a 0x18-byte local {Time, Time, int} and
// hands it to the Array grow helper, and ?StopTimer@ (0x180113730) shifts
// entries down by copying three qwords from +0x18/+0x20/+0x28 to +0x00/+0x08/
// +0x10 while stepping the cursor by 0x18.
//
// `System::Time` is one int64 tick count (src/engine/system/Value.h). It is
// spelled as a bare int64_t here so this header does not have to pull in a
// non-namespaced global `Time` struct.
// ============================================================
struct DialogTimer {
    int64_t _duration;    // +0x00  System::Time — StartTimer writes the same
                          //        value into both; nothing else reads it
    int64_t _remaining;   // +0x08  System::Time — the LIVE countdown: ?Update@
                          //        (0x180113980) subtracts the frame time here
                          //        and fires event 0x13 at <= 0 (B4 H3: the
                          //        tree had +0x00 / +0x08 named the other way)
    int32_t _id;          // +0x10
    int32_t _pad14;       // +0x14  never read or written by either method
};
static_assert(sizeof(DialogTimer) == 0x18, "DialogInstance timer stride is 0x18");
static_assert(offsetof(DialogTimer, _duration) == 0x0, "DialogTimer::_duration");
static_assert(offsetof(DialogTimer, _remaining) == 0x8, "DialogTimer::_remaining");
static_assert(offsetof(DialogTimer, _pad14) == 0x14, "DialogTimer::_pad14");
static_assert(offsetof(DialogTimer, _id) == 0x10, "DialogTimer::_id @ +0x10 (StopTimer's compare)");

// ============================================================
// UI::DialogInstance
// ============================================================


// The prefix the replay oracle compares: everything the base ctor initialises.
constexpr size_t kDialogInstanceCtorExtent = 0xb4;

// sizeof(UI::ToolTipDialog), the one DialogInstance-derived object the DLL
// allocates itself (BeginToolTip 0x180120860). NOT the base's size.
constexpr size_t kDialogInstanceAllocSize = 0xc0;


// ============================================================
// UI::ComboDialog — the drop-down list a UI::ComboElement opens.
//
// Derives from DialogInstance and adds exactly three pointers. Appended by the
// lane that ported `??0ComboDialog@UI@@QEAA@…` (0x180111d00); the body that
// pins every line below is src/engine/ui/combodialog.cpp.
//
//   off   field           proven by
//   ----  --------------  -------------------------------------------------
//   0x00  DialogInstance  ctor's first act is to CALL the DialogInstance ctor
//                         overload at 0x1801146b0 on `this`, then stamp
//                         `??_7ComboDialog@UI@@6B@` (0x1801590e0) over +0x00.
//   0xb8  _list           written null at 0x180111d60, then at 0x180111e17
//                         with the ListElement found under the dialog's root.
//                         Every later read in the ctor re-loads THIS field.
//   0xc0  _parentDialog   ctor param 3, an InternalPtr<DialogInstance> passed
//                         BY HIDDEN REFERENCE: 0x180111d6e is `MOV RAX,[RDI]`
//                         where RDI is the incoming R9. *** It is NOT the
//                         DialogInstance base's `_parent` (+0x28): the ctor
//                         hands the base ctor a ZEROED local instead
//                         (0x180111d31 / 0x180111d48), so a ComboDialog is
//                         deliberately NOT linked into its parent's child
//                         list, and keeps the parent here instead. ***
//   0xc8  _combo          ctor param 4, InternalPtr<ComboElement>, also by
//                         hidden reference (0x180111d7f-0x180111d8a).
//
// SIZE IS PROVEN, unlike the base's. `??2ComboDialog@UI@@SAPEAX_K@Z` is called
// with `MOV ECX,0xd0` at 0x18011282b, and src/engine/ui/factory_ui1_tables.inc
// line 540 independently records 0xd0 for this class.
// ============================================================

}  // namespace RtUI

// ############################################################################
// UI::DialogInstance — THE REAL CLASS (ENDGAME B.3 step 6, block B)
// ############################################################################
//
// The `RtUI::DialogInstance` POD above STAYS, field-identical and
// cross-asserted below: the ctor DOORS (dialog_create.cpp,
// combodialog_ctor.cpp) build minted-table objects through it, files outside
// this block (userinterface_hover.cpp, src/diag/mouse_inject.cpp) read it,
// and engine/types/ui/DialogInstance.h aliases it so that the 20 exe dialog
// structs in engine/types/ui/Dialogs.h can keep embedding `DialogInstance
// base;` BY VALUE — a member of this class's ABSTRACT type would be
// ill-formed (B34_BRIEFS A.4 as amended).
//
// PARENT: `Object::Internal`, proved by
//   0x1800453d0  ?_ParentTypeName@DialogInstance@UI@@CAPEBDXZ -> "Object::Internal"
//
// `??_7DialogInstance@UI@@6B@` @ 0x180156d70, TEN slots (B36_SLOTS.md sec. 1):
//
//   [0] +0x00  0x1800458c0  (unexported) vector deleting dtor
//   [1] +0x08  0x180045400  ?TypeName@DialogInstance@UI@@UEBAPEBDXZ
//   [2] +0x10  0x1800453e0  ?Type@DialogInstance@UI@@UEBAIXZ
//   [3] +0x18  0x180114fa0  ?Persist@DialogInstance@UI@@UEAAXAEAVStream@IO@@@Z   (RET)
//   [4] +0x20  0x180114fb0  ?OnOpened@DialogInstance@UI@@UEAAXXZ
//   [5] +0x28  0x180115330  ?OnClosed@DialogInstance@UI@@UEAAXXZ
//   [6] +0x30  0x1801153a0  ?OnUpdateLayout@DialogInstance@UI@@UEAAXXZ
//   [7] +0x38  0x180115400  ?OnUpdate@DialogInstance@UI@@UEAAXAEBVTime@System@@@Z
//   [8] +0x40  0x180114ed0  ?OnElementEvent@DialogInstance@UI@@UEAAXV?$InternalPtr@VElement@UI@@@Object@@HHHHHPEAX@Z
//   [9] +0x48  0x180115470  ?OnSerializeSettings@DialogInstance@UI@@UEAAXAEAVStream@IO@@@Z (RET)
//
// RULE B6-1.2: DialogController numbers the same names ONE SLOT LATER
// (DialogController.h). Slot 8 takes the ADDRESS of the caller's InternalPtr
// copy, for the reason DialogController.h's banner gives.
//
// CONCRETE, REAL CONSTRUCTORS (ENDGAME B.3.8 block RB; B.3.9 UI-D retired
// the minted table, its RT_DELDTOR row, the ??0 / ??1 doors and the slot
// doors). Every construction is a real constructor: the 20 exe dialog
// classes' init lists, the plain child dialogs the exe builds (ToolUI,
// edit-select, toolbar utility, the main menu's BuildId child:
// `::new (p) UI::DialogInstance(...)`), ComboDialog / ToolTipDialog, and the
// factory. The 6-argument ctor's body is `InitFields` and the dtor's is
// `ReleaseFields` (private helpers; no door shares them any more). Slot 0 is
// the SCALAR arm because the exe instantiates the class itself (its table
// copy 0x14015f5a0).
//
// UI IS CONSTRUCTED ON THE LOADER THREAD (RULE B6-5): no member below keeps a
// static initialiser, a lazy singleton or a first-call cache.
// ############################################################################

namespace System { class Time; class String; }

namespace UI {

class Dialog;                   // Dialog.h (this block)
class DialogControllerConfig;   // DialogControllerConfig.h (this block)
class ElementDescription;       // block A
class Element;                  // block A
class UserInterface;            // block E
class SpriteContext;            // block D
class SpriteSheet;

// ---------------------------------------------------------------------------
// DialogInstance::_flags (+0x20), seeded from Dialog::_defaultFlags (+0x40, the
// .rsc value; the ctor copies one into the other, so the bits are the same in
// both words). Names from Runtime-x64-profile.dll's {value, name} table at
// 0x1802543b0 (the DialogInstance flag names); the predicates are cited per
// line. Unscoped: the fields and SetFlagsOnOff stay uint32_t.
// ---------------------------------------------------------------------------
enum DialogFlag : uint32_t {
    kDialogFlagPauseGame         = 0x00000001,   // ?PausesGame@ 0x180114160 = f & 1
    kDialogFlagModal             = 0x00000002,   // ?IsModal@ 0x180114170 = f >> 1 & 1: input stops here
    kDialogFlagNoMouseCapture    = 0x00000004,   // ?NoMouseCapture@ 0x180114150: the mouse passes through
    kDialogFlagPerElementCapture = 0x00000008,   // ?PerElementCapture@ 0x180114140: hit test via root Contains
    kDialogFlagFirstDraw         = 0x00000010,   // ?IsFirstDraw@ 0x180114130: the draw barrier
    kDialogFlagNoDelete          = 0x00000020,   // ?DeleteOnClose@ 0x180114110 = !(f >> 5 & 1)
    kDialogFlagAlwaysDraw        = 0x00000040,   // _defaultFlags only: still drawn below the first-draw dialog
};

class DialogInstance : public Object::Internal {
public:
    // [0] CONCRETE (ENDGAME B.3.8 block RB, B38_RULES D1/D2): the exe
    // instantiates the Runtime class ITSELF — its own copy of this table at
    // 0x14015f5a0 (B38_SLOTS section 4; the ToolUI child dialog and four other
    // stampers), whose slot 0 is the exe's scalar deleting dtor 0x140020cb0:
    //     CALL [??1DialogInstance]  ; TEST BL,1 ; JZ ; CALL [MemFree]  ; MOV RAX,RDI
    // The DLL's own vector deleting dtor 0x1800458c0 (and its RT_DELDTOR
    // row) is retired with the minted table (ENDGAME B.3.9 UI-D).
    // Inline so the key function (TypeName) and so the vtable's TU are unchanged.
    // `??1DialogInstance@UI@@UEAA@XZ` 0x180114d40. VIRTUAL (B3X W1).
    // The compiler restamps DialogInstance's table on entry and chains
    // ~Array (x2, no-ops on the zeroed headers) and ~Internal after the body —
    // retail's inlined ~Internal tail.
    ~DialogInstance() override { ReleaseFields(); }

    // ENDGAME B.3.9 TI-UI: ONE reflection block (B39 D6); `_typeInfo` is the
    // class's own record (DEFINE_TYPE in the class's TU), no longer minted.
    // retail: TypeName 0x180045400, Type 0x1800453e0, TypeId 0x1800453f0, CreateNewObject 0x1800454f0
    DECLARE_TYPE_INHERITED_INPLACE(DialogInstance)
public:
    void Persist(IO::Stream& stream) override;                          // [3] 0x180114fa0  RET
    virtual void OnOpened();                                      // [4] 0x180114fb0
    virtual void OnClosed();                                      // [5] 0x180115330
    virtual void OnUpdateLayout();                                // [6] 0x1801153a0
    virtual void OnUpdate(const System::Time& time);              // [7] 0x180115400
    virtual void OnElementEvent(Object::InternalPtr<Element>* element,
                                UI::EventId a, int32_t b, int32_t c,
                                int32_t d, int32_t e, void* userData);  // [8] 0x180114ed0
    virtual void OnSerializeSettings(IO::Stream& stream);               // [9] 0x180115470  RET

    // ---- PORTED non-virtual members ---------------------------------------
    // Each registered door delegates with a QUALIFIED call. The RETSLOT
    // exports return by value here and the door placement-news (B5-12).
    Object::InternalPtr<Element> GetRoot();                                    // 0x180045580
    Object::InternalPtr<DialogController> GetController(uint32_t typeId) const; // 0x180113320
    Object::InternalPtr<Element> FindElement(const System::String& name);      // 0x180114c70
    Object::InternalPtr<Element> FindElementType(uint32_t typeId);             // 0x1801140c0
    void SetFlagsOnOff(uint32_t on, uint32_t off);                             // 0x180114690
    // `System::Time` BY VALUE in retail: one int64 tick count, in R8.
    void StartTimer(int32_t id, int64_t timeTicks);                            // 0x1801137c0
    void StopTimer(int32_t id);                                                // 0x180113730
    bool Contains(int32_t x, int32_t y) const;                                 // 0x180114820
    int32_t GetMouseCursorIndex(Object::InternalPtr<Element> element,
                                int32_t x, int32_t y) const;                   // 0x180114010
    void ComputeLayout(int32_t x, int32_t y, int32_t w, int32_t h);            // 0x180113d90
    // `Element::UpdateState&` in retail.
    void Update(const System::Time& time, UserInterface* ui,
                Element::UpdateState& state);                                  // 0x180113980
    void SetUpperLeft(int32_t x, int32_t y, bool scaled);                      // 0x180114a60
    void AddController(uint32_t typeId, Object::Base* dataSource,
                       Object::ExternalPtr<DialogControllerConfig> config,
                       Object::InternalPtr<Element> container);                // 0x1801133b0
    void AddController(uint32_t typeId, Object::Base* dataSource,
                       Object::ExternalPtr<DialogControllerConfig> config,
                       Object::ExternalPtr<ElementDescription> description,
                       const System::String& name);                            // 0x180113470
    // The first dword of a saved-settings buffer (_userData): SaveSettings writes
    // it, LoadSettings refuses a buffer without it (dialog_settings.cpp).
    static constexpr uint32_t kSettingsMagic = 0xdeadbeefu;
    void SaveSettings();                                                       // 0x180114240

    // ---- exported, NOT PORTED here: declared, deliberately undefined -------
    // Nothing calls these as members. `EndHover` IS ported — as a door in
    // src/engine/ui/userinterface_hover.cpp, which is the fragment pass's
    // file, not this block's.
    void EndHover(UserInterface* ui);                                          // 0x180113d10
    Object::InternalPtr<Element> GetRoot() const;                              // 0x180045590
    Object::InternalPtr<DialogInstance> GetParent();                           // 0x180045570
    const Dialog* GetDialog() const;                                           // 0x180045620
    UserInterface* GetUserInterface();                                         // 0x1800455e0
    void SetUserInterface(UserInterface* ui);                                  // 0x1800455b0
    void* GetUserObject() const;                                               // 0x180045610
    int32_t GetResult() const;                                                 // 0x1800455c0  reads +0x24
    void SetDialogResult(int32_t result);                                      // 0x1800455d0  writes +0x24
    bool IsHovering() const;                                                   // 0x1800455a0  reads +0x98
    int32_t GetLeftCorner() const;                                             // 0x1800455f0
    int32_t GetTopCorner() const;                                              // 0x180045600
    int32_t GetDrawLevel() const;                                              // 0x1801140f0
    int32_t GetMouseCursorIndex(int32_t x, int32_t y) const;                   // 0x180113fe0
    bool IsFirstDraw() const;                                                  // 0x180114130
    bool IsModal() const;                                                      // 0x180114170
    bool NoMouseCapture() const;                                               // 0x180114150
    bool PausesGame() const;                                                   // 0x180114160
    bool PerElementCapture() const;                                            // 0x180114140
    bool DeleteOnClose() const;                                                // 0x180114110
    void CloseChildren();                                                      // 0x180114180
    void Detach();                                                             // 0x1801141c0
    void LoadSettings();                                                       // 0x180114450
    void Draw(SpriteContext& context) const;                                   // 0x180113840
    void DrawBackground(SpriteContext& context, int32_t x, int32_t y,
                        int32_t w, int32_t h) const;                           // 0x180113880
    DialogInstance(const DialogInstance& other);                               // 0x180045630
    DialogInstance& operator=(const DialogInstance& other);                    // 0x180045790

    // System::MemoryAllocator statics — B36_RULES 7 keeps them doors
    // (`??3DialogInstance@UI@@SAXPEAX@Z` is one, in spritesheet_new.cpp).
    static void* operator new(std::size_t bytes);                 // 0x180045410
    // Declared, defined NOWHERE: construct in place with `::new (p) X(...)`.
    static void* operator new(std::size_t bytes, void* place);    // 0x180045480
    static void  operator delete(void* p) { ::Object::detail::InternalMemFree(p); }                        // 0x180045490
    // The class's own factory (B.3.9 UI-D; no CreateInPlaceObject export).

    // ---- construction (ENDGAME B.3.8 block RB, RULE B8-1) --------------------
    // REAL constructors now. The compiler stamps Base -> Internal ->
    // DialogInstance (-> the subclass) exactly where retail's `CALL
    // ??0Internal` + `LEA RAX,[0x180156d70]; MOV [RBX],RAX` do; the bodies are
    // retail's minus those stores. PUBLIC: the class is concrete (slot 0).
    //
    // ??0DialogInstance@UI@@QEAA@XZ  0x180114ca0 — the default ctor (the one
    // `CreateNewObject` runs). No door exists for it; this IS its port. It
    // leaves +0x30 (_dataSource), +0x4c, +0x64, +0x99..+0x9f and +0xb0..+0xb7
    // unwritten, as retail does. `_timers` / `_controllers` are zeroed by their
    // own Array ctors — the six zero stores retail makes at +0x68..+0x80.
    DialogInstance() : _root(Object::InternalBasePtr::NoStore()) {
        _ui = nullptr;                                  // +0x10
        _dialog = nullptr;                              // +0x18
        _flags = 0;                                     // +0x20 } one QWORD store
        _result = 0;                                    // +0x24 }
        _parent = Object::InternalPtr<DialogInstance>(); // +0x28
        _children._head = nullptr;                      // +0x38
        _children._tail = nullptr;                      // +0x40
        _children._count = 0;                           // +0x48 (DWORD)
        _root = nullptr;                                // +0x50
        _spriteSheet = nullptr;                         // +0x58
        _spriteIndex = -1;                              // +0x60
        _upperLeftX = 0;                                // +0x88 } one QWORD store
        _upperLeftY = 0;                                // +0x8c }
        _hoverMouseX = -1;                              // +0x90 } one QWORD -1
        _hoverMouseY = -1;                              // +0x94 }
        _hovering = 0;                                  // +0x98 (BYTE)
        _hoverTime = 0;                                 // +0xa0
        _userData = nullptr;                            // +0xa8
    }

    // ??0DialogInstance@UI@@QEAA@PEAVUserInterface@1@PEBVDialog@1@V?$InternalPtr@
    //     VDialogInstance@UI@@@Object@@PEAVBase@5@PEAXH@Z   0x180114870
    // The overload the exe's dialog ctors call. Its body is `InitFields`
    // (dialog_create.cpp). `parent` is by value (retail passes it by hidden
    // reference and the callee owns the copy).
    DialogInstance(UserInterface* ui, const Dialog* dialog,
                   Object::InternalPtr<DialogInstance> parent,
                   Object::Base* dataSource, void* userData, int32_t userFlags)
        : _root(Object::InternalBasePtr::NoStore()) {   // built in CreateInstance's return slot (InitFields)
        InitFields(ui, dialog, &parent, dataSource, userData, userFlags);
    }

    // ??0DialogInstance@UI@@QEAA@PEAVUserInterface@1@V?$InternalPtr@VElement@UI@@@
    //     Object@@V?$InternalPtr@VDialogInstance@UI@@@4@PEAVBase@4@PEAXH@Z
    //                                                            0x1801146b0
    // The ROOT-ELEMENT overload (ComboDialog's base): no Dialog resource, the
    // root handed in; the parent's child list gets this dialog PREPENDED.
    // ENDGAME B.3.9 UI-D: the real constructor, replacing the door
    // `RtUi_DialogInstance_CtorFromElement` (combodialog_ctor.cpp, the body).
    DialogInstance(UserInterface* ui, Object::InternalPtr<Element> root,
                   Object::InternalPtr<DialogInstance> parent,
                   Object::Base* dataSource, void* userData, int32_t userFlags);

private:
    // 0x180114870 MINUS `CALL ??0Internal` and the vptr store — dialog_create.cpp.
    void InitFields(UserInterface* ui, const Dialog* dialog,
                    const Object::InternalPtr<DialogInstance>* parent,
                    Object::Base* dataSource, void* userData, int32_t userFlags);
    // 0x180114d40 MINUS the vptr store and the inlined ~Internal — dialog.cpp.
    void ReleaseFields();

protected:

    // RULE B5-15 / B6-7, COPIED from B36_SLOTS section 3:
    //   0x1800453c0  ?_TypeName@       -> "DialogInstance"
    //   0x1800453d0  ?_ParentTypeName@ -> "Object::Internal"

public:
    // ---- the 0xa8 bytes DialogInstance adds, in retail order ---------------
    // Evidence per field: the RtUI POD's banner at the top of this file.
    UserInterface* _ui;            // +0x10  (B4 H4: typed)
    const Dialog*  _dialog;        // +0x18
    uint32_t       _flags;         // +0x20  SetFlagsOnOff 0x180114690
    int32_t        _result;        // +0x24  GetResult / SetDialogResult
    Object::InternalPtr<DialogInstance> _parent;                  // +0x28
    Object::Base*  _dataSource;    // +0x30  ctor param 4
    Collection::LinkedList<Object::InternalPtr<DialogInstance> > _children;  // +0x38..+0x4f
    // Retail's ctors never zero it before building it (every ctor names it
    // with InternalPtr's NoStore ctor; trivially destructible: ReleaseFields
    // deletes the Element explicitly, first, as retail's dtor does).
    Object::InternalPtr<Element> _root;   // +0x50
    SpriteSheet*   _spriteSheet;   // +0x58  (B4 H4: typed)
    int32_t        _spriteIndex;   // +0x60
    int32_t        _pad64;        // +0x64  NOT written by the ctor
    Collection::Array<RtUI::DialogTimer> _timers;                 // +0x68..+0x77
    Collection::Array<DialogController*> _controllers;            // +0x78..+0x87
    int32_t        _upperLeftX;    // +0x88
    int32_t        _upperLeftY;    // +0x8c
    int32_t        _hoverMouseX;   // +0x90  ctor -1
    int32_t        _hoverMouseY;   // +0x94  ctor -1
    uint8_t        _hovering;      // +0x98  IsHovering 0x1800455a0
    uint8_t        _pad99[7];     // +0x99  NOT written by the ctor
    int64_t        _hoverTime;     // +0xa0  System::Time ticks
    void*          _userData;      // +0xa8  ctor param 5
    int32_t        _userFlags;     // +0xb0  ctor param 6 — NOT padding, as the
                                   //        old engine POD claimed
    int32_t        _padB4;        // +0xb4  NOT written by the ctor
};

static_assert(sizeof(DialogInstance) == 0xb8,
              "UI::DialogInstance is 0xb8 bytes (MOV EDX,0xb8 in 0x1800458c0; "
              "20 exe subclasses start at +0xb8)");
static_assert(offsetof(DialogInstance, _dialog) == 0x18, "DialogInstance::_dialog");
static_assert(offsetof(DialogInstance, _flags) == 0x20, "DialogInstance::_flags");
static_assert(offsetof(DialogInstance, _result) == 0x24, "DialogInstance::_result");
static_assert(offsetof(DialogInstance, _parent) == 0x28, "DialogInstance::_parent");
static_assert(offsetof(DialogInstance, _dataSource) == 0x30, "DialogInstance::_dataSource");
static_assert(offsetof(DialogInstance, _children) == 0x38, "DialogInstance::_children");
static_assert(offsetof(DialogInstance, _root) == 0x50, "DialogInstance::_root");
static_assert(offsetof(DialogInstance, _spriteSheet) == 0x58, "DialogInstance::_spriteSheet");
static_assert(offsetof(DialogInstance, _spriteIndex) == 0x60, "DialogInstance::_spriteIndex");
static_assert(offsetof(DialogInstance, _pad64) == 0x64, "DialogInstance::_pad64");
static_assert(offsetof(DialogInstance, _timers) == 0x68, "DialogInstance::_timers");
static_assert(offsetof(DialogInstance, _controllers) == 0x78, "DialogInstance::_controllers");
static_assert(offsetof(DialogInstance, _upperLeftX) == 0x88, "DialogInstance::_upperLeftX");
static_assert(offsetof(DialogInstance, _upperLeftY) == 0x8C, "DialogInstance::_upperLeftY");
static_assert(offsetof(DialogInstance, _hoverMouseX) == 0x90, "DialogInstance::_hoverMouseX");
static_assert(offsetof(DialogInstance, _hoverMouseY) == 0x94, "DialogInstance::_hoverMouseY");
static_assert(offsetof(DialogInstance, _hovering) == 0x98, "DialogInstance::_hovering");
static_assert(offsetof(DialogInstance, _pad99) == 0x99, "DialogInstance::_pad99");
static_assert(offsetof(DialogInstance, _hoverTime) == 0xA0, "DialogInstance::_hoverTime");
static_assert(offsetof(DialogInstance, _userData) == 0xA8, "DialogInstance::_userData");
static_assert(offsetof(DialogInstance, _userFlags) == 0xB0, "DialogInstance::_userFlags");
static_assert(offsetof(DialogInstance, _padB4) == 0xB4, "DialogInstance::_padB4");
static_assert(alignof(DialogInstance) == 8, "qword-aligned (the vptr)");
static_assert(offsetof(DialogInstance, _ui) == sizeof(Object::Internal),
              "DialogInstance's own fields begin at the end of the Internal base");
#define B36_DI_PIN(f, off) \
    static_assert(offsetof(DialogInstance, f) == off, "DialogInstance::" #f " @ " #off);
B36_DI_PIN(_ui,          0x10)
B36_DI_PIN(_dialog,      0x18)
B36_DI_PIN(_flags,       0x20)
B36_DI_PIN(_result,      0x24)
B36_DI_PIN(_parent,      0x28)
B36_DI_PIN(_dataSource,  0x30)
B36_DI_PIN(_children,    0x38)
B36_DI_PIN(_root,        0x50)
B36_DI_PIN(_spriteSheet, 0x58)
B36_DI_PIN(_spriteIndex, 0x60)
B36_DI_PIN(_timers,      0x68)
B36_DI_PIN(_controllers, 0x78)
B36_DI_PIN(_upperLeftX,  0x88)
B36_DI_PIN(_upperLeftY,  0x8c)
B36_DI_PIN(_hoverMouseX, 0x90)
B36_DI_PIN(_hoverMouseY, 0x94)
B36_DI_PIN(_hovering,    0x98)
B36_DI_PIN(_hoverTime,   0xa0)
B36_DI_PIN(_userData,    0xa8)
B36_DI_PIN(_userFlags,   0xb0)
#undef B36_DI_PIN


}  // namespace UI
