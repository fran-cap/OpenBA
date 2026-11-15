// ============================================================================
// src/engine/ui/tooltipdialog.cpp — Wave 10D lane TIP
// ============================================================================
//
// `UI::ToolTipDialog` — the four census rows Wave 10C's lane UIF6 created when
// it ported `?BeginToolTip@UserInterface@UI@@` and resolved that body's callees
// BY NAME rather than opening silent address doors
// (reference_porting_can_grow_the_cutover_surface.md). This file is the
// follow-through, plus the three CODE slots of that class's vtable.
//
//   0x18004cfd0  ??2ToolTipDialog@UI@@SAPEAX_K@Z                (class op new)
//   0x1800356a0  ?GetToolTipString@Element@UI@@QEBA?AVString@System@@XZ
//   0x1800dab00  ?SetText@ToolTipDialog@UI@@QEAAXAEBVString@System@@@Z
//   0x18004cfc0  ?TypeName@ToolTipDialog@UI@@UEBAPEBDXZ     vtable slot 1
//   0x18004cfa0  ?Type@ToolTipDialog@UI@@UEBAIXZ            vtable slot 2
//
// Vtable slot 0 (0x18004d270, the unexported VECTOR deleting destructor) is the
// compiler's, from the class's virtual destructor (ToolTipDialog.h).
//
// ---------------------------------------------------------------------------
// ABSENCE ESTABLISHED BEFORE A LINE WAS WRITTEN
// ---------------------------------------------------------------------------
// All five addresses and all five mangled names were grepped across `code/`,
// `runtime/`, `engine/` and `standalone/` before any of this was written
// (reference_grep_for_the_port_before_writing_it.md — three lanes in one wave
// re-ported what the tree already had, and a duplicate registration is
// last-binding-wins, which silently makes one body dead code). The only hits
// were CONSUMERS: `RT_EXPORT_ONCE` call sites inside
// src/engine/ui/userinterface_hover.cpp, plus one comment. A body that is
// ported but never REGISTERED is indistinguishable from an absent one to a
// census (reference_half_registered_port_reads_as_absent.md), so the grep was
// by ADDRESS *and* by NAME, not by class string.
//
// ---------------------------------------------------------------------------
// `??_7ToolTipDialog@UI@@6B@` IS NOT IN THIS FILE, AND WHY
// ---------------------------------------------------------------------------
// It is exported DATA (0x180157880). A live ToolTipDialog wears the compiler's
// table, from the class in src/engine/ui/ToolTipDialog.h (lane notes:
// docs/history/runtime_port/WAVE10D_LANE_TIP.md). Retail's ten slots are
//
//   slot 0  0x18004d270  vector deleting dtor   -> the compiler's (~ToolTipDialog)
//   slot 1  0x18004cfc0  TypeName               -> DECLARE_TYPE (ToolTipDialog.h)
//   slot 2  0x18004cfa0  Type                   -> DECLARE_TYPE (ToolTipDialog.h)
//   slot 3  0x180114fa0  Persist               |
//   slot 4  0x180114fb0  OnOpened              |
//   slot 5  0x180115330  OnClosed              |  DialogInstance's OWN slot
//   slot 6  0x1801153a0  OnUpdateLayout        }  targets, byte-identical
//   slot 7  0x180115400  OnUpdate              |
//   slot 8  0x180114ed0  OnElementEvent        |
//   slot 9  0x180115470  OnSerializeSettings   |
//
// and slots 3..9 were COMPARED BY ADDRESS against
// `??_7DialogInstance@UI@@6B@` (0x180156d70), not assumed to be inherited.
// All seven match exactly; ToolTipDialog overrides nothing but slots 0/1/2.
// The 10-slot extent is pinned by the adjacent export: 0x180157880 + 10*8 ==
// 0x1801578d0 == `??_7DialogLoaderLoopT@Engine@@6B@`.
//
// ---------------------------------------------------------------------------
// EVERY BODY BELOW WAS READ FROM THE DISASSEMBLY
// ---------------------------------------------------------------------------
// Ghidra's decompiled C drops dereferences and prints MOVSXD as a zero
// extension (reference_ghidra_c_output_vs_disassembly.md). The decompiler
// output was used only to name things; every branch, every offset and every
// signedness below comes from the instruction listing, which is transcribed
// inline against each statement.
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <new>

