// src/game/Dialog/spritecaptureui.cpp
// Clean reimplementation of SpriteCaptureUI from Application-x64.exe.
//
// SpriteCaptureUI (0x70 bytes), vtable at 0x14015d4f0.
// Extends DialogController (0x30 base). Dev tool for capturing UI element sprites.
// Fields: Dialog/SpriteCaptureUI.h.
//
// OnCreated (vtable +0x20, 0x140079640):
//   Finds 3 elements: "editCapture", "buttonCapture", "captureBox".
//   Sets button ID 1000, default capture path, initializes capture state.
//
// OnUpdate (vtable +0x40, 0x1400793a0) — per-frame update:
//   Handles mouse drag for capture box. When pendingExternal set and
//   saveFrameCount reaches 1, calls EndSpriteCapture, builds save path from
//   FileSystem::GetSourceDirectory + edit text + ".rsc", calls External::SaveText.

#include "Dialog/Panels.h"
#include "Dialog/SpriteCaptureUI.h"
#include "system/Value.h"
#include "ui/UserInterface.h"
#include "object/TypeInfo.h"
#include "system/String.h"
#include "system/Filename.h"
#include "object/External.h"   // ENDGAME B.3.3 step 6A — Object::External
#include "io/FileSystem.h"     // ENDGAME B.3.4 caller batch P
#include <cstdint>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "ui/Element.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/DialogInstance.h"
#include "ui/UserInterface.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/EditElement.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "input/Input.h"  // ENDGAME B.3.7 callers (K3)
#include "ui/ButtonElement.h"  // X::TypeId() (B.3.9 CG2)
#include "Component/CaptureBoxElement.h"   // X::TypeId() (B.3.9 CG2)
#include "input/Device.h"   // ENDGAME B.3.9 SS2: Input::Device::_singleton
#include "ui/SpriteCapture.h"          // UI::SpriteCapture (StartCapture)
#include "system/MemoryAllocator.h"
#include "video/Context.h"             // Video::Viewport
#include <new>

// The element named `name` under the panel's root, if it is a `typeId`.
static UI::Element* FindTyped(UI::Element* root, const char* name, uint32_t typeId) {
    System::String nameStr(name);
    UI::Element* elem = root->FindElement(nameStr).Get();
    if (elem && ::Object::IsDerivedFromNullSafe(elem, typeId))
        return elem;
    return nullptr;
}

// ============================================================
// SpriteCaptureUI::OnCreated (vtable +0x20, 0x140079640)
// ============================================================
// [4] 0x140079640
void SpriteCaptureUI::OnCreated() {
    SpriteCaptureUI* self = this;
    // --- the three elements, each checked with IsDerivedFrom ---
    self->editCapture = static_cast<UI::EditElement*>(
        FindTyped(self->_container, "editCapture", UI::EditElement::TypeId()));
    // ENDGAME B.3.9 CG2: TypeId_HelpButtonElem shares its initialiser (exref
    // 0x1401514f8) with TypeId_ButtonElement -> UI::ButtonElement::TypeId()
    // (Table 3 name lie: this is not a distinct "help button" element class).
    self->buttonCapture = static_cast<UI::ButtonElement*>(
        FindTyped(self->_container, "buttonCapture", UI::ButtonElement::TypeId()));
    self->captureBox = static_cast<CaptureBoxElement*>(
        FindTyped(self->_container, "captureBox", CaptureBoxElement::TypeId()));

    // --- Set button element ID to 1000 ---
    self->buttonCapture->SetElementId(1000);

    // --- Set default capture path in edit element ---
    {
        System::String pathStr("Game/Help/Dialog/");
        self->editCapture->SetText(pathStr);
    }

    // --- Initialize capture state fields ---
    self->curX = 0;
    self->curY = 0;
    self->anchorX = 100;
    self->anchorY = 100;
    self->dragging = 0;
    self->pendingExternal = nullptr;
}

