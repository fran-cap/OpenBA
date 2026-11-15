// src/game/Work/work_base_bst.cpp
// Shared Work "simple BST" (AVL) family — citizen-AI work-tracking tree.
//
// These are the exe's inlined Collection::Set instances for an 8-byte key (an
// Object pointer compared as an unsigned 64-bit value): EntityList's update trees,
// the Work leaves' citizen sets (WorkCitizenSetNode), the scheduler's order tree.
#include "Work/exe_bodies.h"
#include "Work/KeyBst.h"   // WorkBST_Retrace (0x14001a8c0), the remove templates
//
// All functions are faithful 1:1 ports of the decompiled exe helpers (Ghidra-verified).
//
// Node (Collection::Set<uint64_t>::Node, 0x28 bytes):
//   +0x00  uint64  element      (the key == the pointer an InternalPtr holds)
//   +0x08  ptr     parent
//   +0x10  ptr     left child
//   +0x18  ptr     right child
//   +0x20  int16   balance      (= rightHeight - leftHeight)
//   +0x22  int16   height       (leaf = 0; node = max(childHeight)+1 encoded as child.height+1)
// Tree: the Set's header, _root (+0x00) and _count (+0x08).
//

#include "object/TypeInfo.h"
#include "system/MemoryAllocator.h"
#include "io/Serializer.h"     // ENDGAME B.3.4 caller batch C
#include "object/Internal.h"   // ENDGAME B.3.4a closing: InternalBasePtr::Serialize
#include "collection/Collection.h"
#include "Component/CitizenComponent.h"   // ENDGAME B.3.9 D5: CitizenComponent::TypeId()
#include <cstdint>

// 0x140021b40 (the leaf allocation), 0x140021a30 (the insert) and 0x1400296b0 (the set
// serializer) are templates over the node type in Work/KeyBst.h (B9), beside the remove
// 0x14001a6e0, the find-remove 0x140025520 and the retrace 0x14001a8c0 (B8 D5): each caller
// instantiates them on its own set, so this file keeps no body.
