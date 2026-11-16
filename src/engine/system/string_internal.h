#pragma once
// ============================================================================
// src/engine/system/string_internal.h — the System::String primitives filename.cpp
// shares with string.cpp.
// ============================================================================
// RtMemAlloc / RtMemFree, the allocator pair every String buffer comes from,
// and RtString_StrCompare, a one-line shim over System::String::StrCompare
// (src/engine/system/String.h). The definitions are in string.cpp (RtMemFree
// is header-inline in String.h outside the unit test).
// ============================================================================

#include "system/String.h"

namespace RtSystem {

// System::MemoryAllocator's MemAlloc / MemFree, called by name (malloc / free
// in the host unit test). Every String buffer comes from and goes back to
// this one heap.
void* RtMemAlloc(uint64_t size, int align);
void  RtMemFree(void* p);

// ?StrCompare@String@System@@SAHPEBD0@Z  retail 0x1800c98a0
int RtString_StrCompare(const char* a, const char* b);

}  // namespace RtSystem