// ============================================================
// SpriteCaptureUI::OnUpdate (vtable +0x40, 0x1400793a0)
// Per-frame update: drag capture box with left mouse + key 0x3E.
// When pendingExternal is set, waits one frame then saves.
// ============================================================
// [8] 0x1400793a0
void SpriteCaptureUI::OnUpdate(const System::Time& /*time*/) {
    SpriteCaptureUI* self = this;
    // Input::Device singleton, read unguarded (0x1400793a0)
    const Input::Device* inputDevice = ::Input::Device::_singleton;

    // 1. If currently dragging, check if left mouse button (axis 4) was released
    if (self->dragging) {
        short btnHeld = inputDevice->GetValue(Input::Device::InputDevice::Mouse, 0,
                                              ToUnderlying(Input::Device::MouseInput::Left));
        if (btnHeld == 0) {
            self->dragging = 0;
            goto check_pressed;
        }
    }

    // 2. While dragging, update anchor position to current mouse pos
    if (self->dragging) {
        short mx = inputDevice->GetValue(Input::Device::InputDevice::Mouse, 0,
                                         ToUnderlying(Input::Device::MouseInput::X));
        short my = inputDevice->GetValue(Input::Device::InputDevice::Mouse, 0,
                                         ToUnderlying(Input::Device::MouseInput::Y));
        self->anchorX = (int)mx;
        self->anchorY = (int)my;
    }

check_pressed:
    // 3. Check for new capture start: left mouse pressed + key 0x3E held
    if (inputDevice->Pressed(Input::Device::InputDevice::Mouse, 0, ToUnderlying(Input::Device::MouseInput::Left))) {
        short keyHeld = inputDevice->GetValue(Input::Device::InputDevice::Keyboard, 0, 0x3e);
        if (keyHeld != 0) {
            short mx = inputDevice->GetValue(Input::Device::InputDevice::Mouse, 0,
                                             ToUnderlying(Input::Device::MouseInput::X));
            short my = inputDevice->GetValue(Input::Device::InputDevice::Mouse, 0,
                                             ToUnderlying(Input::Device::MouseInput::Y));
            self->curX   = (int)mx;
            self->curY   = (int)my;
            self->anchorX = (int)mx;
            self->anchorY = (int)my;
            self->dragging = 1;
        }
    }

    // 4. While dragging, push current bounds into the captureBox's corners (+0xd0..+0xdc,
    //    the box read unguarded)
    if (self->dragging) {
        CaptureBoxElement* box = self->captureBox;
        box->_x0 = self->curX;
        box->_y0 = self->curY;
        box->_x1 = self->anchorX;
        box->_y1 = self->anchorY;
    }

    // 5. Handle pending External save
    Object::External* pending = self->pendingExternal;
    if (pending) {
        if (self->saveFrameCount == 1) {
            // a. End sprite capture mode on the dialog's UserInterface
            self->_dialog->_ui->EndSpriteCapture();

            // b. The source directory (the FileSystem singleton read unguarded)
            const IO::FileSystem* fs = ::IO::FileSystem::_singleton;
            System::String sourceDirStr = fs->GetSourceDirectory().AsString();

            // c. The edit element's text (+0x30, read unguarded)
            UI::EditElement* editElem = self->editCapture;

            // d. Concat: sourceDir + editText -> concatStr1
            //    Then: concatStr1 + ".rsc" literal -> concatStr2
            //    Filename ctor from concatStr2 -> pathFilename
            System::String concatStr1 = sourceDirStr + editElem->GetText();

            // ".rsc" — retail's literal at 0x14015d424 in the exe .rdata
            const char* rscLiteral = ".rsc";
            System::String concatStr2 = concatStr1 + rscLiteral;

            System::Filename pathFilename(concatStr2);

            // e. Build subdir Filename from "resource" literal at 0x14015d430
            System::Filename filenameRsc("resource");

            // f. Save: External::SaveText(this, pathFilename, subdirFilename)
            pending->SaveText(pathFilename, filenameRsc);

            // g. Delete the External through its own slot 0 (flag 1 = free)
            delete pending;

            // h. Clear pendingExternal
            self->pendingExternal = nullptr;

            // i. Hide captureBox element: SetFlagsOnOff(elem, hideFlags=0, showFlags=1)
            if (self->captureBox)
                self->captureBox->SetFlagsOnOff(0, UI::kElementFlagHidden);

            // j. filenameRsc/pathFilename release via their destructors (System::Filename).
        } else {
            // Not yet at frame 1: increment counter
            self->saveFrameCount++;
        }
    }
}

// ============================================================

// ============================================================
// SpriteCaptureUI::OnElementEvent (vtable +0x48, 0x140079620)
// ============================================================
// [9] 0x140079620: `a == 0x12 && b == 1000` (the capture button's click; OnCreated set
// its id to 1000) tail-jumps to StartCapture 0x1400792b0. Nothing else is handled.
void SpriteCaptureUI::OnElementEvent(Object::InternalPtr<UI::Element>* /*element*/,
                                     UI::EventId a, int32_t b, int32_t /*c*/,
                                     int32_t /*d*/, int32_t /*e*/, void* /*userData*/) {
    if (a == UI::EventId::Activated && b == 1000)
        StartCapture();   // 0x1400792b0
}

// 0x1400792b0, in order: normalise the box so (curX, curY) is the top-left corner;
// MemAlloc(0x58, 8) and UI::SpriteCapture's ctor (import 0x140152c10; the exe's own
// vtable copy 0x14015d550 is the class's) on a non-null block; pendingExternal = it,
// saveFrameCount = 0; the inclusive viewport {curX, curY, w + 1, h + 1} to
// UserInterface::BeginSpriteCapture (import 0x140151d78, R8 = &viewport) on the
// dialog's UI (+0x18 -> +0x10); then captureBox->SetFlagsOnOff(1, 0) (import
// 0x1401514d8, tail jump). OnUpdate saves the capture one frame later.
void SpriteCaptureUI::StartCapture() {
    if (anchorX < curX) { const int32_t t = curX; curX = anchorX; anchorX = t; }
    if (anchorY < curY) { const int32_t t = curY; curY = anchorY; anchorY = t; }

    UI::SpriteCapture* capture = nullptr;
    if (void* mem = System::MemoryAllocator::MemAlloc(0x58, 8))
        capture = ::new (mem) UI::SpriteCapture();
    pendingExternal = capture;
    saveFrameCount = 0;

    const Video::Viewport viewport{curX, curY, anchorX - curX + 1, anchorY - curY + 1};
    _dialog->_ui->BeginSpriteCapture(capture, &viewport);
    captureBox->SetFlagsOnOff(UI::kElementFlagHidden, 0);
}
