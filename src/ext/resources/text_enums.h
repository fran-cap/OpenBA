#pragma once
// ============================================================================
// src/ext/resources/text_enums.h -- the identifiers our .rsc text may use for an enum field
// ============================================================================
// NOT RETAIL. The value table behind text_reader.cpp's enum slots (`Alignment _alignment = MidLeft;`,
// `Flags _flags = ExpandWidth;`), kept apart from the reader so the unit tests check it without the
// live factory. Where each value comes from is noted at its row in text_enums.cpp; an identifier
// without a known value is an error, never a guess.
// ============================================================================

#include <cstdint>
#include <string>

namespace Ext {
namespace RscText {

enum Enum { kNoEnum, kAlignment, kLabelPosition, kLayoutH, kLayoutV, kDialogFlags, kElementFlags };

// The value of `name` in the identifier set `en`; false when the set does not know it.
bool LookupEnum(Enum en, const std::string& name, uint32_t& value);

}  // namespace RscText
}  // namespace Ext
