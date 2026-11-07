// src/ext/ui/row_tint.h -- Ext::RowTint: the row and box tints the ext toolbar pages share
//
// NOT RETAIL. ARGB colours handed to a row border's SetColor. The idle row is the Mods list's
// (ModDialog::kRowTintIdle, retail); these are the tints the ext pages add on top of it.
#pragma once

#include <cstdint>

namespace Ext {
namespace RowTint {

inline constexpr uint32_t kSelected   = 0x30a0c8ffu;   // the selected row / person (blue)
inline constexpr uint32_t kSearchFill = 0x38ffffffu;   // a search box's fill

}  // namespace RowTint
}  // namespace Ext
