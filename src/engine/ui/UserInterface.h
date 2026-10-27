#pragma once
// ============================================================================
// src/engine/ui/UserInterface.h — binary layout for UI::UserInterface
// ============================================================================
//
// Wave 7 lane C of the Runtime-x64.dll port (docs/runtime_port/PLAN.md §4.2
// wave 7, §6 step 3). The per-function write-up, the oracle argument per
// function and everything this lane held back are in
// docs/history/runtime_port/WAVE7_LANE_C.md.
//
// PORTABILITY: no <windows.h>, per PLAN §4.1 decision 1.
//
// ---------------------------------------------------------------------------
// SIZE: 0x128 bytes (allocated with alignment 4; alignof is 8). PROVEN, NOT INFERRED.
// ---------------------------------------------------------------------------
// Retail allocates it in exactly one place, and spells both numbers out:
//
//     UI::UserInterface::ShowDialog        (0x18011fd10, ord 4034)
//         System::MemoryAllocator::Allocate(_singleton, 0x128, 4)
//         UI::UserInterface::UserInterface(that)
//
// which is the nested modal UI ShowDialog builds for itself. Our own tree
// already independently reached the same pair — src/game/Game/
// gameupdateloop_load.cpp:1065 does `AllocSub(0x128, 4)` before calling the
// ctor through `s_fn_UI_Ctor` — and the ctor's last store lands at +0x120..
// +0x127, one qword short of 0x128.
//
// ---------------------------------------------------------------------------
// THE CLASS IS NOT POLYMORPHIC. There is no vtable pointer at +0x00.
// ---------------------------------------------------------------------------
// This is the single most load-bearing fact in the lane, and it is read out of
// the shipped bytes rather than inferred from the absent `??_7UserInterface`
// export (WAVE7.md fact 2 asked for exactly that check). The ctor at
// 0x180120de0 begins
//
//     180120df1  XOR EAX,EAX
//     180120df3  MOV qword ptr [RCX],RAX        ; _dialogs._head = 0
//     180120df6  MOV qword ptr [RCX+0x8],RAX    ; _dialogs._tail = 0
//
// — its first store is the dialog list's head, not a vtable pointer, and every
// remaining store in the body is a zero or a literal constant. No method of
// the class is virtual: all 52 exported members are `QEAA`/`QEBA`/`CA`, none
// `UEAA`. So PLAN §4.3 risk 3 (vtable ownership) does not arise here, exactly
// as it did not for `Video::Context` in Wave 6 lane A.
//
// ---------------------------------------------------------------------------
// FIELD EVIDENCE
// ---------------------------------------------------------------------------
// A one-line exported accessor is the strongest kind of evidence, because it
// gives the offset AND the field's real name. Ten of the offsets below have
// one; the rest are named by the setter or the loop that owns them.
//
//   off    field                       proven by                       retail
//   0x000  _dialogs (LinkedList)       CleanRemovedDialogs unlink      0x18011f790
//   0x000  _dialogs._head              GetFirstDialog = *(this+0)      0x180031150
//   0x008  _dialogs._tail              GetFirstContainedDialog walks   0x18011fb00
//                                      backwards from *(this+8)
//   0x010  _dialogs._count             ++ / -- around every link edit   0x18011f790
//   0x018  _removedDialogs             ~UserInterface + CleanRemoved-   0x180120b90
//                                      Dialogs iterate it with count
//                                      at +0x20; CloseDialog appends
//   0x028  _addDialogs                 AddDialog pushes to +0x28        0x180120ee0
//   0x030  _addDialogs._size           GetAddDialogCount = *(this+0x30) 0x180031160
//   0x038  _toolTipOwner               BeginToolTip stores *param_2     0x180120860
//   0x040  _toolTipDialog              BeginToolTip stores the new
//                                      ToolTipDialog; CloseDialog and
//                                      Update clear +0x40/+0x38 as a pair
//   0x048  _events (Array, 0x30 each)  CleanRemovedDialogs compacts it  0x18011f790
//                                      with a 0x30 stride; Update and
//                                      CloseDialog push 0x30-byte records
//   0x058  _mouseDownX                 Update, on mouse-button-down     0x1801213c0
//   0x05c  _mouseDownY                 Update, on mouse-button-down     0x1801213c0
//   0x060  _dragDropDialog             Update / CloseDialog end-drag    0x1801213c0
//   0x068  _dragDropElement            Update destroys it (vslot 0)     0x1801213c0
//   0x070  _dragDropId                 DragDropId = *(this+0x70)        0x180031040
//   0x078  _dragDropData               DragDropData = *(this+0x78)      0x180031030
//   0x090  _audioScene                 SetAudioScene                    0x180031050
//   0x098  _sounds[7]                  SetSound writes +0x98 + i*8      0x1800310d0
//   0x0d0  _cursor                     SetCursor                        0x1801225f0
//   0x0d8  _cursorEnabled              EnableCursor                     0x180122580
//   0x0d9  _mouseCaptured              WasMouseCaptured = this[0xd9]    0x180031060
//   0x0dc  _uiScale                    SetUIScale = scale/100.0f        0x180031070
//   0x0e0  _fadeSprite                 SetFullscreenFadeSprite copies   0x18011f560
//                                      both qwords, +0xe0 and +0xe8
//   0x0f0  _fadeVelocity               SetFullscreenFade (+/-1.0f);     0x18011f500
//                                      IsFading is `+0xf0 != 0.0f`      0x180031010
//   0x0f4  _fadeAmount                 SetFullscreenFade reverse-branch 0x18011f500
//   0x0f8  _fadeColor                  SetFullscreenFade 2nd arg        0x18011f500
//   0x0fc  _clipRegion                 GetClipRegion = (this+0xfc)      0x180031000
//   0x110  _spriteCapture              EndSpriteCapture = 0;            0x18011f4a0
//                                      Draw passes it to SpriteContext  0x180121890
//   0x118  _captureViewport            Draw passes (Viewport*)(this+    0x180121890
//                                      0x118) to SpriteContext::Begin
//
// INDEPENDENT CROSS-CHECK from the exe side, written months before this lane
// existed: src/game/Dialog/optionsdialog.cpp's `ApplyKeyMapToUI_Impl`
// (exe 0x140099260) writes UI +0xA8/+0xB0/+0xB8/+0xC0/+0xC8 as five sound
// effects and UI +0xDC as `settings[0] / 100.0f`. Those are sound slots 2..6
// of the `_sounds[7]` array at +0x98 and the `_uiScale` field — both agree
// with the Runtime-side accessors above, from a completely separate source.
//
// ---------------------------------------------------------------------------
// WHAT THE CTOR DOES *NOT* WRITE — 25 bytes, and the oracle depends on it
// ---------------------------------------------------------------------------
// `reference_oracle_must_not_compare_unwritten_bytes.md`: an oracle that
// memcmps bytes NEITHER side writes fails on padding noise and reads like a
// catastrophic port defect. The ctor leaves these ranges untouched:
//
//     0x014..0x017   _dialogs._pad14
//     0x074..0x077   padding after _dragDropId
//     0x089..0x08f   padding after _dragDropActive
//     0x0da..0x0db   padding after _mouseCaptured (the ctor's store at +0xd8
//                    is a WORD, so it covers 0xd8 and 0xd9 and stops)
//     0x0ec..0x0ef   _fadeSprite._pad0c
//     0x10c..0x10f   padding after _clipRegion
//
// The ctor's replay oracle therefore pre-fills BOTH scratch buffers with the
// SAME byte pattern before running either implementation, so those 25 bytes
// compare equal by construction rather than by luck.
// ============================================================================

