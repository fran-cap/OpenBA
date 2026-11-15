#pragma once
// ============================================================================
// src/engine/system/widestring_internal.h — String.h and WideString.h for its includers
// ============================================================================
// ENDGAME B.3.4b SYSTEM (4a): the LAYOUT and the class are in
// `src/engine/system/WideString.h`, which is where a type header belongs and
// where `tests/unit/system_abi_test.cpp` can reach it. This file declares
// nothing of its own; its includers get System::String and System::WideString
// through it.
// ============================================================================

#include <cstddef>
#include "system/String.h"
#include "system/WideString.h"

namespace RtSystem {
// (B.4 Z MIRROR2: the Set / AsUTF8 doors are gone; callers hold a real
// System::WideString and call its members.)


}  // namespace RtSystem
