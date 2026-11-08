#pragma once
// ============================================================================
// src/engine/ui/Dialog.h — UI::Dialog, the RESOURCE side of a dialog
// ============================================================================
//
// Wave 7 lane B (docs/history/runtime_port/WAVE7_LANE_B.md).
//
// `UI::Dialog` is the parsed `.rsc` description of a dialog: a root element
// description, a list of controller configurations, a sprite sheet, and three
// lookup arrays (named templates, indexed templates, indexed references).
// `UI::DialogInstance` (DialogInstance.h) is the live object built FROM one.
// One Dialog serves every instance of that dialog.
//
// `??_7Dialog@UI@@6B@` is EXPORTED, at 0x1801562a0.
// `?_typeInfo@Dialog@UI@@0VBaseTypeInfo@Object@@A` — the class's
// `Object::BaseTypeInfo`, used by `IsDerivedFrom` — is exported as a DATA
// symbol at 0x1801faa88 (ordinal 4847). It is not a function and nothing in
// this lane ports it; our tree reads the DLL's.
//
// ---------------------------------------------------------------------------
// WHAT THIS HEADER PROVES, AND WHAT IT DOES NOT
// ---------------------------------------------------------------------------
// This lane ports exactly ONE Dialog method (`?GetTemplate@`), so the layout
// below is deliberately partial: it records every offset three decompiled
// bodies pin, and says "unknown" everywhere else rather than inventing names.
// The three bodies are
//
//   ??0DialogInstance@UI@@QEAA@…@Z   0x180114870  — reads +0x20/+0x28/+0x38/
//                                                   +0x40/+0x48/+0x50
//   ?CreateElement@Dialog@UI@@…      0x180112ed0  — reads +0x58/+0x60
//   ?GetTemplate@Dialog@UI@@…        0x1800388b0  — reads +0x68
//   ?GetReference@Dialog@UI@@…       0x1800388d0  — reads +0x78
//
//   off   field              proven by
//   ----  -----------------  ----------------------------------------------
//   0x00  base, 0x20 bytes   NOT PROVEN by this lane. The first Dialog-owned
//                            field is at +0x20, and Object::External is
//                            exactly 0x20 bytes (src/engine/object/
//                            External.h), so the base is very likely
//                            Object::External — but no body in this lane
//                            touches +0x00..+0x1f, so it is opaque here.
//   0x20  _spriteSheet       DialogInstance ctor copies it to inst+0x58
//   0x28  _spriteName        DialogInstance ctor hands &dialog[0x28] to
//                            SpriteSheet::GetSpriteIndexFromName
//   0x38  _rootDescription   DialogInstance ctor calls its vtable +0x40 with
//                            (out = inst+0x50, this dialog) — i.e. the same
//                            "make me an element" virtual that
//                            ElementDescription::CreateInstance is reached
//                            through in ?CreateElement@
//   0x40  _defaultFlags      DialogInstance ctor seeds inst->_flags from it
//   0x48  _controllerConfigs Array data. The ctor's loop steps 0x20 bytes per
//                            entry and reads four fields out of each — see
//                            DialogControllerConfigEntry below
//   0x50  _controllerCount   the same loop's bound
//   0x58  _namedTemplates    Array data, stride 0x10: {External*, String}.
//                            ?CreateElement@ linear-scans it comparing the
//                            ExternalPtr's path identifier against entry+0x08
//   0x60  _namedTemplateCount that scan's bound
//   0x68  _templates         ?GetTemplate@ is one line:
//                            `*out = ((void**)this[0x68])[index]`
//   0x78  _references        ?GetReference@ is one line:
//                            `*out = ((void**)this[0x78])[index]`
//
// +0x68 and +0x78 are almost certainly `Collection::Array<ExternalPtr<...>>`
// (data / size / capacity), because that is what every other indexed list in
// this class is — but neither accessor bounds-checks, so neither reads the
// size, so neither PROVES a size field is there. They are declared as bare
// pointers plus explicitly-named unknown bytes.
//
// PORTABILITY: no <windows.h>, per PLAN.md §4.1 decision 1.
// ============================================================================

