// ============================================================================
// src/engine/ui/dialog_create.cpp — Wave 10 lane UI3
//   The four allocating UI::DialogInstance rows Wave 7 lane B held back.
// ============================================================================
//
//   | Export (short form)                    | Retail      | Ord  |
//   |----------------------------------------|-------------|------|
//   | ??0DialogInstance (ctor)               | 0x180114870 |  147 |
//   | ?AddController@ (…ExtPtr<ED>, String&) | 0x180113470 | 1541 |
//   | ?AddController@ (…IntPtr<Element>)     | 0x1801133b0 | 1542 |
//   | ?StartTimer@                           | 0x1801137c0 | 4081 |
//
// All four mangled names were read back out of the shipped export directory of
// C:\GOG Games\Banished\Runtime-x64.dll with tools/pe_exports.py BEFORE a line
// of body was written, and all four are present at exactly those addresses
// (reference_resolveexport_phantom_names.md — a name the DLL does not contain
// resolves to NULL in silence and the census still counts the registration).
// The full query output is in docs/history/runtime_port/WAVE10_LANE_UI3.md §1.
//
// ---------------------------------------------------------------------------
// THE BLOCKER THESE FOUR SAT BEHIND, AND WHY IT IS ANSWERED
// ---------------------------------------------------------------------------
// WAVE7_LANE_B.md §2a held all four back under one heading: "eight allocate or
// free — split ownership, and no oracle can be built". Three sub-claims, each
// re-derived here rather than inherited
// (reference_holdback_blockers_are_stale_check_them_first.md):
//
//  1. "`EnterCriticalSection` is Win32 and cannot appear in a non-`_win` file."
//     TRUE OF THE INLINED FORM, and irrelevant. Every allocation these four
//     make is the byte-for-byte body of an EXPORTED static:
//         ?MemAlloc@MemoryAllocator@System@@SAPEAX_KH@Z   0x1800c7a50
//             = Enter(_singleton+0x640); Allocate(_singleton,n,align); Leave
//         ?MemFree@MemoryAllocator@System@@SAXPEAX@Z      0x1800c7ad0
//     src/engine/collection/array.cpp made exactly this call three waves ago
//     and states the argument: "calling it IS retail's body minus the inlining,
//     and it keeps <windows.h> out of src/engine/". This file follows it and
//     uses the SAME two accessors, by address, for the same reason.
//
//  2. "The node it allocates is freed by `??1DialogInstance`, which stays in
//     the DLL." BOTH HALVES ARE NOW FALSE. `??1DialogInstance@UI@@` (0x180114d40)
//     was ported by Wave 9 lane UID and is registered in dialog.cpp. And even
//     when the DLL's own dtor runs (it still does, through the DLL's own direct
//     near-CALLs, which no registry can redirect), it frees into the SAME heap:
//     `MemAlloc`/`MemFree` reach 0x1800c7a50/0x1800c7ad0, which is the DLL's
//     allocator port. THERE IS NO CONFIGURATION IN WHICH THE TWO SIDES SEE
//     DIFFERENT HEAPS. That is the whole of the "split ownership" objection.
//
//  3. "`MemFree` is Wave 2's allocator port, which shipped opt-in with a NO-GO."
//     EXPIRED. docs/history/runtime_port/WAVE10_LANE_ALLOC.md returns GO: armed with
//     the DLL present, 901,685 allocations and 540,750 frees served by the
//     ported bodies in 30 s, a 3,000-frame redzone stress town with ZERO
//     violations, and an unarmed run byte-identical in behaviour. It remains
//     OPT-IN, which is why point 2 above is stated for BOTH settings.
//
// ---------------------------------------------------------------------------
// TWO UNEXPORTED ARRAY HELPERS, AND WHY REGISTERING THEM IS NOT NEW WORK
// ---------------------------------------------------------------------------
// `?StartTimer@` tail-calls 0x180076250 and the 4-arg `?AddController@`
// tail-calls 0x180076370. Neither is an export. Neither needed porting:
//
//   0x180076250  ≡  0x18005fb00   (registered, `kSpec_Node18`, stride 0x18)
//   0x180076370  ≡  0x180076900   (registered, `kSpec_Fill8Z`, stride 8)
//
// "≡" is a MEASUREMENT, not an impression: both pairs were compared
// instruction-for-instruction with the leading address stripped and every
// INTERNAL branch target rewritten as a body-relative delta, so every operand
// naming anything outside the body (0x1800c7a50, 0x1801fc718, 0x18014b390,
// 0x1800c6c00, 0x18014b388) had to match ABSOLUTELY
// (reference_position_only_diffs_hide_one_body.md). Both pairs: 0 differing
// instructions. THE INSTRUMENT WAS CONTROLLED — the same comparison between
// the stride-8 body and the stride-0x18 body reports 62 differing instructions,
// so it is not structurally incapable of saying "different"
// (reference_diagnostic_field_cannot_express_the_answer.md). The two extra
// `RegisterRuntimeAddr` lines live in src/engine/collection/array.cpp beside
// the bodies they name, NOT here, because a private second registration of a
// body another file owns is the drift that parameterisation exists to prevent.
//
// ---------------------------------------------------------------------------
// FAITHFULNESS NOTES A "TIDY" PORT WOULD DESTROY
// ---------------------------------------------------------------------------
//  * `?StartTimer@` finds an existing entry with the same id, rewrites its two
//    `Time` fields, AND THEN FALLS THROUGH AND APPENDS A SECOND ENTRY ANYWAY.
//    There is no `return` and no `else` at 0x1801137fd. Retail accumulates
//    duplicate timer ids and `?StopTimer@` removes only the first match.
//    WAVE7_LANE_B.md §6c wrote this down for whoever landed the body; this is
//    that landing, and the behaviour is reproduced, not repaired.
//  * The 4-arg `?AddController@` DEREFERENCES A NULL when the type id is not in
//    `Object::Factory`'s tree (0x1801133f4 sets RAX to the zero register and
//    0x1801133f7 immediately reads [RAX+0x10]), and again when the constructed
//    object is not a `UI::DialogController` (the `CMOVNZ` at 0x18011341e leaves
//    RDI at zero and 0x180113422 writes [RDI+0x10]). Both are reproduced.
//    CLAUDE.md: retail's unguarded derefs are load-bearing; a guard does not
//    restore the behaviour, it skips it.
//  * The 6-arg `?AddController@` calls `Array::SetSize` on `RBX+0xa0` and then
//    ORs `[RBX+0x34]` with RBX possibly null, for the same reason.
//  * The 6-arg's element-name test compares LENGTHS FIRST and only then calls
//    `StrCompare`, and its "lengths equal" arm re-tests the pointer and the
//    length a second time. Transcribed as written.
//  * `?StartTimer@`'s appended element leaves bytes +0x14..0x17 of the 0x18-byte
//    record UNWRITTEN — retail builds the fill record on the stack and never
//    touches that word. The differential oracle EXCLUDES those four bytes per
//    element; comparing them would fail 100 % of calls and read as a
//    catastrophic port defect
//    (reference_oracle_must_not_compare_unwritten_bytes.md).
//
// PORTABILITY: no <windows.h>, per PLAN.md §4.1 decision 1.
// ============================================================================