#include "system/String.h"
#include "ui/Element.h"
#include "ui/TextElement.h"
#include "ui/ToolTipDialog.h"
#include "system/MemoryAllocator.h"
#include "system/string_internal.h"   // RtString_StrCompare

namespace {

using UI::Element;

// ---------------------------------------------------------------------------
// ENTRY COUNTERS — attendance, and NOT a verification of anything.
// ---------------------------------------------------------------------------
// These count ENTRIES, unconditionally, on every build, and print
// unconditionally at exit.
//
// The EFFECTIVENESS CONTROL for each counter is named beside it: a quantity
// that must VARY with the thing being counted, so the instrument cannot report
// a constant and be believed (Wave 10C's DialogController gate counted bodies
// that were registered unconditionally, and only satisfying it exposed that).
uint64_t g_newEntries      = 0;
uint64_t g_newBytes        = 0;   // control: the size retail asks for (0xc0)
uint64_t g_getTipEntries   = 0;
uint64_t g_getTipNonNull   = 0;   // control: took the "element has a string" arm
uint64_t g_setTextEntries  = 0;
uint64_t g_setTextRootHit  = 0;   // control: the ROOT itself was named textToolTip
uint64_t g_setTextFound    = 0;   // control: a text element was found at all
uint64_t g_setTextWrote    = 0;   // control: reached the String::operator= tail

struct TipDlgSummary {
    ~TipDlgSummary() {
        const unsigned long long total =
            g_newEntries + g_getTipEntries + g_setTextEntries;
        if (total == 0) {
            printf("[RtPort] UI::ToolTipDialog ATTENDANCE: none of the three "
                   "bodies was entered in this run.\n");
            fflush(stdout);
            return;
        }
        printf("[RtPort] UI::ToolTipDialog ATTENDANCE (entries, not "
               "comparisons): total=%llu operatorNew=%llu (bytes=%llu) "
               "GetToolTipString=%llu (nonNull=%llu) SetText=%llu (rootHit=%llu "
               "found=%llu wrote=%llu)\n",
               total,
               (unsigned long long)g_newEntries,      (unsigned long long)g_newBytes,
               (unsigned long long)g_getTipEntries,   (unsigned long long)g_getTipNonNull,
               (unsigned long long)g_setTextEntries,  (unsigned long long)g_setTextRootHit,
               (unsigned long long)g_setTextFound,    (unsigned long long)g_setTextWrote);
        fflush(stdout);
    }
};
TipDlgSummary g_summary;

// ---------------------------------------------------------------------------
// Dependencies these bodies CALL rather than reimplement.
// ---------------------------------------------------------------------------

// `?MemAlloc@MemoryAllocator@System@@SAPEAXKH@Z`, 0x1800c7a50. See the
// operator-new banner: the two functions are the SAME nineteen instructions and
// differ only in whether the alignment is a parameter or the immediate 8.
// Called directly (B3R RT-UI).
void* MemAlloc(uint64_t bytes, int align) {
    return System::MemoryAllocator::MemAlloc(bytes, align);
}

// (ENDGAME B.3.9 TI-UI: slots 1/2 and TypeId -- 0x18004cfc0 / 0x18004cfa0 /
// 0x18004cfb0 -- are DECLARE_TYPE's, over the class's own record.)

}  // namespace