#include <cstdint>
#include <cstddef>

#include "../collection/Collection.h"

namespace UI {

// ============================================================
// The element type of all three dialog containers.
//
// `Object::InternalPtr<UI::DialogInstance>` is ONE pointer, 8 bytes, whose
// single field IS the object pointer: every body in this class compares
// `*(void**)ptr` against a list element or an array slot directly
// (AddDialog 0x180120ee0, CloseDialog 0x180120f20, PausesGame 0x18011fa10,
// ~UserInterface 0x180120b90), and the LinkedList node stride is 0x18 with
// `prev` at +0x08, which pins sizeof(T) == 8 through
// `Node = align8(sizeof T) + 0x10` (Collection.h).
//
// B.4 P2 (R-UI, H4): spelled as the typed `DialogInstance*` (the one pointer
// the InternalPtr holds); layout unchanged: 8 bytes, node stride 0x18.
// ============================================================
class DialogInstance;
typedef DialogInstance* DialogInstancePtr;

// ============================================================
// UI::UserInterface::ClipRegion — 4 ints, 0x10 bytes, at +0xfc.
//
// Named by its own exported accessor:
//   ?GetClipRegion@UserInterface@UI@@QEBAAEBUClipRegion@12@XZ  0x180031000
//   -> `return (ClipRegion*)(this + 0xfc);`
// and filled by Update (0x1801213c0) and Draw (0x180121890), both of which do
//   *(uint64_t*)(this+0xfc) = 0;            // left = top = 0
//   *(int*)(this+0x104) = scaledWidth - 1;  // right
//   *(int*)(this+0x108) = scaledHeight - 1; // bottom
// so the member order is left, top, right, bottom.
// ============================================================
struct ClipRegion {
    int32_t left;    // +0x00
    int32_t top;     // +0x04
    int32_t right;   // +0x08
    int32_t bottom;  // +0x0c
};
static_assert(sizeof(ClipRegion) == 0x10, "ClipRegion is 4 ints");
static_assert(offsetof(ClipRegion, left) == 0x0, "ClipRegion::left");
static_assert(offsetof(ClipRegion, top) == 0x4, "ClipRegion::top");
static_assert(offsetof(ClipRegion, right) == 0x8, "ClipRegion::right");
static_assert(offsetof(ClipRegion, bottom) == 0xC, "ClipRegion::bottom");

class Element;

// ============================================================
// One queued UI event, 0x30 bytes.
//
// The SIZE is proven — CleanRemovedDialogs (0x18011f790) compacts the +0x48
// array by copying element[i+1] down onto element[i] field by field with a
// 0x30 stride, and both push sites (Update 0x1801213c0, CloseDialog
// 0x180120f20) build a 0x30-byte stack record before calling the array grow
// helper at 0x180033690.
//
// The FIELDS (B.4 W1 R-UI-b) are what UpdateEvents (0x18011fc60) hands to the
// target dialog's `OnElementEvent(InternalPtr<Element>, int, int, int, int,
// int, void*)` (the mangled `HHHHHPEAX` of slot 8): +0x00 the dialog, +0x08
// the element (copied into a fresh InternalPtr), the five dwords +0x10..+0x20
// and the pointer at +0x28. The grow helper's assign copies exactly those
// eight fields and never +0x24 (array.cpp AssignEvent30). The push sites:
// +0x10 is an event id (0xd = drag-drop end, 0xe = drag-drop cancel), +0x14
// is -1, +0x20 copies _dragDropId and +0x28 copies _dragDropData. No retail
// string names the dwords, so they keep typed guess names.
// ============================================================
struct Event {
    DialogInstancePtr dialog; // +0x00  the list's element type (B4 P2 H4: typed)
    Element* element;    // +0x08
    int32_t  eventId;    // +0x10
    int32_t  arg14;      // +0x14
    int32_t  arg18;      // +0x18
    int32_t  arg1C;      // +0x1c
    int32_t  arg20;      // +0x20
    int32_t  _pad24;     // +0x24  never written, never copied
    void*    userData;   // +0x28
};
static_assert(sizeof(Event) == 0x30, "UI event record stride, from CleanRemovedDialogs");
static_assert(offsetof(Event, dialog)   == 0x00, "Event::dialog @ +0x00");
static_assert(offsetof(Event, element)  == 0x08, "Event::element @ +0x08");
static_assert(offsetof(Event, eventId)  == 0x10, "Event::eventId @ +0x10");
static_assert(offsetof(Event, arg14)    == 0x14, "Event::arg14 @ +0x14");
static_assert(offsetof(Event, arg18)    == 0x18, "Event::arg18 @ +0x18");
static_assert(offsetof(Event, arg1C)    == 0x1c, "Event::arg1C @ +0x1c");
static_assert(offsetof(Event, arg20)    == 0x20, "Event::arg20 @ +0x20");
static_assert(offsetof(Event, _pad24)   == 0x24, "Event::_pad24 @ +0x24");
static_assert(offsetof(Event, userData) == 0x28, "Event::userData @ +0x28");


} // namespace UI