#include <cstdint>
#include <cstddef>

#include "../object/Internal.h"
#include "../object/External.h"

namespace UI { class ElementDescription; class DialogControllerConfig; class SpriteSheet; }

// B4 Y RT-UI: the hand-derived rows UI::Dialog's four arrays are built and
// destroyed through (src/engine/collection/array.cpp), named here so the
// typed members can carry them as their spec.
namespace rtcoll {
extern const ArraySpec kSpec_U64Zero;       // 0x1800492d0 / 0x18002f070  Array<ExternalPtr<T>>
extern const ArraySpec kSpec_ExtStr10at08;  // 0x180075d20  Array<DialogNamedTemplate>
extern const ArraySpec kSpec_CfgEntry20;    // 0x180075b20  Array<DialogControllerConfigEntry>
}  // namespace rtcoll

namespace RtUI {

// One entry of Dialog::_controllerConfigs. Stride 0x20, pinned by the
// DialogInstance ctor's loop (0x180114870), which reads it as
//
//     AddController(inst, entry[0x00], inst->_dataSource,
//                   entry[0x10], entry[0x08], &entry[0x18]);
//
// and whose cursor advances by 0x20 per iteration.
//
// *** THESE TWO FIELDS WERE NAMED BACKWARDS UNTIL 2026-09-08 (lane DLG2). ***
// The header shipped `_config` at +0x08 and `_elementDesc` at +0x10, with a
// comment above it rationalising the swap as "the config is passed AFTER the
// description". That comment was the error, not a caveat about it — and lane
// UI3 had to work around it at its own call site rather than fix it here. THE
// SHIPPED BYTES SETTLE IT:
//
//     180114a07  MOV EDX,[R9]           -> arg 1, the type id
//     180114a0a  LEA RAX,[R9+0x18]
//     180114a0e  MOV [RSP+0x28],RAX     -> arg 6 (5th after `this`), String&
//     180114a13  MOV RAX,[R9+0x08]
//     180114a17  MOV [RSP+0x20],RAX     -> arg 5  *** entry+0x08 ***
//     180114a1c  MOV R9,[R9+0x10]       -> arg 4  *** entry+0x10 ***
//     180114a20  MOV R8,[RSI+0x30]      -> arg 3, the data source
//     180114a27  CALL 0x180113470
//
// and the callee's own mangled name gives the parameter order:
//   ?AddController@DialogInstance@UI@@QEAAX I PEAVBase@Object@@
//        V?$ExternalPtr@VDialogControllerConfig@UI@@@4@     <- arg 4
//        V?$ExternalPtr@VElementDescription@UI@@@4@         <- arg 5
//        AEBVString@System@@@Z                              <- arg 6
//
// arg 4 is the CONFIG and it comes from entry+0x10; arg 5 is the
// ELEMENT DESCRIPTION and it comes from entry+0x08. Both are read as 8-byte
// VALUES (`MOV RAX,[R9+8]`), which is also what makes `ExternalPtr<T>` an
// 8-byte register-passed type here. Do not re-swap these on the strength of
// the reading order in the source; pin to 0x180114a13 / 0x180114a1c.
struct DialogControllerConfigEntry {
    uint32_t _typeId;        // +0x00
    uint32_t _pad04;         // +0x04  never read
    UI::ElementDescription*     _elementDesc;   // +0x08  ExternalPtr<ElementDescription>  -> arg 5 (B4 H4)
    UI::DialogControllerConfig* _config;        // +0x10  ExternalPtr<DialogControllerConfig> -> arg 4 (B4 H4)
    // B4 Y RT-UI: typed. The element is only ever built, copied and
    // destroyed by kSpec_CfgEntry20's arms (assign 0x180075bc8, init
    // 0x180075c00, destroy 0x180039580), which store `_name._data` raw; no
    // C++ construction or destruction of an entry runs, so the name is
    // released once.
    System::String _name;    // +0x18
};
static_assert(offsetof(DialogControllerConfigEntry, _elementDesc) == 0x08,
              "config entry: the DESCRIPTION is at +0x08 (0x180114a13 -> arg 5)");
static_assert(offsetof(DialogControllerConfigEntry, _config) == 0x10,
              "config entry: the CONFIG is at +0x10 (0x180114a1c -> arg 4)");
static_assert(sizeof(DialogControllerConfigEntry) == 0x20,
              "Dialog::_controllerConfigs stride is 0x20 (ctor 0x180114870 loop)");
static_assert(offsetof(DialogControllerConfigEntry, _typeId) == 0x0, "DialogControllerConfigEntry::_typeId");
static_assert(offsetof(DialogControllerConfigEntry, _pad04) == 0x4, "DialogControllerConfigEntry::_pad04");
static_assert(offsetof(DialogControllerConfigEntry, _name) == 0x18, "DialogControllerConfigEntry::_name");

// One entry of Dialog::_namedTemplates. Stride 0x10, pinned by
// ?CreateElement@ (0x180112ed0): the scan starts at `data + 8`, compares that
// String against the requested path identifier, steps 0x10 per iteration, and
// on a hit re-indexes `data + i*0x10` to read the ExternalPtr at +0x00.
struct DialogNamedTemplate {
    UI::ElementDescription* _description;   // +0x00  ExternalPtr<ElementDescription> (B4 H4)
    // B4 Y RT-UI: typed; built / destroyed only by kSpec_ExtStr10at08's arms
    // (assign 0x180075dc4, init 0x180075dee, destroy 0x180039640).
    System::String _name;   // +0x08
};
static_assert(sizeof(DialogNamedTemplate) == 0x10,
              "Dialog::_namedTemplates stride is 0x10 (CreateElement 0x180112ed0)");
static_assert(offsetof(DialogNamedTemplate, _description) == 0x0, "DialogNamedTemplate::_description");
static_assert(offsetof(DialogNamedTemplate, _name) == 0x8, "DialogNamedTemplate::_name");




// THE sizeof ASSERT, AND WHY IT IS NOW ALLOWED — WAVE 10C LANE DLG8.
//
// This header carried an explicit REFUSAL to assert a size, and the refusal was
// correct as written: lane DLG2's evidence was the default ctor's last store at
// +0x80, and a constructor's WRITTEN EXTENT is a LOWER BOUND on a class size,
// never the size itself — trailing members a ctor leaves alone are invisible to
// it. A written extent belongs to a WRITER, and "never written" is meaningless
// without "by whom" (reference_never_written_needs_by_whom.md).
//
// The new evidence is a DIFFERENT KIND of witness, not a bigger pile of the
// same kind. `??_EDialog@UI@@` — slot 0 of `??_7Dialog@UI@@6B@`, at
// 0x180038b20 — passes MSVC's `eh vector destructor iterator` the stride it
// must walk a `Dialog[]` with:
//
//     180038b3f   MOV EDX,0x88
//
// That is `sizeof(UI::Dialog)` by definition: it is what the compiler itself
// believes the distance between two adjacent Dialog objects is. It is a
// measurement, and it happens to agree with DLG2's lower bound exactly, which
// is the interesting part — there are no trailing members.
//
// Re-derived from the shipped DLL, independently of this comment, by
// tools/rt_deleting_dtor_verify.py (row "UI::Dialog": `size=0x88`), and that
// script has a negative control that perturbs a row and must report exactly one
// problem.
//
// The ONE FOUR-BYTE HOLE: +0x44..+0x47. 0x180112e0b is a DWORD store to +0x40
// and the next store is a QWORD at +0x48, so retail never writes +0x44 in the
// ctor. `src/engine/ui/dialog_ctor_dtor.cpp`'s oracle excludes those four
// bytes from its compare and says so; a port that zeroed +0x40 as a qword
// would be writing over caller stack retail leaves alone.

}  // namespace RtUI