// *** INCLUDE ORDER IS LOAD-BEARING. ***
// `src/engine/object/External.h` opens `namespace RtObject {` and then
// `#include "TypeInfo.h"` INSIDE it, so whichever translation unit pulls
// External.h first nests every TypeInfo declaration into `RtObject::RtObject`
// for the whole TU — which is why dialog.cpp records that TypeInfo.h "IS NOT
// INCLUDABLE HERE". Including it FIRST fixes that for this TU: `#pragma once`
// then makes External.h's nested include a no-op. Do not move this line.
#include "object/TypeInfo.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "system/String.h"
#include "collection/Collection.h"
#include "ui/Dialog.h"
#include "ui/DialogInstance.h"
#include "ui/DialogController.h"   // B.3.9 TI-UI: X::TypeId()
#include "ui/GroupElement.h"
#include "ui/Element.h"       // ENDGAME B.3.6 closing pass: FindElement
#include "ui/SpriteSheet.h"   // ENDGAME B.3.6 closing pass: GetSpriteIndexFromName
#include "system/MemoryAllocator.h"
#include <new>
#include "../collection/array_internal.h" // ENDGAME B.3.2 step 2 — kSpec_Fill8Z

using RtUI::DialogTimer;

namespace {

// The 6-arg AddController propagates UI::kElementFlagNeedsSizeCalc (bit 27) up the
// parent chain: `OR dword ptr [RBX+0x34],0x8000000` at 0x180113620.

// ---- The allocator boundary ------------------------------------------------
// `?MemAlloc@MemoryAllocator@System@@SAPEAX_KH@Z` (0x1800c7a50), called
// directly (B3R RT-UI).
void* MemAlloc(uint64_t bytes, int align) {
    return System::MemoryAllocator::MemAlloc(bytes, align);
}

// ---- Probe scheduling ------------------------------------------------------
//
// *** THE DIFFERENTIAL MUST NOT RUN FROM THE REGISTER FUNCTION. *** That runs
// inside `RuntimePortInit()`, immediately after `ResolveInit()`'s LoadLibrary —
// at which point the DLL's `MemoryAllocator` has been CONSTRUCTED but not
// STARTED (`Engine::Startup` does that later). `?StartTimer@` allocates, so a
// ============================================================================
// Callees, called by name (B3R RT-UI).
// ============================================================================

// ENDGAME B.3.2 step 2 — the three forwarders that used to sit here are GONE.
// All three fields are real `Collection::Array<T>` members now
// (DialogInstance.h `_timers`/`_controllers`, Element.h `_children`), and all
// three rows are TRAIT-EQUIVALENT for the field's element type:
//
//   0x180076250  kSpec_Node18  {0x18,8,Assign18(full byte copy),init=null,
//                destroy=null,byteCopy} == SpecFor<DialogTimer>() field for
//                field: DialogTimer is a trivially copyable/constructible/
//                destructible 0x18-byte POD, so the trait-derived
//                `Array<DialogTimer>::SetSize` IS that instantiation.
//   0x180076370  kSpec_Fill8Z  {8,8,Assign8Z(zero-then-copy; net effect is a
//                full 8-byte write, same bytes a placement copy-construct of
//                a pointer produces),init=null,destroy=null,byteCopy} ==
//                SpecFor<DialogController*>().
//   0x180076900  kSpec_Fill8Z, same reasoning, == SpecFor<Element*>() for
//                `Element::_children`.
//
// No `ExplicitSpec` bridge needed at any of the three call sites below.

// ??0Dialog@UI@@QEAA@XZ 0x180112db0 / ??1Dialog@UI@@UEAA@XZ 0x180112e40: the
// 6-arg AddController builds a default UI::Dialog on its stack with both
// bodies INLINED. ENDGAME B.3.9 UI-D: `::new (p) UI::Dialog()` and
// `~T()` (slot 0, flags 0) (the door pair is retired).

// The two type ids the bodies test against. Retail loads the FIRST DWORD of
// each class's `_typeInfo` (`MOV EDX,dword ptr [0x1801f9e48]` for
// DialogController, 0x1801fabd8 for GroupElement): `X::TypeId()`, read at the
// use. ENDGAME B.3.9 TI-UI: the by-name lookup, its first-use cache and the
// WAVE 10C `[TidStatic]` attendance line (it reported which minted node the
// lookup read; there is no minted node left to report) are gone.

// ?_singleton@Factory@Object@@0PEAV12@EA — the class static now (ENDGAME
// B.3.9 SS1, B39_RULES D7): read fresh, no lazy cache (B9-10).
::Object::Factory* FactorySingleton() {
    return ::Object::Factory::_singleton;
}

// ============================================================================
// ?StartTimer@DialogInstance@UI@@QEAAXHVTime@System@@@Z   0x1801137c0
//
//     void DialogInstance::StartTimer(int id, System::Time t)
//
// `System::Time` is ONE int64 tick count and is passed BY VALUE in R8 — proved
// by the body itself, which stores R8 into the record's +0x00 and reloads the
// SAME home slot for +0x08 (0x1801137eb / 0x1801137f3). Both fields get the
// same value; DialogInstance.h names them `_duration` and `_remaining` (B4: ?Update@ counts +0x08 down)
// the second one "the reload", which is what an equal seed means.
//
// The loop cursor is a BYTE offset stepped by 0x18 (`ADD RAX,0x18`), and the id
// it compares lives at record+0x10 — the two facts DialogInstance.h pinned
// `DialogTimer` from, and the two the displacement oracle re-pins out of the
// shipped bytes below.
//
// THE FALL-THROUGH IS THE SHIPPED BEHAVIOUR. After rewriting a matching entry
// the body does NOT return; it walks to the end of the loop and appends another
// record with the same id (WAVE7_LANE_B.md §6c). Ported as written.
// ============================================================================
// B3R RT-UI: the door went with its registration (no reader); the body is
// UI::DialogInstance::StartTimer, bottom of file.

// ============================================================================
// ?AddController@DialogInstance@UI@@QEAAX I PEAVBase@Object@@
//     V?$ExternalPtr@VDialogControllerConfig@UI@@@4@
//     V?$InternalPtr@VElement@UI@@@4@ @Z                       0x1801133b0
//
//     void DialogInstance::AddController(unsigned typeId, Object::Base* dataSource,
//                                        ExternalPtr<DialogControllerConfig> config,
//                                        InternalPtr<Element> container)
//
// ARGUMENT ABI, read off the homing stores rather than from the mangled name:
//   RCX  this
//   EDX  typeId
//   R8   Object::Base* dataSource          -> controller+0x28
//   R9   the ExternalPtr's 8 bytes BY VALUE -> controller+0x10
//   [rsp+0x28]  a POINTER to the InternalPtr — 0x180113416 loads the argument
//   and 0x18011341b DEREFERENCES it before storing. engine_funcs.h's
//   `fn_DialogInstance_AddControllerTab_t` already spells it that way and its
//   exe call sites pass an address; the two agree.
//
// THE TWO UNGUARDED NULL DEREFERENCES ARE RETAIL'S. See the banner.
// ============================================================================
// B3R RT-UI: the door went with its registration (no reader); the body is
// UI::DialogInstance::AddController (4-arg), bottom of file.

// ============================================================================
// ?AddController@DialogInstance@UI@@QEAAX I PEAVBase@Object@@
//     V?$ExternalPtr@VDialogControllerConfig@UI@@@4@
//     V?$ExternalPtr@VElementDescription@UI@@@4@
//     AEBVString@System@@ @Z                                   0x180113470
//
//     void DialogInstance::AddController(unsigned typeId, Object::Base* dataSource,
//                                        ExternalPtr<DialogControllerConfig> config,
//                                        ExternalPtr<ElementDescription> desc,
//                                        const System::String& name)
//
// Three phases:
//   1. RESOLVE the insertion element by name — the dialog root itself if its
//      name matches, else the first hit of `Element::FindElement` over the
//      root's direct children, else null; then require it to be a
//      `UI::GroupElement` (`?_typeInfo@GroupElement@UI@@`, 0x1801fabd8).
//   2. CREATE the element: default-construct a temporary `UI::Dialog`, call the
//      description's vtable slot 8 with (out, &tmpDialog), parent the result
//      into the resolved element, append it to that element's `_children`, and
//      OR UI::kElementFlagNeedsSizeCalc up the whole parent chain. Then destroy tmp.
//   3. FORWARD to the 4-arg overload with the created element, if id != 0.
//
// THE TEMPORARY IS A `UI::Dialog`, NOT AN `ElementDescription`. The inlined
// ctor stamps `??_7Dialog@UI@@6B@` (0x1801562a0) as its third vtable — the
// ElementDescription ctor stamps 0x1801561c0. Getting that wrong would have put
// the wrong object on the description's slot-8 call. See lane doc §3.
//
// WHEN `desc` IS NULL the whole of phases 1 and 2 is skipped (0x1801134aa) and
// the body goes straight to phase 3 with a null element.
// ============================================================================
// B3R RT-UI: the door went with its registration (no reader); the body is
// UI::DialogInstance::AddController (5-arg), bottom of file.

// (ENDGAME B.3.9 UI-D: the ??0DialogInstance door `RtUiDialogInstance_Ctor`
// (0x180114870) is retired; the exe's last three door sites spell
// `::new (p) UI::DialogInstance(ui, dialog, parent, dataSource, userData,
// userFlags)`. The ctor's body is `InitFields`, below.)

}  // namespace