// ############################################################################
// UI::UserInterface — THE REAL CLASS (ENDGAME B.3 step 6, block E)
// ############################################################################
//
// NON-POLYMORPHIC ROOT (B36_RULES 2.4): no `??_7UserInterface@UI@@6B@`, no
// `_typeInfo`, every one of the 52 exports is `QEAA`/`QEBA`/`AEAA`/`CA`. The
// Wave 7 POD that used to be spelled here occupied the retail name and was
// field-identical to this class, so it is not moved to `RtUI::` — the POD
// BECOMES the class (B6-4.1, one definition per layout) and every includer
// (`UpdateLoop.h`'s two by-value holders, the exe-tree readers of `_dialogs`,
// `src/diag/mouse_inject.cpp`) compiles against the same fields.
//
// HELD BY VALUE (RULE B6-4.5 / B5-4): `RtEngine::DialogLoopT` (+0x40, pinned by
// `?GetUserInterface@DialogLoopT@Engine@@` 0x18004d7c0) and
// `RtEngine::DialogLoaderLoopT` (+0x60) embed it. So it is CONCRETE, its default
// constructor is `protected` and `= default`, and the two holders are
// `friend`s. That `= default` is NOT retail's constructor: `??0UserInterface@UI@@
// QEAA@XZ` (0x180120de0) seeds `_uiScale = 1.0f`, `_fadeSpriteIndex = -1` and
// `_fadeColor = 0xffffffff` and leaves 25 bytes unwritten, and it STAYS A DOOR
// (`RtUi_UserInterface_Ctor`, userinterface.cpp — B4b-2: a C++ ctor would run a
// second time over what the door seeded). It is constructed on the LOADER
// thread (`src/game/Game/gameupdateloop_load.cpp`, `s_fn_UI_Ctor`) and by
// `ShowDialog` for its nested modal UI — both through the door.
//
// `??1UserInterface@UI@@QEAA@XZ` (0x180120b90) likewise STAYS A DOOR and is NOT
// declared as a C++ destructor: declaring it would replace the implicit
// member-wise destructor that the two by-value holders' own implicit
// destructors name, and nothing destroys a UserInterface in C++.
//
// NOT TRIVIALLY COPYABLE — and it never was. The block prompt asked for
// `static_assert(std::is_trivially_copyable_v<UI::UserInterface>)`; that assert
// is FALSE for this layout and for the POD before it: `_removedDialogs`,
// `_addDialogs` and `_events` are `Collection::Array<T>`, whose destructor is
// user-declared (Collection.h `~Array()`, retail's 0x18001c750 family), exactly
// the reason Camera.h gives for `RendererDrawGroup`. The assert below pins the
// measured half instead, so an edit cannot silently flip it.
//
// ARGUMENT PASSING, read from the RETAIL PROLOGUES (RULE B6-11):
//   * every `Object::InternalPtr<T>` BY-VALUE parameter arrives as the ADDRESS
//     of the caller's temporary and is declared `Object::InternalPtr<T>*`:
//       AddDialog 0x180120ee0            MOV RCX,[RDX]
//       BringDialogToFront 0x180120440   MOV RAX,[RDX]
//       CloseDialog 0x180120f20          MOV RDI,RDX / MOV RBX,[RDX]
//       IsDialogOpen 0x180120410         MOV RCX,[RDX]
//       ShowDialog 0x18011fd10           MOV R14,RDX / MOV RAX,[R14]
//       EndToolTip 0x180120aa0           CMP [RDX],RAX
//       BeginToolTip 0x180120860         MOV RAX,[R8] (element), MOV RCX,[R14] (dialog)
//       BeginDragDrop 0x18011f5f0        MOV RAX,[RDX] (dialog), MOV RAX,[R8] (element)
//       PostEvent 0x1800310e0            MOV RDX,[RDX] (dialog), MOV RAX,[R8] (element)
//   * `Object::ExternalPtr<T>` BY VALUE arrives as the BARE POINTER in the
//     register (GCC passes the trivially copyable 8-byte class the same way), so
//     it is declared by value:
//       SetCursor 0x1801225f0            MOV [RCX+0xd0],RDX, then [RDX+0x20]
//       SetSound 0x1800310d0             MOV [RCX+RAX*8+0x98],R8
//   * the 16-byte by-value classes arrive by hidden pointer to the caller's copy:
//       SetFullscreenFadeSprite 0x18011f560  `UI::Sprite`       MOV RAX,[RDX]
//       BeginSpriteCapture 0x18011f4c0       `Video::Viewport`  MOV EAX,[R8]
//
// HIDDEN RETURN SLOTS (B36_SLOTS sec. 4), all NON-virtual, so the member returns
// by value (both ends are this GCC) and the registered door keeps retail's
// `(this, ret, …)` shape and placement-news the prvalue (B5-12):
//   GetFirstDialog 0x180031150 (`Iterator`), GetFirstContainedDialog 0x18011fb00,
//   SetClipRegion x2 0x180122470 / 0x1801224d0 (`ClipRegion`).
//
// RULE B6-5: no member keeps a static initialiser, a lazy singleton or a
// non-atomic first-call cache. Doors another namespace owns are resolved through
// namespace-scope `std::atomic<void*>` slots (`rtuielem::AtomicExport`).
// ############################################################################

