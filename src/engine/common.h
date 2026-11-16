#pragma once
// Forward declarations for Runtime-x64.dll types.
// Included by src/engine/engine/Engine.h.
// Binary layouts defined in individual headers under src/engine/<namespace>/.

#include <cstdint>
#include <cstddef>

// ============================================================
// Video namespace
// ============================================================

struct ImageBuffer;          // Video::ImageBuffer (0x68 bytes) — see video/ImageBuffer.h
struct ImageBufferData;      // Video::ImageBufferData — platform-specific (DX11/DX9)
struct VideoDevice;          // Video::Device
struct RenderBuffer;         // Video::RenderBuffer
struct VideoContext;          // Video::Context (already in global_types.h)

// ============================================================
// Graphics namespace
// ============================================================

struct MaterialInstance;      // Graphics::MaterialInstance
namespace UI { class SpriteSheet; }   // UI::SpriteSheet (was mislabelled Graphics::)
using SpriteSheet = UI::SpriteSheet;  // ENDGAME B.3.6 block D: RULE B6-4.2 alias

// ============================================================
// Object namespace
// ============================================================

struct External;             // Object::External
struct CompiledExternal;     // Object::CompiledExternal (0x20 bytes base)
struct ExternalFactory;      // Object::ExternalFactory

// ============================================================
// System namespace
// ============================================================

namespace System { class MemoryAllocator; }  // src/engine/system/MemoryAllocator.h
struct Filename;             // System::Filename