// ############################################################################
// UI::Dialog — THE REAL CLASS (ENDGAME B.3 step 6, block B)
// ############################################################################
//
// The `RtUI::Dialog` POD above stays, field-identical and cross-asserted: the
// ctor/dtor/factory DOORS (dialog_ctor_dtor.cpp, dialog_factory.cpp) and the
// stack temporaries in dialog.cpp / dialog_create.cpp build through it.
//
// PARENT: `Object::External` — the "base, 0x20 bytes, NOT PROVEN" note above
// is now proved by
//   0x180038410  ?_ParentTypeName@Dialog@UI@@CAPEBDXZ -> "Object::External"
//
// `??_7Dialog@UI@@6B@` @ 0x1801562a0, NINE slots (B36_SLOTS.md sec. 1):
//
//   [0] +0x00  0x180038b20  (unexported) vector deleting dtor
//   [1] +0x08  0x180038440  ?TypeName@Dialog@UI@@UEBAPEBDXZ
//   [2] +0x10  0x180038420  ?Type@Dialog@UI@@UEBAIXZ
//   [3] +0x18  0x1801130b0  ?GetFileVersion@Dialog@UI@@UEBAHXZ   (MOV EAX,6)
//   [4] +0x20  0x1800066e0  ?Compile@External@Object@@  INHERITED — not declared
//   [5] +0x28  0x1801130c0  ?Serialize@Dialog@UI@@UEAA_NAEAVStream@IO@@@Z
//   [6] +0x30  0x1800386b0  ?Reconstruct@Dialog@UI@@UEAAPEAVExternal@Object@@XZ
//   [7] +0x38  0x1800387f0  ?Destruct@Dialog@UI@@UEAAXXZ
//   [8] +0x40  0x180038750  ?Construct@Dialog@UI@@UEAAPEAVExternal@Object@@XZ
//                           — INTRODUCED HERE (RULE B6-1.1: slot 8 on the
//                           External resources)
//
// ENDGAME B.3 step 9 (block UI-D, B39_RULES D1/D2): CONCRETE. The real public
// ctor is retail's `??0Dialog@UI@@QEAA@XZ` (0x180112db0) minus its three vptr
// stores; slot 0 is the class's own vector deleting dtor; ~Dialog is
// public, virtual and holds the retired ??1 door's body
// (dialog_ctor_dtor.cpp). Construct/Reconstruct and the stack temporaries
// retail inlines (AddController, ElementDescription::CreateInstance,
// ComboElement::Update, TabElement::AddTab) spell `::new (p) UI::Dialog()` and
// destroy with `~T()` (slot 0, flags 0). The factory pair is the class's own
// (dialog_factory.cpp); the minted `??_7Dialog@UI@@6B@` (manifest WITHHELD)
// and its RT_DELDTOR row are retired with the ??0/??1 doors.
// ############################################################################