#include <type_traits>

#include "../object/Internal.h"   // B4 Y RT-UI: Object::InternalPtr<T> (IsDialogOpen reads it by name)
#include "../video/Context.h"     // B4 S4 RTUI: Video::Viewport (_captureViewport, by value)
#include "DialogInstance.h"       // B4 S4 RT2: IsDialogOpen's InternalPtr<DialogInstance>::Get() downcast needs the class complete

namespace Object {
template <class T> class ExternalPtr;
}
namespace System { class Time; }
namespace Video  { class Context; struct Viewport; }
namespace Audio  { class Scene; class SoundEffect; }
namespace RtEngine { struct DialogLoopT; struct DialogLoaderLoopT; }
namespace Engine { class DialogLoopT; class DialogLoaderLoopT; }   // ENDGAME B.3.8 RB

namespace UI {

class DialogInstance;
class Element;
class Cursor;
class Sprite;
class SpriteCapture;
class SpriteContext;
class SpriteSheet;
class ToolTipDialog;

class UserInterface {
public:
    // Retail's nested spellings: `UserInterface::ClipRegion` is the mangled
    // `UClipRegion@12`; the Event record and the dialog list's iterator.
    using ClipRegion = ::UI::ClipRegion;
    using Event      = ::UI::Event;
    using Iterator   = Collection::LinkedList<DialogInstancePtr>::Iterator;

    // ---- the 52 exports, in B36_SLOTS sec. 6 order --------------------------
    // ??0UserInterface@UI@@QEAA@AEBV01@@Z  0x180031170 — NOT PORTED, no caller.
    // Declared (and so no C++ code can reach the Arrays' shallow defaulted copy),
    // deliberately undefined.
    UserInterface(const UserInterface& other);
    // ??0UserInterface@UI@@QEAA@XZ  0x180120de0 — DOOR (see banner; protected
    //   `= default` below is not it).
    // ??1UserInterface@UI@@QEAA@XZ  0x180120b90 — DOOR (see banner; not declared).

