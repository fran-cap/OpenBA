// src/game/Work/tendarea_ctor.cpp
// TendArea "from entity" constructor — retail TendArea_CopyCtor (0x1400f5a80).
//
// WHY A SEPARATE FILE: tendarea.cpp hosts the TendArea vtable and every slot body, but
// not this constructor — the same split plantorchard_ctor.cpp exists for. It is a
// leaf in OrchardComponent state 10's closure: both the state-10 enter
// (0x140050e30) and its per-frame seasonal re-post (0x140050c00) build the tending work
// item with `TendArea_CopyCtor(MemAlloc(0x60, 8), &orchard->+0x10, 1)`.
#include "Work/exe_bodies.h"
//
// ABI, read from the disassembly at 0x1400f5a80 (not inferred):
//   TendArea* __fastcall TendArea_CtorFromEntity(void* mem /*RCX*/,
//                                                void** entityIBP /*RDX*/,
//                                                int kind /*R8D*/)
//   MOV RAX,[RDX] ; MOV [RSP+0x48],RAX ; LEA RDX,[RSP+0x48] ; CALL 0x1400f72e0
//   LEA RAX,[0x140161990] ; MOV [RDI],RAX        ; vtable
//   XOR ECX,ECX ; MOV [RDI+0x48],RCX ; MOV [RDI+0x50],ECX ; MOV [RDI+0x58],EBX
//
// Retail copies the incoming IBP to a local FIRST and hands Work_Ctor25 the address of
// that local, because Work_Ctor25 takes an InternalBasePtr* rather than a raw pointer.

#include "Work/TendArea.h"
#include <new>
#include "object/TypeInfo.h"
#include <cstdint>
#include <cstdio>

// ENDGAME B.3.8: the file-level lazy Resolve() is gone (B38 D7) — Work_Ctor25 is the
// base ctor Work(ComponentEntity* const*) and the stamp is the compiler's.

// Retail copies the incoming IBP to a local and hands Work_Ctor25 its address; the base
// ctor Work(ComponentEntity* const*) reads the pointer once, so passing the caller's IBP
// is the same read.
// The +0x48 set's root / count zeroes are the `_citizens` member's ctor (retail 0x1400f5ac6 /
// 0x1400f5ac9, recorded as the EH member state).
TendArea::TendArea(ComponentEntity* const* entityIBP, int kind) : Work(entityIBP) {
    tendMode = kind;
}