namespace System { class String; }

namespace UI {

class Element;              // block A
class ElementDescription;   // block A
class Sprite;               // block D

class Dialog : public Object::External {
public:
    // Retail's `Dialog::ElementController` (the `AEBUElementController@12@`
    // that `?GetController@Dialog@` returns, indexed below
    // `?GetControllerCount@`) is the 0x20-byte `_controllerConfigs` entry.
    // INFERRED from those two exports' shapes, not from a body read.
    using ElementController = RtUI::DialogControllerConfigEntry;

    // ??0Dialog@UI@@QEAA@XZ 0x180112db0 — the REAL constructor: External()
    // (+0x08..+0x18), then +0x20..+0x80 zero EXCEPT +0x30 = -1 (the background
    // colour) and +0x44..+0x47, which retail NEVER writes (+0x40 is a DWORD
    // store and the next is a QWORD at +0x48).
    Dialog() : Object::External() {
        _spriteSheet           = nullptr;                   // 180112df5
        // 180112df9 (+0x28 = 0): the System::String member's default ctor,
        // run before this body (B4 X-RH).
        _backgroundColor       = System::kColorWhite;       // 180112dfd  -1, NOT 0
        _drawLevel             = 0;                         // 180112e04
        _rootDescription       = nullptr;                   // 180112e07
        _defaultFlags          = 0;                         // 180112e0b  DWORD
        // 180112e0e .. 180112e2a (+0x48 .. +0x84 zero): the four Array
        // members' own Array() (B4 Y RT-UI), run before this body.
    }

