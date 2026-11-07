#pragma once
// DebugUI C bodies (defined in debugui.cpp). Split out of ui_panels.h (B.4 R0-EXE).

#include "Dialog/DebugUI.h"
#include <cstdint>

// B4 FORWARD: 1 outside caller(s) (src/diag/disaster_harness.cpp) -- the body is
// DebugUI::OnElementEvent (0x140060bc0).
void DebugUI_Slot9(DebugUI* self, Object::InternalPtr<UI::Element>* element, UI::EventId param3, int param4);  // B9: harness / port-only -- read: case 329 calls 0x14005fd20 (the tornado spawn) without loading RCX (0x140060ea5; only case 327 does MOV RCX,RBX), and no DebugUI exists on the disaster route: retail makes one only through its type factory 0x140010ec0 (RegisterType_DebugUI 0x14014be9e) when a dialog resource names it, and no tree route opens that dialog; the harness keeps the literal null