    void AddDialog(Object::InternalPtr<DialogInstance>* dialog);                 // 0x180120ee0
    void BeginDragDrop(Object::InternalPtr<DialogInstance>* dialog,
                       Object::InternalPtr<Element>* element,
                       int32_t x, int32_t y, uint32_t id, void* data);           // 0x18011f5f0
    void BeginSpriteCapture(SpriteCapture* capture,
                            const Video::Viewport* viewport);                    // 0x18011f4c0
    void BeginToolTip(Object::InternalPtr<DialogInstance>* dialog,
                      Object::InternalPtr<Element>* element,
                      int32_t x, int32_t y);                                     // 0x180120860
    void BringDialogToFront(Object::InternalPtr<DialogInstance>* dialog);        // 0x180120440
    void CancelDragDrop();                                                       // 0x18011f580  not ported
    void CloseDialog(Object::InternalPtr<DialogInstance>* dialog);               // 0x180120f20
    bool DialogContains(int32_t x, int32_t y) const;                             // 0x18011fbb0  not ported
    void* DragDropData() const { return _dragDropData; }                         // 0x180031030  not registered
    uint32_t DragDropId() const { return _dragDropId; }                          // 0x180031040  not registered
    void Draw(Video::Context& context);                                          // 0x180121890
    void DrawDragDrop(SpriteContext& context) const;                             // 0x18011f780  not ported
    void DrawFade(SpriteContext& context, int32_t x, int32_t y,
                  int32_t w, int32_t h) const;                                   // 0x18011f6f0  not ported
    void DrawMouse(SpriteContext& context) const;                                // 0x18011f640  not ported
    void EnableCursor(bool enable);                                              // 0x180122580
    void EndDragDrop();                                                          // 0x180120af0
    void EndSpriteCapture();                                                     // 0x18011f4a0
    void EndToolTip(Object::InternalPtr<Element>* element);                      // 0x180120aa0  not ported
    int32_t GetAddDialogCount() const;                                           // 0x180031160
    const ClipRegion& GetClipRegion() const { return _clipRegion; }             // 0x180031000  not registered
    Object::InternalPtr<DialogInstance> GetFirstContainedDialog(int32_t x,
                                                                int32_t y) const; // 0x18011fb00  RETSLOT
    Iterator GetFirstDialog();                                                   // 0x180031150  RETSLOT
    int32_t GetMouseCursorIndex(int32_t x, int32_t y) const;                     // 0x18011fa40
    // 0x180120410, fourteen instructions, read whole: MOV RAX,[RCX] (the head)
    // / JZ false — the argument is NOT read on an empty list — then
    // MOV RCX,[RDX] (the dialog, through the caller's InternalPtr) and a walk
    // through `next` (+0x10). Not registered (nothing in the tree calls it).
    bool IsDialogOpen(Object::InternalPtr<DialogInstance>* dialog) const {
        auto* node = _dialogs._head;
        if (node == nullptr) return false;
        DialogInstance* const want = dialog->Get();
        do {
            if (node->element == want) return true;
            node = node->next;
        } while (node != nullptr);
        return false;
    }
    // 0x180031010: UCOMISS/JP/JNZ — a NaN velocity reads as fading.
    bool IsFading() const { return !(_fadeVelocity == 0.0f); }                  // 0x180031010  not registered
    bool IsInsideClipRegion(int32_t x, int32_t y) const;                         // 0x180120830
    bool PausesGame() const;                                                     // 0x18011fa10
    void Play(int32_t id);                                                       // 0x1801223d0
    void PostEvent(Object::InternalPtr<DialogInstance>* dialog,
                   Object::InternalPtr<Element>* element,
                   int32_t a, int32_t b, int32_t c, int32_t d, int32_t e,
                   void* userData);                                              // 0x1800310e0  not ported
    void SaveAllSettings();                                                      // 0x1801204b0
    void SetAudioScene(Audio::Scene* scene) { _audioScene = scene; }             // 0x180031050  not registered
    ClipRegion SetClipRegion(const ClipRegion& region);                          // 0x1801224d0  RETSLOT, not ported
    ClipRegion SetClipRegion(int32_t left, int32_t top,
                             int32_t right, int32_t bottom);                     // 0x180122470  RETSLOT, not ported
    void SetCursor(Object::ExternalPtr<Cursor> cursor);                          // 0x1801225f0
    void SetFullscreenFade(bool fadeIn, uint32_t color, bool immediate);         // 0x18011f500
    void SetFullscreenFadeSprite(const Sprite* sprite);                          // 0x18011f560
    void SetSound(int32_t id, Object::ExternalPtr<Audio::SoundEffect> sound);    // 0x1800310d0  not ported
    // 0x180031070: CVTDQ2PS / DIVSS [0x180161714] (100.0f).
    void SetUIScale(int32_t percent) { _uiScale = static_cast<float>(percent) / 100.0f; } // not registered
    int32_t ShowDialog(Object::InternalPtr<DialogInstance>* dialog);             // 0x18011fd10
    // 0x180031090 / 0x1800310b0: CVTDQ2PS, MULSS / DIVSS by _uiScale, CVTTSS2SI.
    int32_t UIScaled(int32_t v) const   { return static_cast<int32_t>(static_cast<float>(v) * _uiScale); }
    int32_t UIUnscaled(int32_t v) const { return static_cast<int32_t>(static_cast<float>(v) / _uiScale); }
    int32_t Update(const System::Time& time);                                    // 0x1801213c0  (main-loop hot path)
    void UpdateEvents();                                                         // 0x18011fc60
    void UpdateFade(const System::Time& time);                                   // 0x18011f990  not ported
    void UpdateToolTip(int32_t x, int32_t y);                                    // 0x180120a70  not ported
    bool WasMouseCaptured() const { return _mouseCaptured != 0; }                // 0x180031060  not registered