    // [0] 0x180038b20 — MSVC's VECTOR deleting destructor, both arms exactly as
    // the retired RT_DELDTOR row ran them (`MOV EDX,0x88`, the element size).
    // `??1Dialog@UI@@UEAA@XZ` 0x180112e40 — the four ~Array (+0x78, +0x68,
    // +0x58, +0x48), then the +0x28 String release, then ~External: every one
    // of them a member destructor run in reverse declaration order after the
    // empty body (dialog_ctor_dtor.cpp; B4 X-RH, B4 Y RT-UI).
    ~Dialog() override;

    // ENDGAME B.3.9 TI-UI: ONE reflection block (B39 D6); `_typeInfo` is the
    // class's own record (DEFINE_TYPE in the class's TU), no longer minted.
    // retail: TypeName 0x180038440, Type 0x180038420, TypeId 0x180038430, CreateNewObject 0x180038530
    //         CreateInPlaceObject 0x180038610
    DECLARE_TYPE(Dialog)
public:
    int32_t     GetFileVersion() const override;                  // [3] 0x1801130b0
    bool        Serialize(IO::Stream& stream) override;                 // [5] 0x1801130c0
    Object::External* Reconstruct() override;                    // [6] 0x1800386b0
    void        Destruct() override;                              // [7] 0x1800387f0
    virtual Object::External* Construct();                        // [8] 0x180038750

    // ---- PORTED non-virtual members ---------------------------------------
    // RETSLOT in retail; plain by-value returns here, the doors placement-new.
    Object::ExternalPtr<ElementDescription> GetTemplate(int32_t index) const;  // 0x1800388b0
    Object::ExternalPtr<Object::External> GetReference(int32_t index) const;   // 0x1800388d0
    Object::InternalPtr<Element> CreateElement(
        Object::ExternalPtr<ElementDescription> description) const;           // 0x180112ed0

    // ---- exported, NOT PORTED: declared, deliberately undefined -----------
    Object::InternalPtr<Element> CreateElements() const;                       // 0x180112c80
    Sprite   GetBackgroundSprite() const;                                      // 0x180038860
    uint32_t GetBackgroundColor() const;                                       // 0x1800388a0
    const ElementController& GetController(int32_t index) const;               // 0x180038900
    int32_t  GetControllerCount() const;                                       // 0x1800388f0
    int32_t  GetDrawLevel() const;                                             // 0x180038910
    uint32_t GetFlags() const;                                                 // 0x180038850
    bool     IsFirstDraw() const;                                              // 0x180038840
    bool     IsModal() const;                                                  // 0x180038800
    bool     NoMouseCapture() const;                                           // 0x180038820
    bool     PausesGame() const;                                               // 0x180038810
    bool     PerElementCapture() const;                                        // 0x180038830
    Dialog(const Dialog& other);                                               // 0x180038920
    Dialog& operator=(const Dialog& other);                                    // 0x180038a30
    // MemoryAllocator statics (B36_RULES 7). Placement is spelt
    // `::new (p) Dialog()` (B9-5): these stay undefined.
    static void* operator new(std::size_t bytes);                 // 0x180038450
    static void* operator new(std::size_t bytes, void* place);    // 0x1800384c0
    static void  operator delete(void* p) { ::Object::detail::InternalMemFree(p); }                        // 0x1800384d0
    // Retail's factory pair (B39_RULES D2): MemAlloc(0x88, 8) + the ctor, and
    // the null-tested in-place ctor. Defined and registered in dialog_factory.cpp.

