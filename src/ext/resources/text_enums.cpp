// ============================================================================
// src/ext/resources/text_enums.cpp -- the identifiers our .rsc text may use (text_enums.h)
// ============================================================================
// NOT RETAIL. The .rsc identifiers are the kit's; their values come from the engine's own readers
// (Element::GetAlignedPosition's row-major 3x3, element.cpp), from the retail accessors
// (Dialog::IsModal 0x180038800 reads bit 1, PausesGame 0x180038810 bit 0, IsFirstDraw 0x180038840
// bit 4), from the kit's own name tables in Runtime-x64-profile.dll, or from the compiled output
// (LayoutHorizontal LeftToLeft = 1 / LeftToRight = 2, LayoutVertical TopToTop = 1, LabelPosition
// TextBottom = 3).
// ============================================================================
#include "ext/resources/text_enums.h"

namespace Ext {
namespace RscText {

namespace {

struct EnumValue {
    Enum        en;
    const char* name;
    uint32_t    value;
};

const EnumValue kEnumValues[] = {
    // Element::GetAlignedPosition (element.cpp): row-major 3x3, 0 = top-left .. 8 = bottom-right.
    { kAlignment, "TopLeft", 0 },    { kAlignment, "TopCenter", 1 },    { kAlignment, "TopRight", 2 },
    { kAlignment, "MidLeft", 3 },    { kAlignment, "MidCenter", 4 },    { kAlignment, "MidRight", 5 },
    { kAlignment, "BottomLeft", 6 }, { kAlignment, "BottomCenter", 7 }, { kAlignment, "BottomRight", 8 },
    // Dialog::PausesGame / IsModal / IsFirstDraw (retail 0x180038810 / 0x180038800 / 0x180038840).
    { kDialogFlags, "PauseGame", 1 }, { kDialogFlags, "Modal", 2 }, { kDialogFlags, "FirstDraw", 0x10 },
    // ElementDescription::_flags, OR'd into Element::_flags by ElementDescription::CreateInstance: the kit's
    // {value, name} table in Runtime-x64-profile.dll (0x180254580). Each bit is the one Element.h names:
    // ExpandWidth / ExpandHeight are the stretch bits the aligned position and the ribbon / table layouts
    // test (kElementFlagAlignArg1 / 2; retail's StatusBar.rsc:groupCommon, Crop.rsc:crop), FullHeight /
    // FullWidth the screen-size bits of GetPaddedSize.
    { kElementFlags, "Hidden", 0x1 },      { kElementFlags, "NoMouseCapture", 0x2 },
    { kElementFlags, "ExpandWidth", 0x4 }, { kElementFlags, "ExpandHeight", 0x8 },
    { kElementFlags, "FullHeight", 0x10 }, { kElementFlags, "FullWidth", 0x20 },
    // The compiled output of our own files (EnhancedOptions.rsc:labelEnhanced, Extensions.rsc:entryRow).
    { kLabelPosition, "TextBottom", 3 },
    { kLayoutH, "LeftToLeft", 1 }, { kLayoutH, "LeftToRight", 2 },
    { kLayoutV, "TopToTop", 1 },
};

}  // namespace

bool LookupEnum(Enum en, const std::string& name, uint32_t& value) {
    for (const EnumValue& e : kEnumValues) {
        if (e.en == en && name == e.name) {
            value = e.value;
            return true;
        }
    }
    return false;
}

}  // namespace RscText
}  // namespace Ext