    // ---- retail `CA` (private STATIC) and `AEAA` (private) members ----------
    // PUBLIC here, as SpriteContext.h keeps its AEAA members: the registered
    // doors (plain functions) delegate to them, and MainLoop's callback arrays
    // hold the DOORS, whose (context, userData) shape these statics mirror.
    static void DrawStandAloneUI(Video::Context& context, void* userData);       // 0x1801203f0  CA
    static void UpdateStandAloneUI(const System::Time& time, void* userData);    // 0x180120310  CA
    void CleanRemovedDialogs();                                                  // 0x18011f790  AEAA

    // FIX SEAM (fix.ui_scale_layout, NOT RETAIL; DESIGN.md hook rule 5): null by
    // default, the game side points it at the fix's bool (Ext::Fix()). While it
    // reads true, Update lays the dialogs out at the scale an event handler
    // wrote THIS frame (docs/retail_bugs.md section 5). src/engine never
    // includes src/ext.
    static const bool* _extFixScaleLayout;

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/PHONEMENU.md; DESIGN.md hook rule 5): ext.phone_menu.
    // Null by default (always under the faithful profile). One pointer, four call points:
    //   kExtPlaceLayout      Update, before a dialog's ComputeLayout: `rect` holds {0, 0, w, h}
    //                        (retail's call) and may come back inset (a safe area);
    //   kExtPlaceAfterLayout Update, after that ComputeLayout (`rect` as laid out; ignored on
    //                        return): the ext may move elements of the laid-out tree;
    //   kExtPlaceBackdrop    Draw, after the backdrop's cover fit: `rect` holds retail's
    //                        {x1, y1, x2, y2} and may come back moved.
    //   kExtPlaceBackdropLower Draw, just before the backdrop is drawn (P9): `rect` holds the
    //                        backdrop's {x1, y1, x2, y2} (after kExtPlaceBackdrop); a different
    //                        non-empty answer is drawn first, under it (the menus' backdrop's bottom
    //                        border on a wide screen).
    // `width` / `height` are the layout-space screen. src/engine never includes src/ext.
    enum : int32_t {
        kExtPlaceLayout = 0,
        kExtPlaceBackdrop = 1,
        kExtPlaceAfterLayout = 2,
        kExtPlaceBackdropLower = 3
    };
    static void (*_extDialogPlace)(int32_t point, const DialogInstance* dialog, int32_t width,
                                   int32_t height, int32_t* rect);

    // EXT SEAM (NOT RETAIL, docs/android/TOUCH_MODE.md section 2; DESIGN.md hook rule 5): ext.touch_mode.
    // Null by default (always under the faithful profile). A dialog's own unit space: the answer k
    // (1.0 = retail) makes the dialog laid out, hit-tested and drawn as if the UI scale were
    // _uiScale / k (Update: the layout rectangle and the mouse divided by k; Draw: the quads scaled
    // by k through the context's world transform). src/engine never includes src/ext.
    static float (*_extDialogScale)(const DialogInstance* dialog);
    // EXT SEAM (NOT RETAIL, P9, docs/extensions/handins/PHONEMENU.md §3): ext.phone_menu. Null by
    // default (always under the faithful profile). Draw, just AFTER a dialog's backdrop: `rect` holds
    // the backdrop's {x1, y1, x2, y2} as drawn, `sheet` / `sprite` the backdrop's sprite; a true
    // answer draws `sheet` / `sprite` (the backdrop's unless the hook names another) once more at the
    // answered `rect`, over it and before the dialog's elements, clipped to `clip` {x, y, w, h}
    // (layout units, the SpriteContext scissor; w <= 0: no clip). The loading screen's picture gets
    // the menus' see-through overlay, bottom-aligned and clipped to its torn bottom border (P11).
    static bool (*_extBackdropOver)(const DialogInstance* dialog, int32_t width, int32_t height,
                                    int32_t* rect, int32_t* clip, SpriteSheet** sheet, int32_t* sprite);

protected:
    // RULE B5-4 / B6-4.5: NOT retail's ctor (that is the door, see banner). It
    // exists only so the two by-value holders below can declare the member.
    UserInterface() = default;
    // ENDGAME B.3.8 block RB: the two real loop classes' constructors
    // default-construct the member, then run the ctor door on it (InitFields).
    friend class Engine::DialogLoopT;
    friend class Engine::DialogLoaderLoopT;

public:
    // ---- the 0x128 bytes, evidence per field in the FIELD EVIDENCE table ----
    // (public: the exe-tree readers of `_dialogs` and the doors reach them).

    // --- the live dialog stack, front-to-back ---------------------------------
    // _head is the BACKmost dialog and _tail the FRONTmost: BringDialogToFront
    // (0x180120440) re-appends at _tail, GetFirstContainedDialog walks from
    // _tail backwards through `prev` so the topmost dialog wins a hit test,
    // and Draw/PausesGame walk from _head forwards.
    Collection::LinkedList<DialogInstancePtr> _dialogs;          // +0x000, 0x18