// ============================================================================
// ??2ToolTipDialog@UI@@SAPEAX_K@Z                          retail 0x18004cfd0
// ============================================================================
// The class `operator new`. Nineteen instructions:
//
//     PUSH RDI / SUB RSP,0x30 / MOV [RSP+0x20],-2      MSVC unwind marker
//     RDI = [0x1801fc718] + 0x640                      the allocator's lock
//     CALL [0x18014b390]                               EnterCriticalSection
//     MOV R8D,0x8 / RDX=size / RCX=[0x1801fc718]
//     CALL 0x1800c6e60                                 Allocate(alloc,size,8)
//     CALL [0x18014b388]                               LeaveCriticalSection
//     return RAX
//
// **ALIGNMENT IS THE WHOLE PARAMETER, AND IT WAS CHECKED BY BODY.** This is
// instruction-for-instruction `?MemAlloc@MemoryAllocator@System@@` (0x1800c7a50)
// with the alignment argument frozen to the immediate `MOV R8D,0x8`; both were
// disassembled side by side for this lane. Reading one and assuming the other
// matches by resemblance is exactly how this family has been got wrong before
// (src/engine/video/context_ctor.cpp records the same check, and found a
// sibling using align 4).
//
// The lock is NOT reproduced here for the same reason context_ctor.cpp does not
// reproduce it: it is INSIDE MemAlloc, which this body calls.
// ============================================================================
extern "C" void* RtUi_ToolTipDialog_OperatorNew(uint64_t bytes) {
    ++g_newEntries;
    g_newBytes = bytes;                       // retail's only caller passes 0xc0
    return MemAlloc(bytes, 8);                // MOV R8D,0x8
}

// ============================================================================
// ?GetToolTipString@Element@UI@@QEBA?AVString@System@@XZ    retail 0x1800356a0
// ============================================================================
// Returns the element's tooltip-text override by value, so the hidden return
// slot is RDX and the function returns that same pointer in RAX.
//
//     MOV [RDX],RAX          with RAX=0     ret->data = nullptr
//     MOV RAX,[RCX+0x58]                    self->_toolTipString
//     TEST RAX,RAX / JZ out
//     MOV [RDX],RAX                         ret->data = it
//     INC dword ptr [RAX + -0x4]            ... and ADD A REFERENCE
//     MOV RAX,RDX / RET
//
// That is the AddRef half of the copy ctor
// (0x180006cb0) — and `data[-4]` is the REFCOUNT, `data[-8]` the length
// (src/engine/system/String.h). Element +0x58 is `_toolTipString`, pinned by
// `?SetToolTipString@` and asserted in src/engine/ui/Element.h:497.
//
// The `MOV dword ptr [RSP],0/1` pair at 0x1800356b4 / 0x1800356c9 is MSVC's
// SEH state variable for the unwind funclet, not a program value; it is not
// reproduced, and nothing reads it.
//
// CALLER'S OBLIGATION, and it is retail's own: this hands back a reference the
// caller must release. `BeginToolTip` calls it TWICE and releases both
// (userinterface_hover.cpp:366 / :378).
// ============================================================================