// ============================================================================
// UI::DialogInstance::InitFields — the body of ??0DialogInstance 0x180114870
// after `CALL ??0Internal` and the vptr store (ENDGAME B.3.8 block RB, B8-1).
// The real constructor's body (DialogInstance.h); since ENDGAME B.3.9 UI-D no
// door shares it. MOVED VERBATIM from the retired door: it still
// works through the field-identical `RtUI::` POD views (cross-asserted in
// DialogInstance.h), so not one store changed.
// ============================================================================
namespace UI {

void DialogInstance::InitFields(UserInterface* uiArg, const Dialog* dialogArg,
                                const Object::InternalPtr<DialogInstance>* parentArg,
                                Object::Base* dataSourceArg, void* userData,
                                int32_t userFlags) {
    const Dialog* const dlg = dialogArg;
    _ui         = uiArg;                          // +0x10
    _dialog     = dialogArg;                      // +0x18
    _flags      = dlg->_defaultFlags;             // +0x20  from _dialog->+0x40
    _result     = 0;                              // +0x24
    _parent     = Object::InternalPtr<DialogInstance>();   // +0x28  written 0 then...
    _parent     = *parentArg;                     // +0x28  ...the real value
    _dataSource = dataSourceArg;                  // +0x30
    _children._head  = nullptr;                   // +0x38
    _children._tail  = nullptr;                   // +0x40
    _children._count = 0;                         // +0x48   (+0x4c left alone)

    // 0x1801148e7: `_dialog->_rootDescription`'s vtable slot 8, with the hidden
    // return slot pointed AT THE OBJECT'S OWN `_root` FIELD and the Dialog as
    // the second argument — the same slot the 6-arg AddController calls.
    // `_root` is retail's InternalPtr<Element>, built here with NO prior store
    // (0x1801148f1 LEA RDX,[RSI+0x50]): the ctors name it with NoStore.
    dlg->_rootDescription->CreateInstance(&_root, dialogArg);

    _spriteSheet = dlg->_spriteSheet;             // +0x58
    // 0x180114900: -1 when the sheet is null, otherwise the sheet's lookup of
    // `&_dialog->_spriteName` — the String OBJECT's address, not the char*.
    _spriteIndex = _spriteSheet
        ? _spriteSheet->GetSpriteIndexFromName(dlg->_spriteName)
        : -1;                                     // +0x60   (+0x64 left alone)

    _timers._data      = nullptr;                 // +0x68
    _timers._size      = 0;                       // +0x70
    _timers._capacity  = 0;                       // +0x74
    _controllers._data     = nullptr;             // +0x78
    _controllers._size     = 0;                   // +0x80
    _controllers._capacity = 0;                   // +0x84
    _upperLeftX = 0;                              // +0x88
    _upperLeftY = 0;                              // +0x8c
    _hoverMouseX = -1;                            // +0x90  a QWORD -1 --
    _hoverMouseY = -1;                            // +0x94  both hover ints
    _hovering    = 0;                             // +0x98  a single BYTE
    _hoverTime   = 0;                             // +0xa0   (+0x99.. left alone)
    _userData    = userData;                      // +0xa8
    _userFlags   = userFlags;                     // +0xb0   (+0xb4 left alone)

    // 0x180114965: the freshly created root Element points back at us.
    // UNGUARDED in retail — if slot 8 returned null this faults. Faithful.
    _root->_dialogParent = this;

    // ---- link into the parent's child list -------------------------------
    // A PREPEND: head at +0x38, tail at +0x40, node = {element, prev, next}
    // with prev at +0x08 and next at +0x10. DialogInstance.h records the
    // tie-break (`?GetFirstDialog@UserInterface@UI@@`, 0x180031150, reads the
    // FIRST element from the list's +0x00) and warns that reading the ctor
    // alone gets this backwards.
    using ChildNode = Collection::LinkedList<Object::InternalPtr<DialogInstance> >::Node;
    if (DialogInstance* const parent = _parent.Get()) {
        // 0x180114999: `MOV EDX,0x18 / LEA R8D,[RDX-0x10]` -> Allocate(0x18, 8),
        // wrapped in Enter/Leave — i.e. exactly ?MemAlloc@…(0x18, 8).
        ChildNode* node = static_cast<ChildNode*>(MemAlloc(0x18, 8));
        if (node) {
            node->element = Object::InternalPtr<DialogInstance>(this);
            node->prev    = nullptr;
            node->next    = nullptr;
        }
        // 0x1801149cf: on an OOM `node` is null and retail links the null in
        // anyway. Reproduced, guard-free.
        if (ChildNode* head = parent->_children._head) {
            head->prev = node;                                 // old head->prev
            node->next = parent->_children._head;              // node->next
        } else {
            parent->_children._tail = node;
        }
        parent->_children._head = node;
        parent->_children._count++;
    }

    // ---- run the dialog's controller table -------------------------------
    // Stride 0x20 (`ADD RDI,0x20`). Argument 4 is the
    // `ExternalPtr<DialogControllerConfig>` (entry+0x10) and argument 5 the
    // `ExternalPtr<ElementDescription>` (entry+0x08): 0x180114a13 /
    // 0x180114a1c, Dialog.h's banner.
    for (int32_t i = 0; i < dlg->_controllerConfigs._size; ++i) {
        const Dialog::ElementController& e = dlg->_controllerConfigs[i];
        AddController(
            e._typeId, _dataSource,
            Object::ExternalPtr<UI::DialogControllerConfig>(e._config),       // entry+0x10 -> R9
            Object::ExternalPtr<UI::ElementDescription>(e._elementDesc),      // entry+0x08 -> a5
            e._name);                                                       // entry+0x18 -> a6
    }
}

}  // namespace UI