    // Dialogs closed this frame. CloseDialog appends here; the next
    // CleanRemovedDialogs unlinks each one from _dialogs, drops any pending
    // event that names it, releases the object unless its flag bit 5 says a
    // nested ShowDialog UI owns it, and then clears the array.
    Collection::Array<DialogInstancePtr>      _removedDialogs;   // +0x018, 0x10

    // Dialogs added since the last Update. AddDialog appends here rather than
    // to _dialogs; Update drains it back-to-front into the list, so the LAST
    // dialog added in a frame ends up FRONTmost.
    Collection::Array<DialogInstancePtr>      _addDialogs;       // +0x028, 0x10

    // The element the tooltip belongs to, and the ToolTipDialog built for it.
    // Always cleared as a pair (BeginToolTip, CloseDialog, Update).
    DialogInstancePtr _toolTipOwner;                             // +0x038
    ToolTipDialog*    _toolTipDialog;                            // +0x040

    Collection::Array<Event> _events;                            // +0x048, 0x10

    int32_t _mouseDownX;                                         // +0x058
    int32_t _mouseDownY;                                         // +0x05c

    DialogInstancePtr _dragDropDialog;                           // +0x060
    Element*          _dragDropElement;                          // +0x068
    uint32_t          _dragDropId;                               // +0x070
    uint32_t          _pad74;                                    // +0x074 never written
    void*             _dragDropData;                             // +0x078
    // The WRITER, BeginDragDrop (0x18011f5f0), fills +0x080 as a PAIR OF int32
    // GRAB OFFSETS: +0x080 = x - element->_layoutLeft and +0x084 = y -
    // element->_layoutTop. The two sites that CLEAR them (EndDragDrop, Update)
    // zero both with one qword store; the tree writes the two named fields in
    // address order (B4_RULES H7).
    int32_t           _dragDropGrabX;                            // +0x080
    int32_t           _dragDropGrabY;                            // +0x084
    // Also UpdateEvents' re-entrancy guard (set for the whole drain).
    uint8_t           _dragDropActive;                           // +0x088
    uint8_t           _pad89[7];                                 // +0x089 never written

    Audio::Scene* _audioScene;                                           // +0x090 Audio::Scene* (B4 P2 H4: typed)
    // ExternalPtr<Audio::SoundEffect>, one per UI sound id. Play(id) reads
    // _sounds[id] and queues it on _audioScene; Update calls Play(0) whenever
    // it drained a non-empty _addDialogs, which is the "dialog opened" sound.
    Audio::SoundEffect* _sounds[7];                              // +0x098..+0x0c8

    Cursor* _cursor;                                             // +0x0d0 ExternalPtr<UI::Cursor>
    uint8_t _cursorEnabled;                                      // +0x0d8
    uint8_t _mouseCaptured;                                      // +0x0d9
    uint8_t _pad0da[2];                                          // +0x0da never written
    float   _uiScale;                                            // +0x0dc init 1.0f

    // UI::Sprite, 0x10 bytes: {ExternalPtr<SpriteSheet>, int32 index, pad},
    // copied wholesale by SetFullscreenFadeSprite (padding included). The
    // ctor's `index = -1` is the engine's empty-sprite value.
    SpriteSheet* _fadeSpriteSheet;                               // +0x0e0  (B4 P2 H4: typed; SetFade copies Sprite::spriteSheet)
    int32_t _fadeSpriteIndex;                                    // +0x0e8 init -1
    int32_t _pad0ec;                                             // +0x0ec never written

    float    _fadeVelocity;                                      // +0x0f0 0 = not fading
    float    _fadeAmount;                                        // +0x0f4 0..1
    uint32_t _fadeColor;                                         // +0x0f8 init 0xffffffff

    ClipRegion _clipRegion;                                      // +0x0fc
    uint32_t   _pad10c;                                          // +0x10c never written

    SpriteCapture* _spriteCapture;                               // +0x110
    // Handed to SpriteContext::Begin by Draw and written dword by dword by
    // BeginSpriteCapture (0x18011f4c0). A trivially copyable POD: the ctor's
    // two-qword zeroing is unchanged.
    Video::Viewport _captureViewport;                            // +0x118
};

static_assert(sizeof(UserInterface) == 0x128,
              "sizeof(UI::UserInterface) is the literal 0x128 in ShowDialog's Allocate");
// The ALLOCATION alignment retail asks for is 4 (`Allocate(_singleton, 0x128, 4)`
// in ShowDialog); the TYPE's alignment is its widest member, the qword pointers.
static_assert(alignof(UserInterface) == 8, "UI::UserInterface is qword-aligned (pointer members)");
static_assert(!std::is_polymorphic_v<UserInterface>, "no vtable: the ctor's first store is _dialogs._head");
static_assert(!std::is_trivially_copyable_v<UserInterface>,
              "Collection::Array's user-declared destructor makes the layout non-trivial "
              "(see banner: the prompt's is_trivially_copyable assert is false for this layout)");