// ============================================================================
// ?SetText@ToolTipDialog@UI@@QEAAXAEBVString@System@@@Z      retail 0x1800dab00
// ============================================================================
// Finds the dialog's `textToolTip` element and writes `text` into parameter 0.
// It is `Element::FindElement` with its FIRST LEVEL INLINED, followed by the
// tail of `?SetParameter@TextElement@UI@@` with the index frozen to 0.
//
// STEP 1 (0x1800dab22..0x1800dab37). A local `System::String` is built from the
// literal at 0x180157868, which reads `textToolTip\0` (verified from the DLL
// image, and the vtable this file's banner tabulates begins in the very next
// qword at 0x180157880). `System::String::Set` (0x1800c95d0) is PORTED, so this
// calls the member `Set`.
//
// STEP 2 (0x1800dab3d). `MOV RBX,[RBX+0x50]` — the DialogInstance root element,
// read UNCONDITIONALLY and UNGUARDED. **Left unguarded deliberately.** This port
// is mature enough that a faithful crash here means something upstream handed us
// a ToolTipDialog with a null root, and a guard would skip the behaviour rather
// than restore it (CLAUDE.md, "Don't 'fix' a faithful crash with a null check").
//
// STEP 3 (0x1800dab41..0x1800dab7b) — the inlined self-match on the ROOT. It is
// `FindElement`'s own head test, transcribed:
//   * an EMPTY query never matches: `TEST RDX,RDX / JZ` on the data and
//     `TEST EAX,EAX / JZ` on the length both jump straight to the child scan;
//   * lengths are compared FIRST (`CMP R8D,EAX / JNZ`), and only then, when
//     this element's name is non-null and non-empty, does `StrCompare` run;
//   * a NULL name of matching length (i.e. length 0) counts as a MATCH
//     (`TEST RCX,RCX / JZ found`) — unreachable here only because the length
//     test above has already excluded a zero-length query.
// Reproduced as retail spells it, not as it "meant to".
//
// STEP 4 (0x1800dab7d..0x1800dabb9) — the child scan, which calls the REAL
// `Element::FindElement` (0x180115650, ported) on each non-null child of the
// root and stops at the first hit. Note the loop bound `[RBX+0xa8]` is read into
// EBP ONCE, before the loop, and `JLE` makes it a SIGNED test: a negative child
// count scans nothing.
//
// STEP 5 (0x1800dabbd..0x1800dabee) — the local string is released with retail's
// inlined idiom, INCLUDING its asymmetry: on the freeing branch the object is
// left holding `data - 8`, a dangling pointer, and only the sharing branch nulls
// it. src/engine/system/String.h says in so many words "Reproduce it; do not
// tidy it", and System::String::Release (String.h), run by ~String, is that transcription.
//
// STEP 6 (0x1800dabee..0x1800dac28) — the `SetParameter(0, text)` tail:
//   * guard `CMP dword ptr [RBX+0xd0],0 / JLE` — SIGNED, on the parameter
//     Array<String>'s SIZE (src/engine/ui/TextElement.h);
//   * the dirty walk, a DO-WHILE that ORs 0x8000000 into `_flags` (+0x34) on the
//     found element and then on every ancestor through `_parent` (+0x98);
//   * `System::String::operator=(paramArray[0], text)` — the array's DATA
//     pointer is at +0xc8 and retail passes it with no index arithmetic at all,
//     which is index 0;
//   * `CacheText(found)`.
//
// NO ORACLE, and the reason is the one widgets_layout.cpp already wrote out for
// `SetParameter`: running retail afterwards is VACUOUS (retail's own equality
// check inside `String::operator=` sees the slot already holding `text` and
// takes its own no-op path), and restoring the slot first to make it non-vacuous
// is a use-after-free risk on a shared, refcounted String. The instrument here
// is the attendance counter, which reports what actually ran.
// ============================================================================

// ############################################################################
// ENDGAME B.3.6 block B — UI::ToolTipDialog's members
// (src/engine/ui/ToolTipDialog.h). Bodies unchanged except `self` became
// `this` and +0x50 became the inherited `_root`.
// ############################################################################
namespace UI {

// ?CreateNewObject@ToolTipDialog@UI@@SAPEAVBase@Object@@XZ  0x18004d0b0
// The FACTUI2 row this replaces: the locked MemAlloc of 0xc0 bytes, align 8;
// retail's null branch; then ??0ToolTipDialog (DialogInstance() + the stamp).
Object::Base* ToolTipDialog::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(ToolTipDialog), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) ToolTipDialog();
}
// ?CreateInPlaceObject@ToolTipDialog@UI@@SAPEAVBase@Object@@PEAX@Z  0x18004d140
Object::Base* ToolTipDialog::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) ToolTipDialog();
}

// ---- ?GetToolTipString@Element@UI@@QEBA?AVString@System@@XZ 0x1800356a0 ----
// ENDGAME B.3.6 fragment pass F2: block A's class, its body in this file (the
// disassembly is quoted in the banner above). Returns a NEW reference the caller
// releases; the result starts null (0x1800356b7).
System::String Element::GetToolTipString() const {
    System::String out;                                        // 0x1800356b7
    char* const s = _toolTipString;        // 0x1800356ba, +0x58
    if (s != nullptr) {                                        // 0x1800356be
        ++g_getTipNonNull;
        out._data = s;                                         // 0x1800356c3
        ++RtSystem::StringRefCount(s);                         // 0x1800356c6
    }
    return out;
}