// ############################################################################
// ENDGAME B.3.6 block B — StartTimer and the two AddController overloads are
// UI::DialogInstance members now. Bodies unchanged except `self->x` became `x`
// and the calls onto THIS block's classes became member calls:
//   controller vtable +0x20 (slot 4)     -> c->OnCreated()      (VIRTUAL)
//   ResolveAddr(Module::Runtime, 0x1801133b0) -> AddController(...) (4-arg)
// Element / Factory / SpriteSheet / Dialog-temporary calls stay as they were
// (other blocks' classes, or ctor/dtor doors). Evidence: the doors' banners.
// ############################################################################
namespace UI {

// ?StartTimer@DialogInstance@UI@@QEAAXHVTime@System@@@Z   0x1801137c0
// THE FALL-THROUGH IS THE SHIPPED BEHAVIOUR: a matching entry is rewritten
// and ANOTHER record with the same id is appended anyway.
void DialogInstance::StartTimer(int32_t id, int64_t time) {
    for (int32_t i = 0; i < _timers._size; ++i) {
        // _data is RE-READ from the object on every iteration. Kept.
        if (_timers._data[i]._id == id) {
            _timers._data[i]._duration  = time;
            _timers._data[i]._remaining = time;
            // NO `break`, NO `return`.
        }
    }
    // +0x14..0x17 is never written; `tmp` is deliberately left uninitialised
    // there for the same reason as the door's banner gives.
    DialogTimer tmp;
    tmp._duration  = time;
    tmp._remaining = time;
    tmp._id        = id;
    _timers.SetSize(_timers.GetSize() + 1, tmp);   // retail 0x180076250
}

// ?AddController@…@V?$InternalPtr@VElement@UI@@@4@@Z   0x1801133b0
// THE TWO UNGUARDED NULL DEREFERENCES ARE RETAIL'S (a null factory node; a
// non-controller leaves `c` null and the four stores write through it).
void DialogInstance::AddController(uint32_t typeId, Object::Base* dataSource,
                                   Object::ExternalPtr<DialogControllerConfig> config,
                                   Object::InternalPtr<Element> container) {
    // Object::Factory's AVL find, inlined by retail (0x1801133c1).
    ::Object::Factory* const factory = FactorySingleton();
    using TypeNode = ::Object::Factory::TypeSet::Node;
    TypeNode* node = factory ? factory->GetTypes().GetRoot() : nullptr;
    TypeNode* found = nullptr;
    while (node) {
        const uint32_t key = node->element.typeId;
        if (typeId < key)      node = node->left;
        else if (key >= typeId) { found = node; break; }
        else                    node = node->right;
    }

    // 0x1801133f7 `MOV RCX,[RAX+0x10]` with RAX possibly zero. FAITHFUL.
    ::Object::BaseTypeInfo* const info = found->element.record.info;

    // info+0x20 (`factoryFn`, `Base* (*)()`). Retail's RCX still holds `info`
    // at the call; the callee takes no argument and does not read it.
    Object::Base* const created = info->factoryFn();

    const bool isController = created->IsDerivedFrom(UI::DialogController::TypeId());

    // 0x18011341e `CMOVNZ RDI,RBX` — a non-controller leaves the target NULL.
    DialogController* const c =
        isController ? static_cast<DialogController*>(created) : nullptr;

    c->_config     = config;                    // +0x10  0x180113422
    c->_dataSource = dataSource;                // +0x28  0x180113426
    c->_dialog     = this;                      // +0x18  0x18011342a
    c->_container  = container.Get();           // +0x20  0x18011342e

    c->OnCreated();                             // 0x18011343d — controller slot 4

    _controllers.SetSize(_controllers.GetSize() + 1, c);   // retail 0x180076370
}

// ?AddController@…@V?$ExternalPtr@VElementDescription@UI@@@4@AEBVString@System@@@Z
//                                                          0x180113470
void DialogInstance::AddController(uint32_t typeId, Object::Base* dataSource,
                                   Object::ExternalPtr<DialogControllerConfig> config,
                                   Object::ExternalPtr<ElementDescription> description,
                                   const System::String& name) {
    ElementDescription* const desc = description.Get();
    Element* createdElement = nullptr;   // RDI

    if (desc) {
        Element* insertion = _root.Get();     // RDI starts at this->_root (0x1801134b4)

        // ---- phase 1a: does the ROOT's own name match? --------------------
        const char* const want = name._data;
        bool useRoot = false;
        if (want && RtSystem::StringLength(want) != 0) {
            // 0x1801134cb `MOV RCX,[RDI+0x10]` — UNGUARDED on `insertion`.
            const char* const have = insertion->_name;
            const int32_t haveLen = have ? RtSystem::StringLength(have) : 0;
            if (haveLen == RtSystem::StringLength(want)) {
                if (!have) useRoot = true;
                else if (RtSystem::StringLength(have) == 0) useRoot = true;
                else if (System::String::StrCompare(have, want) == 0) useRoot = true;
            }
        }

        // ---- phase 1b: otherwise scan the root's DIRECT children ----------
        if (!useRoot) {
            const int32_t n = insertion->_children._size;
            Element* hit = nullptr;
            if (n > 0) {
                Element** kids = insertion->_children._data;
                for (int32_t i = 0; i < n; ++i) {
                    Element* const child = kids[i];
                    if (child) {
                        Element* out = child->FindElement(name).Get();   // 0x180115650
                        if (out) { hit = out; break; }
                    }
                }
            }
            insertion = hit;          // 0x180113538: a miss zeroes RDI
        }

        // ---- the GroupElement gate ---------------------------------------
        // `insertion` may be null here; retail calls IsDerivedFrom on it anyway
        // (0x180113545 with RCX = 0). NOT RETAIL: a name the layout lacks is a
        // member call through null (UB, a fault in retail); null is tested first.
        Element* const target =
            (insertion != nullptr && insertion->IsDerivedFrom(UI::GroupElement::TypeId()))
                ? insertion : nullptr;

        // ---- phase 2: build the element through a UI::Dialog temporary ----
        alignas(16) uint8_t tmpDialog[0x90];
        UI::Dialog* const tmpDlg = ::new (static_cast<void*>(tmpDialog)) UI::Dialog();

        Element* newEl = nullptr;
        {
            // The description's slot 8 (CreateInstance, RETSLOT, B6-2).
            Object::InternalPtr<Element> slot;
            Object::InternalPtr<Element>* const ret = desc->CreateInstance(&slot, tmpDlg);
            newEl = ret->Get();       // 0x1801135ef `MOV RDI,qword ptr [RAX]`
        }
        createdElement = newEl;

        if (newEl) newEl->_parent = target;   // 0x1801135fc

        // 0x180113603: unguarded on `target` in retail; Element::_children
        // through the kSpec_Fill8Z row (see the door's banner).
        // NOT RETAIL: with no GroupElement under that name (a .rsc or mod
        // layout that lacks it) retail dereferences the null target here and
        // in the walk below (UB, a fault). The element is left unparented
        // instead; the controller below still receives it.
        if (target != nullptr) {
            rtcoll::AsArray<&rtcoll::kSpec_Fill8Z>(&target->_children)
                ->SetSizeFill(target->_children._size + 1, &newEl);

            // 0x180113620: a DO-WHILE (retail faults here on a null target).
            Element* walk = target;
            do {
                walk->_flags |= UI::kElementFlagNeedsSizeCalc;
                walk = walk->_parent;
            } while (walk);
        }

        tmpDlg->UI::Dialog::~Dialog();
    }

    // ---- phase 3: id 0 means "element only, no controller" ---------------
    if (typeId != 0)
        AddController(typeId, dataSource, config,
                      Object::InternalPtr<Element>(createdElement));
}

}  // namespace UI
