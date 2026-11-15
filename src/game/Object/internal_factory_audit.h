#pragma once
// src/game/Object/internal_factory_audit.h
//
// Diagnostics for the Object::InternalFactory handle table — the engine-wide
// registry every Object::Internal enters at construction and leaves at
// destruction.
//
// Mechanism (Runtime-x64.dll, Ghidra-verified):
//   InternalFactory::_singleton   @ 0x1801fc6d8 (ptr-to-factory global)
//   factory+0x00  = table base (Internal**)
//   factory+0x08  = count (int)
//   factory+0x0c  = capacity (int)
//   factory+0x20  = System::Random (generation source)
//
//   Object::Internal::Internal (0x1800bfd10) — REGISTER:
//       obj+0x08 = factory.count;  obj+0x0c = Rand();  table[count++] = obj
//   Object::Internal::~Internal (0x1800bfdb0) — UNREGISTER (swap-remove):
//       obj+0x0c = 0;  last = count-1
//       if (obj+0x08 != last) { table[obj+0x08] = table[last];
//                               table[obj+0x08]->+0x08 = obj+0x08; }
//       count = last
//
// The table invariant is therefore: for every i in [0,count), table[i]+0x08 == i.
//
// Two ways a standalone build breaks it, both of which corrupt the heap:
//   (A) register WITHOUT unregister — a freed object stays in the table. The next
//       ~Internal that swap-removes it writes `*(int*)(freedPtr+0x08) = idx`, a
//       4-byte store into memory the allocator has since handed to someone else.
//   (B) unregister WITHOUT register — obj+0x08 is uninitialised heap garbage, so
//       the swap-remove writes 8 bytes at table_base + garbage*8 (wild write).
//
// NB: Object::InternalBasePtr::CheckPtr (0x1800c0290) is an EMPTY function in the
// shipping Runtime. It does NOT validate or null anything, so a stale InternalBasePtr
// never self-invalidates in retail either — the game clears refs explicitly at removal.
// The table exists for SERIALIZATION (InternalBasePtr::Serialize writes index+generation
// and resolves them back via InternalFactory::PersistObject), not for weak-ref safety.

#include <cstdint>
#include <cstddef>

namespace Object { class InternalFactory; class Internal; }

namespace ObjAudit {

// The live InternalFactory (nullptr before the Runtime engine is up).
::Object::InternalFactory* Factory();

// Current registered-object count, or -1 if the factory is not reachable.
int Count();

// True when `obj`'s self-index (+0x08) is in range AND table[idx] == obj — i.e.
// the object is genuinely present in the handle table.
bool IsRegistered(const ::Object::Internal* obj);

}  // namespace ObjAudit
