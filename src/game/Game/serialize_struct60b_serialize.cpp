// src/game/Game/serialize_struct60b_serialize.cpp
//
// M3.5 Path B session 4 — per-element 0x60B serialize + typed-ExternalPtr
// array serialize (cascade points #10 and #13 in Geometry::Serialize).
//
// Helpers (verified addresses via Ghidra MCP):
//   TradeWork_impl_ffa0       — exe 0x14001ffa0  (per-element 0x60B serialize)
//   (0x140020150, the typed-ExternalPtr array, is Object::SerializeExternalArray
//    <Graphics::MaterialInstance> at its caller since B8 RT2)
//   TradeWork_impl_sub_f9c0   — exe 0x14001f9c0  (uint16-stride sub-array;
//                                                  inline anonymous helper —
//                                                  only one caller (ffa0))
//
// Cascade safety: when any required Runtime primitive (FindOrLoad,
// IsDerivedFrom, Filename ctors, Serializer slots, etc.) is null at runtime
// we log a one-shot warning and return early instead of crashing. The
// pattern matches src/game/Game/serialize_externals.cpp:31-50 and
// serialize_arrays.cpp.
//
// TypeInfo for FUN_140020150 was verified via disassembly:
//   140020340: MOV RAX, qword ptr [0x140151350]   ; BaseTypeInfo* slot
//   140020347: MOV EDX, dword ptr [RAX]           ; *(uint*)BaseTypeInfo
//   14002034c: CALL [0x140152a98]                 ; IsDerivedFrom
// Slot 0x140151350 == g_ppTypeInfo_MaterialInstance (Graphics::MaterialInstance).
// This matches FUN_140007140's slot — confirmed shared-type use.
//
// NOTE: This file is the PUBLIC-INTERFACE-ONLY reimpl. The dispatch logic
// (delegate to original-via-fn-ptr if resolved, else execute the cleaned C
// path inline) follows the same pattern as serialize_struct60b.cpp /
// serialize_externals.cpp. The cleaned-C path mirrors the original's body
// shape (no instruction-for-instruction guarantee — the dispatch slots for
// the per-primitive serialize calls aren't all wired yet, so we soft-fail).

#include "object/TypeInfo.h"
#include "graphics/Camera.h"
#include "graphics/Material.h"   // ENDGAME B.3.9 TI-REST: Graphics::MaterialInstance::TypeId()
#include "system/Filename.h"
#include "system/String.h"
#include "Object/factory_registration.h"
#include "object/External.h"   // ENDGAME B.3.3 step 6A
#include "io/Serializer.h"     // ENDGAME B.3.4 caller batch B
#include "Game/exe_bodies.h"
#include "Game/WorldGeometryElements.h"
#include <cstdint>
#include <cstdio>
#include <cstring>

// Reimpl_ExternalPtr_Serialize_Typed_7140 (serialize_externals_typed.cpp, 0x140007140)
// serializes each element's typed-ExternalPtr at +0x00: Game/exe_bodies.h.

namespace {

inline bool IsLoading(IO::Serializer& serializer) {
    return static_cast<int>(serializer.GetDirection()) == 0;   // the stream's direction (+0x08)
}

// 0x14001f9c0 — a batch's vertex array: the count (on load: resize through
// 0x14001ee40, stride 0x14), then per vertex Serialize(4 shorts), (4 shorts),
// (4 bytes).
// B.4 H5-P2 (behaviour:): the load resize is retail's 0x14001ee40 (0x14001f9f8),
// Array<GeometryBatchVertex>::SetSize: MemAlloc(.., 2), the new tail left as found
// (every slot is then read from the stream). The tree called 0x14001bf10's row
// (align 4, zero-filled tail).
void SerializeBatchVertices(Collection::Array<GeometryBatchVertex>& vertices, IO::Serializer& serializer) {
    int32_t local_count = vertices._size;
    serializer.Serialize(local_count, nullptr);
    if (IsLoading(serializer)) {
        vertices.SetSize(local_count);   // 0x14001ee40
    }
    if (local_count <= 0) return;
    GeometryBatchVertex* v = vertices._data;
    if (!v) return;
    for (int i = 0; i < local_count; ++i) {
        serializer.Serialize(4, v[i].position, nullptr);
        serializer.Serialize(4, v[i].uv, nullptr);
        serializer.Serialize(4, v[i].normal, nullptr);
    }
}

} // namespace

// ============================================================
// 0x14001ffa0 — serialize `count` batches in place.
// ============================================================
void GeometryBatch_Serialize(IO::Serializer& serializer, int count, GeometryBatch* batches) {
    if (count <= 0 || !batches) return;

    for (int i = 0; i < count; ++i) {
        GeometryBatch& b = batches[i];

        // 1. Typed ExternalPtr (Graphics::MaterialInstance) at +0x00 (0x140007140).
        ::Object::SerializeExternal<Graphics::MaterialInstance>(serializer, b.material);

        // 2. slot and the culling box (+0x28..+0x38).
        serializer.Serialize(b.slot, nullptr);
        serializer.Serialize(b.minX, nullptr);
        serializer.Serialize(b.minY, nullptr);
        serializer.Serialize(b.maxX, nullptr);
        serializer.Serialize(b.maxY, nullptr);
        // 3. 1 byte at +0x3c.
        serializer.Serialize(b.field_0x3C, nullptr);
        // 4. 1 int at +0x40.
        serializer.Serialize(b.field_0x40, nullptr);
        // 5. 1 bool at +0x44.
        serializer.Serialize(b.field_0x44, nullptr);
        // 6. 5 floats at +0x48..+0x58.
        for (int k = 0; k < 5; ++k)
            serializer.Serialize(b.field_0x48[k], nullptr);
        // 7. 1 bool at +0x5c: retail's bool slot (+0x78). The binary reader / writer
        // move the one byte through its address and never load it as a bool; the
        // field is a raw byte (review F-008).
        serializer.Serialize(reinterpret_cast<bool&>(b.field_0x5C), nullptr);

        // 8. Version-gated bool at +0x5d (versions >= 2).
        int version = serializer.GetVersion();
        if (version >= 2) {
            serializer.Serialize(b.alternateKey, nullptr);
        }

        // 9. Version-gated sub-arrays (versions >= 1).
        version = serializer.GetVersion();
        if (version >= 1) {
            SerializeBatchVertices(b.vertices, serializer);   // 0x14001f9c0

            // The index array: count, on load resize (0x14001c890), then the bulk uint16s.
            int32_t local_count = b.indices._size;
            serializer.Serialize(local_count, nullptr);
            if (IsLoading(serializer)) {
                b.indices.SetSize(local_count);   // 0x14001c890
            }
            if (b.indices._data) {
                serializer.Serialize(local_count, b.indices._data, nullptr);
            }
        }
    }
}