// ??1ToolTipDialog@UI@@UEAA@XZ 0x1800dac70 — restamp + tail-JMP ~DialogInstance,
// both the compiler's (ENDGAME B.3.9 UI-D: the door in deleting_dtor_leaves.cpp
// is retired with the minted table). EMPTY, retail's body.
ToolTipDialog::~ToolTipDialog() {}

// 0x1800daca0 — the 6-argument DialogInstance ctor with dataSource = the
// element (an `Object::Internal`, so an `Object::Base`), userData = 0,
// flags = 0; then the stamp (ToolTipDialog.h).
ToolTipDialog::ToolTipDialog(UserInterface* ui, const Dialog* dialog,
                             Object::InternalPtr<DialogInstance> parent,
                             Object::InternalPtr<Element> element)
    : DialogInstance(ui, dialog, parent, element.Get(), nullptr, 0) {}

// ?SetText@ToolTipDialog@UI@@QEAAXAEBVString@System@@@Z   0x1800dab00 — the
// six steps are the banner above.
void ToolTipDialog::SetText(const System::String& text) {
    // ---- STEP 1 -------------------------------------------------------------
    System::String name;                                       // 0x1800dab22
    name.Set("textToolTip");                                   // 0x1800dab37

    // ---- STEP 2: unguarded, deliberately ------------------------------------
    Element* const root = GetRoot().Get();                     // 0x1800dab3d, +0x50
    Element* found = root;

    // ---- STEP 3: the inlined self-match on the root --------------------------
    bool matched = false;
    if (name._data != nullptr &&                                // 0x1800dab46
        RtSystem::StringLength(name._data) != 0) {              // 0x1800dab4e
        const int32_t want = RtSystem::StringLength(name._data);
        char* const mine = root->_name;    // 0x1800dab52, +0x10
        const int32_t lm = mine ? RtSystem::StringLength(mine) : 0;  // 0x1800dab5b
        if (lm == want) {                                      // 0x1800dab64
            if (mine == nullptr ||                             // 0x1800dab6c
                RtSystem::StringLength(mine) == 0) {           // 0x1800dab6e
                matched = true;
            } else {
                matched = RtSystem::RtString_StrCompare(mine, name._data) == 0;  // 0x1800dab74
            }
        }
    }
    if (matched) ++g_setTextRootHit;

    // ---- STEP 4: the child scan ---------------------------------------------
    if (!matched) {
        found = nullptr;
        const int32_t count = root->_children._size;           // 0x1800dab7f, +0xa8
        for (int32_t i = 0; i < count; ++i) {                  // signed (JLE/JL)
            Element* const child = root->_children._data[i];   // 0x1800dab90, +0xa0
            if (child == nullptr) continue;                    // 0x1800dab93
            // ?FindElement@Element@UI@@ (0x180115650), RETSLOT.
            Element* const hit = child->FindElement(name).Get();   // 0x1800daba2
            if (hit != nullptr) { found = hit; break; }        // 0x1800dabaf
        }
    }
    if (found != nullptr) ++g_setTextFound;

    // ---- STEP 5: `name`'s release is its destructor at the end of the body --

    // ---- STEP 6: the SetParameter(0, text) tail ------------------------------
    // "textToolTip" is the dialog's TextElement (the tooltip asset names it).
    TextElement* const te = static_cast<TextElement*>(found);
    if (te != nullptr && te->_params._size > 0) {              // 0x1800dabee / 0x1800dabf3
        ++g_setTextWrote;
        // ?SetParameter@TextElement@UI@@ (0x18011e9b0) with the index 0, which
        // retail inlines here: the dirty walk up `_parent` (0x1800dac00), the
        // String assignment into `_params[0]` (0x1800dac20), CacheText
        // (0x1800dac28).
        te->SetParameter(0, text);
    }
}

}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::ToolTipDialog's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@ToolTipDialog@UI@@ (0x1801fa4a0): name "ToolTipDialog", parent
// "UI::DialogInstance" (retail ?_ParentTypeName@ToolTipDialog@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::ToolTipDialog, "UI::DialogInstance", nullptr)
