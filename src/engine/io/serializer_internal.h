#pragma once
// ============================================================================
// src/engine/io/serializer_internal.h — the ported IO::Serializer surface (Wave 4, A)
// ============================================================================
//
// Includes src/engine/io/Serializer.h, which declares `IO::Serializer`; the
// member bodies are in serializer_class.cpp. See serializer.cpp for the retail
// addresses and the pinned ReaderWriter vtable offsets.
//
//
// ============================================================================

#include <cstdint>

#include "io/Serializer.h"

// B.4 Z LAST IO: the 19 `extern "C"` RtSerializer_* forwarders that were
// declared here (the exe import-slot family over the `rt::io::Serializer`
// POD) had no caller left anywhere in the tree and are deleted with their
// bodies; every caller calls `IO::Serializer`'s members.
