#pragma once
// ============================================================================
// src/game/Tutorial/TutorialImage.h — ENDGAME B.3.8 block GOAL (B38_RULES D1-D5)
//
// An image shown in a tutorial step's panel (and the base of three variants).
//
// RETAIL (Application-x64.exe, B38_SLOTS.md sections 1 and 6):
//   reflected "TutorialImage", parent "Object::External", ti 0x1401c6588
//   create 0x140027980 (MemAlloc(0x30, 8)), createInPlace 0x1400279e0
//   table 0x140160a40, 10 slots (CONCRETE, and the base of three leaves):
//     [0] 0x140027a80 scalar deleting dtor: stamp, ~External, MemFree on &1
//     [1] TypeName  [2] Type  [3] 0x1400e03a0 GetFileVersion `MOV EAX,0x1000`
//     [4] Object::External::Compile (import) — inherited
//     [5] 0x1400e0330 Serialize
//     [6] 0x140027a30 Reconstruct: this->~T(), then the ctor on `this`
//     [7] 0x140001640 Destruct: this->~T()
//     [8] 0x1400279e0 Construct (folded with createInPlace) — NEW here
//     [9] 0x1400e0300 CreateElement — NEW here: builds the image's UI element
//         in a dialog and returns it BY VALUE through the hidden return slot
//         (RCX this, RDX ret, R8 dialog; `return param_2`).
//         TutorialInstance::RefreshStepUI calls it as `(img, retbuf, dialog)`. B8-11 shape: the slot is
//         spelled with the return slot as an explicit first parameter.
//   sizeof 0x30.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include "object/External.h"
#include "Object/exe_type.h"

#include "object/Internal.h"   // Object::InternalPtr

namespace UI { class ElementDescription; class Element; class Dialog; }

class TutorialImage : public Object::External {
    DECLARE_EXE_TYPE(TutorialImage)

public:
    TutorialImage();                                               // create 0x140027980 (inlined)

    ~TutorialImage() override = default;                           // [0] 0x140027a80
    int32_t           GetFileVersion() const override { return 0x1000; }  // [3] 0x1400e03a0
    bool              Serialize(IO::Stream& stream) override;            // [5] 0x1400e0330
    Object::External* Reconstruct() override;                      // [6] 0x140027a30
    void              Destruct() override;                         // [7] 0x140001640
    virtual Object::External* Construct();                         // [8] 0x1400279e0
    // [9] 0x1400e0300: by value in retail (retslot, B6): builds the image's element.
    virtual Object::InternalPtr<UI::Element>* CreateElement(Object::InternalPtr<UI::Element>* ret,
                                                            const UI::Dialog* dialog);

public:
    UI::ElementDescription* imageRef; // +0x20: ExternalPtr<UI::ElementDescription> (the image)
    int32_t  imageWidth;         // +0x28: serialized int
    int32_t  imageHeight;        // +0x2C: serialized int
};

static_assert(offsetof(TutorialImage, imageRef) == 0x20, "TutorialImage::imageRef offset");
static_assert(offsetof(TutorialImage, imageWidth) == 0x28, "TutorialImage::imageWidth offset");
static_assert(offsetof(TutorialImage, imageHeight) == 0x2C, "TutorialImage::imageHeight offset");
static_assert(sizeof(TutorialImage) == 0x30, "TutorialImage is 0x30 (create 0x140027980)");
static_assert(alignof(TutorialImage) == 8, "qword-aligned");