    // RULE B5-15 / B6-7, COPIED from B36_SLOTS section 3:
    //   0x180038400  ?_TypeName@       -> "Dialog"
    //   0x180038410  ?_ParentTypeName@ -> "Object::External"

public:
    // ---- the 0x68 bytes Dialog adds (evidence: the POD's banner) ---------
    SpriteSheet* _spriteSheet;         // +0x20  (B4 H4: typed)
    System::String _spriteName;        // +0x28  ctor 0x180112df9, dtor 0x180112e88
    uint32_t _backgroundColor;         // +0x30  ctor = 0xFFFFFFFF
    int32_t  _drawLevel;               // +0x34
    ElementDescription* _rootDescription; // +0x38  ExternalPtr<ElementDescription> (B4 H4)
    uint32_t _defaultFlags;            // +0x40
    uint32_t _pad44;               // +0x44  never written by the ctor
    // B4 Y RT-UI: the four arrays are real members, each on retail's own
    // hand-derived row (SetSize / ~Array):
    //   +0x48  0x180075b20 / 0x180039280   +0x58  0x180075d20 / 0x180039360
    //   +0x68  0x1800492d0 / 0x180039440   +0x78  0x18002f070 / 0x180009460
    // Declared in address order, so the compiler destroys them +0x78, +0x68,
    // +0x58, +0x48 -- retail's dtor 0x180112e40 order -- then `_spriteName`.
    using ControllerArray = Collection::Array<ElementController, Collection::DefaultAllocator,
                                              Collection::ExplicitSpec<&rtcoll::kSpec_CfgEntry20>>;
    using NamedTemplateArray = Collection::Array<RtUI::DialogNamedTemplate, Collection::DefaultAllocator,
                                                 Collection::ExplicitSpec<&rtcoll::kSpec_ExtStr10at08>>;
    using TemplateArray = Collection::Array<ElementDescription*, Collection::DefaultAllocator,
                                            Collection::ExplicitSpec<&rtcoll::kSpec_U64Zero>>;
    using ReferenceArray = Collection::Array<Object::External*, Collection::DefaultAllocator,
                                             Collection::ExplicitSpec<&rtcoll::kSpec_U64Zero>>;
    ControllerArray    _controllerConfigs;   // +0x48  count +0x50
    NamedTemplateArray _namedTemplates;      // +0x58  count +0x60
    TemplateArray      _templates;           // +0x68  ExternalPtr<ElementDescription>[], count +0x70
    ReferenceArray     _references;          // +0x78  ExternalPtr<Object::External>[], count +0x80
};

static_assert(sizeof(Dialog) == 0x88,
              "UI::Dialog is 0x88 bytes (MOV EDX,0x88 at 0x180038b3f)");
static_assert(offsetof(Dialog, _spriteName) == 0x28, "Dialog::_spriteName");
static_assert(offsetof(Dialog, _backgroundColor) == 0x30, "Dialog::_backgroundColor");
static_assert(offsetof(Dialog, _drawLevel) == 0x34, "Dialog::_drawLevel");
static_assert(offsetof(Dialog, _rootDescription) == 0x38, "Dialog::_rootDescription");
static_assert(offsetof(Dialog, _defaultFlags) == 0x40, "Dialog::_defaultFlags");
static_assert(offsetof(Dialog, _pad44) == 0x44, "Dialog::_pad44");
static_assert(offsetof(Dialog, _controllerConfigs) == 0x48, "Dialog::_controllerConfigs");
static_assert(offsetof(Dialog, _namedTemplates) == 0x58, "Dialog::_namedTemplates");
static_assert(offsetof(Dialog, _templates) == 0x68, "Dialog::_templates");
static_assert(offsetof(Dialog, _references) == 0x78, "Dialog::_references");
static_assert(alignof(Dialog) == 8, "qword-aligned (the vptr)");
static_assert(offsetof(Dialog, _spriteSheet) == sizeof(Object::External),
              "Dialog's own fields begin at the end of the Object::External base");
#define B36_DLG_PIN(f, off) \
    static_assert(offsetof(Dialog, f) == off, "Dialog::" #f " @ " #off);
B36_DLG_PIN(_spriteSheet,        0x20)
B36_DLG_PIN(_spriteName,         0x28)
B36_DLG_PIN(_backgroundColor,    0x30)
B36_DLG_PIN(_drawLevel,          0x34)
B36_DLG_PIN(_rootDescription,    0x38)
B36_DLG_PIN(_defaultFlags,       0x40)
B36_DLG_PIN(_controllerConfigs,  0x48)
B36_DLG_PIN(_namedTemplates,     0x58)
B36_DLG_PIN(_templates,          0x68)
B36_DLG_PIN(_references,         0x78)
#undef B36_DLG_PIN

}  // namespace UI
