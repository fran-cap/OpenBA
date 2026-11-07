#pragma once
// ============================================================================
// src/engine/ui/EventId.h — UI::EventId, the id of a UI event record
// ============================================================================
//
// UI::UserInterface::Event::eventId (+0x10): what UpdateEvents (0x18011fc60)
// hands a dialog as the second argument of OnElementEvent (DialogInstance slot
// 8, +0x40 / DialogController slot 9, +0x48). Element::Update emits 0-7 and
// 0xb / 0xc from the UpdateState button bytes (the mouse slots Left / Right /
// Middle) and the tooltip 0xf / 0x10; DialogInstance::Update emits 8-0xa, 0x11
// and 0x13; UserInterface the drag-drop ends; the widgets 0x12 and 0x14-0x17.
// Retail passes it as an int (`HHHHHPEAX` in the slot-8 mangling); no retail
// string names the ids, so the names come from the code that raises each one.
// ============================================================================

#include <cstdint>

namespace UI {

enum class EventId : int32_t {
    HoverEnter        = 0x00,   // mouse entered the element (Element::Update)
    HoverLeave        = 0x01,   // mouse left the element
    LeftPress         = 0x02,   // left button pressed on the element (mouse slot Left)
    RightPress        = 0x03,   // right button pressed on the element
    MiddlePress       = 0x04,   // middle button pressed on the element
    LeftRelease       = 0x05,   // left button released over the element
    RightRelease      = 0x06,   // right button released over the element
    MiddleRelease     = 0x07,   // middle button released over the element
    DialogLeftPress   = 0x08,   // left press anywhere in the dialog (DialogInstance::Update)
    DialogRightPress  = 0x09,   // right press in the dialog
    DialogMiddlePress = 0x0A,   // middle press in the dialog
    Drag              = 0x0B,   // left held and the mouse moved over the element
    Drop              = 0x0C,   // a drag-drop released on the element
    DragDropEnd       = 0x0D,   // drag-drop finished (to the source dialog)
    DragDropCancel    = 0x0E,   // drag-drop cancelled (to the source dialog)
    ToolTipBegin      = 0x0F,   // the element's tooltip opened
    ToolTipMove       = 0x10,   // the open tooltip followed the mouse
    HoverEnd          = 0x11,   // the dialog's hover / tooltip ended (EndHover)
    Activated         = 0x12,   // button or link clicked
    Timer             = 0x13,   // a DialogInstance timer expired; the data is the timer id
    SelectionChanged  = 0x14,   // list row / combo item chosen; the data is the index
    ValueChanged      = 0x15,   // edit committed, spinner or number edit changed
    RadioSelected     = 0x16,   // a radio button of a group chosen
    SliderChanged     = 0x17,   // slider value changed
};

}  // namespace UI
