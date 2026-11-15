// src/game/Component/uicomponent_serialize.cpp
// Slot-3 (+0x18) stream Serialize body for UIComponent
// (retail 0x14012dd00 — Ghidra names it UIComponent_Shutdown; per disassembly
// it first CALLs Component_Serialize, confirming this is really Serialize).
//
// Retail body (verified by decompile + disassembly of 0x14012dd00):
//   Component_Serialize(self, stream)
//   Serializer ser(stream, /*version*/0)
//   count = *(int*)(self + 0x30)
//   Serialize(ser, &count, null)                    // int, r/w in place
//   if (loading) FUN_14000a590(self + 0x28, count)   // resize byte array
//   Serialize(ser, count, *(uint8_t**)(self+0x28), null)   // raw byte array
//   ~Serializer
//
// FUN_14000a590 (0x14000a590) is a growable-array resize helper operating on
// the triple {ptr(8)@+0x00, count(4)@+0x08, cap(4)@+0x0c} with 1-byte
// elements (MemAlloc(newCap, /*align*/1)) — i.e. self+0x28 is a genuine
// growable uint8_t array whose header spans self+0x28..0x37 (ptr@0x28,
// count@0x30, cap@0x34). This fills every byte of UIComponent that the ctor
// (Component/UIComponent.h) leaves zeroed — the `data` member, a Collection::Array<uint8_t>,
// is that header, not two independent scratch pointers — so this class is NOT empty:
// it round-trips a real per-instance byte buffer through save/load.
//
// The byte-array Serialize overload is a distinct dispatch slot from the
// scalar int/bool/float Serialize used elsewhere (0x140152400, called as
// Serialize(ser, count, ptr, name) — RCX=ser, EDX=count, R8=ptr, R9=name).
//
// Slot 3 is UIComponent::Persist below, a plain virtual override that no flag gates.
#include "Component/UIComponent.h"
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A
#include <cstdint>

// ============================================================
// UIComponent::Persist (vtable slot [3], 0x14012dd00)
// ============================================================
// B.3.8 CB1b: the body moved here from the free UIComponent_Serialize.
void UIComponent::Persist(IO::Stream& stream) {
    Component::Persist(stream);   // retail: CALL 0x1401189b0 (qualified)

    IO::Serializer ser(stream, 0);

    int count = data._size;   // +0x30
    ser.Serialize(count, nullptr);
    // FUN_14000a590 is Collection::Array<uint8_t>::SetSize(n): no init arm, and the
    // byte read below writes every one of the `count` bytes.
    // True when the serializer is READING (retail: *(int*)(stream + 8) == 0).
    if (static_cast<int>(ser.GetDirection()) == 0)   // IO::Serializer::GetDirection, retail 0x180006210
        data.SetSize(count);

    ser.Serialize(count, data._data, nullptr);
}