static_assert(offsetof(UserInterface, _dialogs)          == 0x000, "_dialogs at +0x000");
static_assert(offsetof(UserInterface, _removedDialogs)   == 0x018, "_removedDialogs at +0x018");
static_assert(offsetof(UserInterface, _addDialogs)       == 0x028, "_addDialogs at +0x028");
static_assert(offsetof(UserInterface, _toolTipOwner)     == 0x038, "_toolTipOwner at +0x038");
static_assert(offsetof(UserInterface, _toolTipDialog)    == 0x040, "_toolTipDialog at +0x040");
static_assert(offsetof(UserInterface, _events)           == 0x048, "_events at +0x048");
static_assert(offsetof(UserInterface, _mouseDownX)       == 0x058, "_mouseDownX at +0x058");
static_assert(offsetof(UserInterface, _mouseDownY)       == 0x05c, "_mouseDownY at +0x05c");
static_assert(offsetof(UserInterface, _dragDropDialog)   == 0x060, "_dragDropDialog at +0x060");
static_assert(offsetof(UserInterface, _dragDropElement)  == 0x068, "_dragDropElement at +0x068");
static_assert(offsetof(UserInterface, _dragDropId)       == 0x070, "_dragDropId at +0x070");
static_assert(offsetof(UserInterface, _dragDropData)     == 0x078, "_dragDropData at +0x078");
static_assert(offsetof(UserInterface, _dragDropGrabX)    == 0x080, "_dragDropGrabX at +0x080");
static_assert(offsetof(UserInterface, _dragDropGrabY)    == 0x084, "_dragDropGrabY at +0x084");
static_assert(offsetof(UserInterface, _pad74)            == 0x074, "_pad74 at +0x074");
static_assert(offsetof(UserInterface, _pad89)            == 0x089, "_pad89 at +0x089");
static_assert(offsetof(UserInterface, _pad0da)           == 0x0da, "_pad0da at +0x0da");
static_assert(offsetof(UserInterface, _pad0ec)           == 0x0ec, "_pad0ec at +0x0ec");
static_assert(offsetof(UserInterface, _pad10c)           == 0x10c, "_pad10c at +0x10c");
static_assert(offsetof(UserInterface, _dragDropActive)   == 0x088, "_dragDropActive at +0x088");
static_assert(offsetof(UserInterface, _audioScene)       == 0x090, "_audioScene at +0x090");
static_assert(offsetof(UserInterface, _sounds)           == 0x098, "_sounds at +0x098");
static_assert(offsetof(UserInterface, _cursor)           == 0x0d0, "_cursor at +0x0d0");
static_assert(offsetof(UserInterface, _cursorEnabled)    == 0x0d8, "_cursorEnabled at +0x0d8");
static_assert(offsetof(UserInterface, _mouseCaptured)    == 0x0d9, "_mouseCaptured at +0x0d9");
static_assert(offsetof(UserInterface, _uiScale)          == 0x0dc, "_uiScale at +0x0dc");
static_assert(offsetof(UserInterface, _fadeSpriteSheet)  == 0x0e0, "_fadeSpriteSheet at +0x0e0");
static_assert(offsetof(UserInterface, _fadeSpriteIndex)  == 0x0e8, "_fadeSpriteIndex at +0x0e8");
static_assert(offsetof(UserInterface, _fadeVelocity)     == 0x0f0, "_fadeVelocity at +0x0f0");
static_assert(offsetof(UserInterface, _fadeAmount)       == 0x0f4, "_fadeAmount at +0x0f4");
static_assert(offsetof(UserInterface, _fadeColor)        == 0x0f8, "_fadeColor at +0x0f8");
static_assert(offsetof(UserInterface, _clipRegion)       == 0x0fc, "_clipRegion at +0x0fc");
static_assert(offsetof(UserInterface, _spriteCapture)    == 0x110, "_spriteCapture at +0x110");
static_assert(offsetof(UserInterface, _captureViewport)  == 0x118, "_captureViewport at +0x118");

// The three container members must line up with the Collection layouts Wave 2
// recovered; if either header ever drifts, this is where it is caught.
static_assert(sizeof(Collection::LinkedList<DialogInstancePtr>) == 0x18,
              "LinkedList<T> is head/tail/count/pad = 0x18");
static_assert(sizeof(Collection::Array<DialogInstancePtr>) == 0x10,
              "Array<T> is data/size/capacity = 0x10");
static_assert(sizeof(UserInterface::Iterator) == 0x08,
              "GetFirstDialog's by-value Iterator is one node pointer");

} // namespace UI

// ==== B3R W0-P0c: Runtime bodies reached by retail address ====
// ENDGAME B.3R (B3R_RULES R5): each body below is registered at the retail
// address in its trailing comment and read through the registry today. It is
// declared here, copied from its definition (linkage, namespace, types), so the
// reader can call it by name. Nothing calls it through this declaration yet.

// ---- ui/userinterface.cpp
extern "C" void RtUi_UserInterface_Dtor(UI::UserInterface* self);  // 0x180120b90
// B3R RT-Core: the ctor body the Engine dialog loops call by name.
extern "C" UI::UserInterface* RtUi_UserInterface_Ctor(UI::UserInterface* self);  // 0x180120de0
// ==== end B3R W0-P0c ====


// ==== B3R G-Game (R5): Runtime bodies code/Game calls by name ====
extern "C" UI::UserInterface* RtUi_UserInterface_Ctor(UI::UserInterface* self);  // 0x180120de0
// ==== end B3R G-Game ====
