// src/game/Game/citizenlist_serialize.cpp
//
// CitizenBehavior::Serialize (exe 0x14008f760, Ghidra: TradeComponent_impl_f760)
// and its post-load spatial-map rebuild pass CitizenBehavior::PostLoad (exe
// 0x14008f580, Ghidra: TradeComponent_impl_f580). CitizenBehavior lives at GameUpdateLoop+0xF0
// (m_citizenList), 0x1C0 bytes — see src/game/Entity/citizenbehavior.cpp for
// the rest of the class (Update/CountResources/BuildPathGraph/...).
//
// ============================================================
// CitizenBehaviorRaw field layout used by Serialize (byte offsets; Ghidra's
// decompile indexes `longlong *param_1` in qwords — every offset below is
// that index * 8). Each "IBP array" is a 0x10-byte header {void* data@+0,
// int32 count@+8, int32 cap@+0xC} — i.e. Collection::Array<T>;
// elements are 8-byte Object::InternalBasePtr slots.
//
//   +0x00  IBP[]  type=ComponentEntity                 (count field +0x08)
//   +0x10  IBP[]  type=ComponentEntity                 (count field +0x18)
//   +0x20  IBP[]  type=PopulationComponent (0x1401c4790)(count field +0x28)   [NOTE: order verified against disasm, see below]
//   +0x30  IBP[]  type=ComponentEntity                 (count field +0x38)
//   +0x40  IBP[]  type=CemeteryComponent  (0x1401c4090) (count field +0x48)
//   +0x50  IBP[]  type=HappinessComponent (0x1401c42c0) (count field +0x58)
//   +0x60  IBP[]  type=AttendanceComponent(0x1401c3f40) (count field +0x68)
//   +0x70  IBP[]  type=HealerComponent    (0x1401c4330) (count field +0x78)
//   +0x80  IBP[]  type=LivestockComponent (0x1401c7970) (count field +0x88)
//   +0x90  IBP[]  type=PastureComponent   (0x1401c7ba0) (count field +0x98)
//   +0xa0  IBP[]  type=CitizenComponent   (0x1401c7158) (count field +0xa8)
//          -> unconditional: FUN_140017a40(this+0x100, ser)  TREE #1 (string-table key)
//   +0xb0  IBP[]  type=HerdComponent      (0x1401c7820) (count field +0xb8)
//   +0xc0  IBP[]  type=PopulationComponent(0x1401c4790) (count field +0xc8)
//   +0xd0  IBP[]  type=BuildComponent     (0x1401c7008) (count field +0xd8)
//   +0xe0  IBP[]  type=WellComponent      (0x1401c4c28) (count field +0xe8)
//   +0xf0  IBP[]  type=DiseaseComponent   (0x1401c4170) (count field +0xf8)  [version>0 only]
//   +0x100 tree#1 {root@0,count@8}    node=0x38B {key,valHdr[16],parent,left,right,balance}
//   +0x110 ExternalPtr<ComponentDescription>[]          (count field +0x118)
//   +0x120 ExternalPtr<ComponentDescription>[]          (count field +0x128)
//   +0x130 int32-pair[] (stride 8)                      (count field +0x138)
//   +0x140 int-assoc[] (stride 0x10 outer)               (count field +0x148) [version>4 only]
//   +0x150 int-assoc[] (stride 0x10 outer, "stats")      (count field +0x158)
//   +0x160 tree#2 {root@0,count@8}  (BuildComponentRef key) [version>=2 live path]
//   +0x170 ProfessionResourceEntry[21] (stride 0x20)     (count field +0x178) [version>=4 live path]
//   +0x180 SummaryEntry[2] (stride 0x10)                 (count field +0x188)
//   +0x190 Time  (System::Time::Serialize)
//   +0x198 Time  (System::Time::Serialize)
//   +0x1a0 Time  (System::Time::Serialize)
//   +0x1a8 float (raw Serialize)
//   +0x1ac int32 (raw Serialize)   [version>1 only]
//   +0x1b0 int32 (raw Serialize)   [version>2 only]
//
// The exact array->offset mapping above was read directly off the decompile's
// `param_1 + N` arithmetic (N in qword units); the semantic type-id ->
// component-name mapping comes from cross-referencing the .rdata type-id
// address against this project's existing RegisterType/factory registrations
// (see per-field comments below).
//
// Dependencies (all verified against the live tree; nothing here is stubbed):
//   Collection::Array<T>::SetSize   — the exe's own monomorphisations
//                                     (0x1400032e0/0x1400021d0/0x140001bb0): each
//                                     member array's own SetSize.
//   fn_InternalBasePtr_Serialize    — engine_funcs.h (0x140152aa0 dispatch slot)
//   fn_InternalBasePtr_CheckPtr     — engine_funcs.h
//   fn_IsDerivedFrom                — engine_funcs.h
//   fn_ExternalPtr_BuildSerialize   — engine_funcs.h, resolves to registered 0x140002340
//                                     (used as TREE #2's key resolver: Ghidra's
//                                     "Object_ExternalPtr_BuildComponentRef_Serialize")
//   FUN_140018020 (TREE #1's key resolver, "string-table" ExternalPtr variant) —
//                                     registered at 0x140018020 (Reimpl_ExternalPtr_
//                                     Serialize_StringTable); resolved locally here.
//   fn_Serializer_SerializeBulk     — engine_funcs.h, used for the int-assoc arrays'
//                                     inner int[] bulk serialize.
//   Avl38Retrace (0x140014b40) — the shared AVL retrace template (Player/exe_bodies.h).
//   Reimpl_ExternalPtrArray_Serialize (0x1400049c0) — already ported+registered in
//                                     src/game/Game/serialize_externals_array.cpp.
//   TradeDescription_impl_e360 (0x14000e360) — DEAD-PATH ONLY (version<2 legacy tree,
//                                     see below); already ported+registered in
//                                     src/game/Component/citizenai_housing_idle.cpp.
//
// Newly ported in THIS file (none registered elsewhere):
//   FUN_140017640 ("StatsArray::Init", stride-0x10 outer array resize + inner
//                  deep-copy on grow) — also resolves engine_funcs.h's
//                  fn_StatsArray_Init (used by CitizenBehavior_Ctor).
//   FUN_1400177c0 ("ProfResArray::Init", stride-0x20 array resize)          — also
//                  resolves fn_ProfResArray_Init.
//   FUN_14000f9e0 (inner stride-4 array deep-copy helper for the above)     — file-local.
//   FUN_140017f60 (stride-8 int-pair array resize)                          — file-local.
//   Type-id storage cells for HealerComponent (0x1401c4330) and WellComponent
//   (0x1401c4c28) — nobody registered these two .rdata cells yet; this file adds
//   them (same pattern as reg_component_animal.cpp) so RegisterType fills them in.
//
// DEAD-CODE PATHS (ported best-effort for completeness; UNREACHABLE at our fixed
// save version 6 — see `IO::Serializer::Serializer(local_1e8,param_2,6)` at the
// top of the original function, which pins every write to version 6, and no
// real save this project produces or is expected to load will carry version<2 or
// version<4 short of a pre-release Banished save file):
//   - version<2 legacy key/int BST (TradeComponent_helper_ebe0, 0x14000ebe0)
//   - version<4 legacy Time-array -> ProfessionResourceEntry conversion block
// Both are implemented but not soak-tested; if a genuinely ancient save ever
// exercises them, verify against Ghidra before trusting the result.

#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "Game/GameUpdateLoop.h"   // caller batch CB6: the world's named fields
#include "Game/Map.h"   // m_game's class (B.4 GAME-a)
#include "Entity/CitizenBehavior.h"         // B4 GAME-a: CitizenBehavior::Serialize / PostLoad
#include <cstdio>
#include "collection/Collection.h"
#include "Component/LivestockComponent.h"
#include "Component/HerdComponent.h"
#include "Component/DiseaseComponent.h"
#include "Component/PopulationComponent.h"
#include "Component/BuildComponent.h"
#include "Component/MapComponent.h"
#include "Component/HealerComponent.h"       // ENDGAME B.3.9 CB: 0x1401c4330 cell class
#include "Component/WellComponent.h"         // ENDGAME B.3.9 CB: 0x1401c4c28 cell class
#include "Component/HappinessComponent.h"    // ENDGAME B.3.9 CB: 0x1401c42c0 cell class
#include "Component/CitizenComponent.h"      // ENDGAME B.3.9 CB: 0x1401c7158 cell class
#include "Component/PastureComponent.h"      // ENDGAME B.3.9 CB: 0x1401c7ba0 cell class
#include "Component/CemeteryComponent.h"     // ENDGAME B.3.9 CB: 0x1401c4090 cell class
#include "Component/AttendanceComponent.h"   // ENDGAME B.3.9 CB: 0x1401c3f40 cell class
#include "Entity/ComponentEntity.h"          // ENDGAME B.3.9 CB: 0x1401c5d70 cell class
#include <cstdint>
#include <cstring>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "object/TypeInfo.h"   // caller batch CB6: Object::Base::Type()
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch B
#include "system/Value.h"      // ENDGAME B.3.4b System caller batch (Random/Time/Color)
#include "Game/exe_bodies.h"
#include "Player/exe_bodies.h"              // Avl38Retrace (0x140014b40)
#include "Component/exe_bodies.h"           // CitizenAI_Reg_helper_e360, StorageComponent_DynArrayResize
#include "Game/LegacyKeyIntNode.h"         // the version < 2 key -> int tree node
#include "Work/exe_bodies.h"               // WorkBase_TreeNodeFree_e320 (B6 row 810: retail frees the legacy tree's subtrees)
#include "Game/Profession.h"   // B8 RT2: ::Object::SerializeExternal<Profession> (the downcast needs it complete)
#include "Component/ComponentDescription.h"   // B8 RT2: ::Object::SerializeExternal<ComponentDescription> (the downcast needs it complete)
#include "ext/registry.h"                           // EXT SEAM: Ext::Fix().unresolved_seeds
#include "ext/unresolved_seeds/unresolved_seeds.h"  // EXT SEAM: Ext::UnresolvedSeeds::DropNulls

// Already-ported sibling functions, called by name through their declaring headers:
//   0x140014b40 Avl38Retrace (Player/exe_bodies.h), 0x14000e360
//   CitizenAI_Reg_helper_e360 (Component/exe_bodies.h), 0x140018020 / 0x140002340
//   Reimpl_ExternalPtr_Serialize_StringTable and 0x1400049c0
//   Reimpl_ExternalPtrArray_Serialize (Game/exe_bodies.h), 0x14000f090
//   StorageComponent_DynArrayResize (Component/exe_bodies.h), 0x140088a80
//   WellComp_AddCellEntry_88a80 (Component/wellcomponent_funcs.h).

namespace {

// ------------------------------------------------------------
// Serializer mode helper
// ------------------------------------------------------------
inline bool SerializerLoading(IO::Serializer* serializer) {
    return static_cast<int>(serializer->GetDirection()) == 0;   // IO::Serializer::GetDirection, retail 0x180006210
}

// The tree serializer's key resolver (TREE #1: 0x140018020, TREE #2: 0x140002340 --
// one body, Reimpl_ExternalPtr_Serialize_StringTable).
// B4 X7-GAME-A: the key resolver is typed on the bucket key (a Profession* / ComponentDescription* slot).

// ------------------------------------------------------------
// Registry census — the TownManager's 16 IBP registries in one line.
//
// Every town service reads one of these arrays (residences +0x20, cemeteries
// +0x40, attendance +0x60, ...) and every one of them is filled ONLY by a
// state-2 "enter" registrar, which a load never re-fires (the state machine is
// restored already in state 2). So on the load leg these counts ARE the town:
// if they come out zero, citizens have no homes, no jobs and no services, which
// looks like "everyone froze, then starved".
void RegistryCensus(const CitizenBehavior* cb, const char* leg, const char* when) {
    const struct { int32_t count; const char* name; } kReg[] = {
        {cb->field_0x00._size, "entA"},   {cb->field_0x10._size, "entB"},
        {cb->field_0x20._size, "residence"}, {cb->field_0x30._size, "entC"},
        {cb->cemeteries._size, "cemetery"},
        {cb->happinessComponents._size, "happiness"},
        {cb->attendanceComponents._size, "attendance"},
        {cb->healers._size, "healer"},
        {cb->livestock._size, "livestock"}, {cb->pastures._size, "pasture"},
        {cb->citizens._size, "citizen"}, {cb->herds._size, "herd"},
        {cb->populations._size, "population"},
        {cb->buildComponents._size, "build"},
        {cb->wells._size, "well"}, {cb->diseases._size, "disease"},
    };
    std::fprintf(stderr, "[TownCensus] %s %s:", leg, when);
    for (const auto& r : kReg)
        std::fprintf(stderr, " %s=%d", r.name, r.count);
    std::fprintf(stderr, "\n");
    std::fflush(stderr);
}

// ------------------------------------------------------------
// IBP array serialize — the recurring 15-times pattern (0x140008150 and its siblings):
//   count = list.count; Serialize(count); if(loading) list.SetSize(count);
//   for each slot: InternalBasePtr::Serialize(slot); if (!IsDerivedFrom(typeId)) slot=null;
// `list` is a Collection::Array<InternalPtr<T>> (CitizenIbpList, kSpec_PtrZero).
// ------------------------------------------------------------
// ENDGAME B.3.9 CB: takes the type id by VALUE (the class static of the cell retail
// reads) and validates unconditionally, as retail does (0x14008f760: every array's
// `if (p && !IsDerivedFrom(p, [cell])) p = 0`). The old pointer form skipped the check
// whenever the cell read 0 -- which it always did for Healer (0x1401c4330), Well
// (0x1401c4c28) and Happiness (0x1401c42c0), so those three arrays were never validated.
template <class T>
void SerializeIBPArray(CitizenIbpList<T>& list, IO::Serializer* serializer, uint32_t typeId) {
    int32_t count = list._size;
    serializer->Serialize(count, nullptr);
    bool loading = SerializerLoading(serializer);
    // retail 0x1400032e0 IS Collection::Array<T>::SetSize(n): stride 8 align 8,
    // new tail zeroed (kSpec_PtrZero). Element is the 8-byte InternalPtr walked below.
    if (loading) list.SetSize(count);

    Object::InternalPtr<T>* arr = list._data;
    for (int32_t i = 0; i < count; ++i) {
        Object::InternalPtr<T>* elem = &arr[i];
        elem->Object::InternalBasePtr::Serialize(*serializer);   // 0x140152aa0 on the element (0x14008f7f8)
        T* val = elem->Get();
        if (val != nullptr) {
            if (!::Object::IsDerivedFromNullSafe(val, typeId)) {
                *elem = nullptr;
            }
        }
    }
}

// (B4 X5-COMP: the three element rows of +0x140 / +0x150, +0x170 and +0x130 —
// 0x140017640 with its inner deep copy 0x14000f9e0, 0x1400177c0, 0x140017f60 — are
// CitizenBehavior's members' rows now: Entity/citizenbehavior.cpp,
// CitizenBehaviorRows in Entity/CitizenBehavior.h.)
}  // namespace

// (FUN_140017640 "StatsArray::Init", the stride-0x10 outer resize, is CitizenIntAssocArray::SetSize;
// its last free-function spelling, ResizeStride0x10, went with the tree-only version-5 clamp: B6 row 15.)

namespace {

// ------------------------------------------------------------
// int-assoc array (stride-0x10 outer, resized via ResizeStride0x10; each
// slot is itself a stride-4 int32[] array resized via Array<I32NoInit>::SetSize,
// bulk-serialized via fn_Serializer_SerializeBulk). Used at +0x140 (v>4)
// and +0x150 ("stats").
// ------------------------------------------------------------
// IO::Serializer::Serialize(int count, int* data, name) — exe dispatch slot
// 0x140151278. Distinct from fn_Serializer_SerializeBulk (slot 0x140152400),
// which is the BYTE-width overload.
void SerializeIntArray(IO::Serializer* serializer, int32_t count, int32_t* data) {
    serializer->Serialize(count, data, nullptr);
}

// `outer` is CitizenBehavior +0x140 / +0x150 (an Array<Array<int32>>, 0x140017640).
void SerializeIntAssocArray(CitizenIntAssocArray& outer, IO::Serializer* serializer) {
    int32_t count = outer._size;
    serializer->Serialize(count, nullptr);
    bool loading = SerializerLoading(serializer);
    if (loading) outer.SetSize(count);

    Collection::Array<int32_t>* slot = outer._data;
    for (int32_t i = 0; i < count; ++i) {
        int32_t innerCount = slot->_size;
        serializer->Serialize(innerCount, nullptr);
        // retail 0x1400021d0 IS Collection::Array<int32_t>::SetSize(n): stride 4 align 4,
        // grown tail left as the allocation found it (no init arm: int32_t's trait row).
        // The bulk int serializer below overwrites all `innerCount` elements either way.
        if (loading) slot->SetSize(innerCount);
        int32_t* innerPtr = slot->_data;
        // Retail (0x1400900e7 for +0x150, 0x1400903b8 for +0x140) dispatches
        // through slot 0x140151278 = IO::Serializer::Serialize(int count,
        // int* data, name) — the INT-array overload, 4 bytes per element. The
        // elements really are int32 (the inner array is resized stride-4), and
        // 0x140152400 (fn_Serializer_SerializeBulk) is the BYTE overload, which
        // read a quarter of the bytes and desynced everything after it. Retail
        // never calls 0x140152400 anywhere in this function.
        SerializeIntArray(serializer, innerCount, innerPtr);
        ++slot;
    }
}

// ------------------------------------------------------------
// TREE #1 / TREE #2 — component-ref-keyed BST mapping key -> IBP[] array.
// Node (0x38 bytes): {uint64 key@0, valHdr[16]@8 (ptr,count,cap), parent@0x18,
// left@0x20, right@0x28, int16 balance@0x30, int16 height@0x32}. Rebalance via Avl38Retrace (0x140014b40).
// ------------------------------------------------------------
// The nodes are CitizenBehavior's bucket-tree nodes (CitizenBucketNode<Key>,
// Entity/CitizenBehavior.h: 0x38 bytes, {key, entity Array} element, parent
// +0x18, left +0x20, right +0x28, int16 balance / height +0x30 / +0x32).
// The node's entity Array is zeroed (+0x08 = +0x10 = 0) and then DEEP-COPIED from the
// loaded local by 0x140015ec0 (Array::Assign: free the destination, allocate exactly
// the source count, copy the qwords), as retail's three node arms do
// (0x140017b03 / 0x140017b9a / 0x140017beb in 0x140017a40; the same in 0x140017cd0).
template <class Key>
CitizenBucketNode<Key>* AllocCompRefTreeNode(Key* key, CitizenBucketNode<Key>* parent,
                                             const CitizenIbpList<ComponentEntity>& val) {
    if (!System::MemoryAllocator::_singleton) return nullptr;
    CitizenBucketNode<Key>* n =
        static_cast<CitizenBucketNode<Key>*>(System::MemoryAllocator::MemAlloc(0x38, 8));
    if (!n) return nullptr;
    n->element.key = key;
    ::new (&n->element.entities) CitizenIbpList<ComponentEntity>();   // +0x08 = +0x10 = 0
    n->element.entities.Assign(val);                                 // 0x140015ec0
    n->parent = parent;              // +0x18
    n->left = nullptr;               // +0x20
    n->right = nullptr;              // +0x28
    n->balance = 0;                  // +0x30 / +0x32: retail's dword zero (H7: two stores)
    n->height = 0;
    return n;
}

template <class Node>
Node* CompRefTreeMin(Node* n) {
    if (!n) return nullptr;
    while (n->left) n = n->left;
    return n;
}
template <class Node>
Node* CompRefTreeSucc(Node* n) {
    if (!n) return nullptr;
    if (n->right) return CompRefTreeMin(n->right);
    Node* parent = n->parent;
    while (parent && parent->right == n) {
        n = parent;
        parent = parent->parent;
    }
    return parent;
}

// `tree` is CitizenBehavior +0x100 / +0x160 (a Collection::Set of buckets).
// `Cell` is the key's ExternalPtr<T> instantiation (B8 RT2): 0x140017a40 calls 0x140018020 (Profession)
// at 0x140017ab3 / 0x140017c5a, 0x140017cd0 calls 0x140002340 (ComponentDescription) at 0x140017d43 /
// 0x140017eea, directly and with no test (it was a function-pointer argument tested for null).
template <class Cell, class Key>
void SerializeCompRefTree(CitizenBucketSet<Key>& tree, IO::Serializer* serializer) {
    using Node = CitizenBucketNode<Key>;
    Node** rootPtr = &tree._root;
    int32_t* countField = &tree._count;
    int32_t count = *countField;
    serializer->Serialize(count, nullptr);
    bool loading = SerializerLoading(serializer);

    if (loading) {
        for (int32_t i = 0; i < count; ++i) {
            // Retail's per-iteration local {key, Array} (zeroed at 0x140017d30..0x140017d38,
            // before the key is read); the Array is freed at the bottom of EVERY iteration,
            // the duplicate-key one included (0x140017c19 in 0x140017a40, 0x140017ea5 in
            // 0x140017cd0): ~val.
            Key* key = nullptr;
            CitizenIbpList<ComponentEntity> val;
            ::Object::SerializeExternal<Cell>(*serializer, key);
            SerializeIBPArray(val, serializer, ComponentEntity::TypeId());

            if (*rootPtr == nullptr) {
                *rootPtr = AllocCompRefTreeNode(key, static_cast<Node*>(nullptr), val);
                *countField = *countField + 1;
                continue;
            }
            Node* cur = *rootPtr;
            Node* insertedNode = nullptr;
            bool dup = false;
            for (;;) {
                if (key < cur->element.key) {
                    Node* left = cur->left;
                    if (!left) {
                        insertedNode = AllocCompRefTreeNode(key, cur, val);
                        cur->left = insertedNode;
                        break;
                    }
                    cur = left;
                } else if (key <= cur->element.key) {
                    dup = true;   // matches original: already present, skip insert + count++
                    break;
                } else {
                    Node* right = cur->right;
                    if (!right) {
                        insertedNode = AllocCompRefTreeNode(key, cur, val);
                        cur->right = insertedNode;
                        break;
                    }
                    cur = right;
                }
            }
            if (!dup) {
                *countField = *countField + 1;
                Avl38Retrace(rootPtr, &insertedNode);   // 0x140014b40 (the shared AVL retrace)
            }
        }
    } else {
        Node* root = *rootPtr;
        Node* node = CompRefTreeMin(root);
        while (node) {
            ::Object::SerializeExternal<Cell>(*serializer, node->element.key);
            SerializeIBPArray(node->element.entities, serializer, ComponentEntity::TypeId());
            node = CompRefTreeSucc(node);
        }
    }
}

// ------------------------------------------------------------
// DEAD-PATH: version<2 legacy key/int BST (TradeComponent_helper_ebe0,
// 0x14000ebe0). Node (0x30 bytes): {key@0,int32 value@8,parent@0x10,
// left@0x18,right@0x20,balance@0x28}. Rebalance via TradeDescription_impl_e360.
// Never reached at our fixed save version (6); ported for completeness only.
// ------------------------------------------------------------
// The node: Game/LegacyKeyIntNode.h (rebalanced by the shared 0x30 walk 0x14000e360).

LegacyKeyIntNode* AllocLegacyTreeNode(ComponentDescription* key, LegacyKeyIntNode* parent, int32_t value) {
    if (!System::MemoryAllocator::_singleton) return nullptr;
    LegacyKeyIntNode* n = static_cast<LegacyKeyIntNode*>(System::MemoryAllocator::MemAlloc(0x30, 8));
    if (!n) return nullptr;
    n->key = key;
    n->value = value;          // +0x08 / +0x0C: the value zero-extended (H7: two stores)
    n->valueHigh = 0;
    n->parent = parent;
    n->left = nullptr; n->right = nullptr;
    n->balance = 0;            // +0x28 / +0x2A: one dword store (H7: two stores)
    n->height = 0;
    return n;
}
LegacyKeyIntNode* LegacyTreeMin(LegacyKeyIntNode* n) {
    if (!n) return nullptr;
    while (n->left) n = n->left;
    return n;
}
LegacyKeyIntNode* LegacyTreeSucc(LegacyKeyIntNode* n) {
    if (!n) return nullptr;
    if (n->right) return LegacyTreeMin(n->right);
    LegacyKeyIntNode* parent = n->parent;
    while (parent && parent->right == n) {
        n = parent;
        parent = parent->parent;
    }
    return parent;
}
void SerializeLegacyKeyIntTree(LegacyKeyIntNode** rootPtr, int32_t* countField, IO::Serializer* serializer) {
    // version < 2 only (dead at save version 6). Retail 0x140090194..0x1400901ea reads this tree
    // into a STACK {root, count} record and frees it (B6 row 810: the tree used to hand +0x160's root
    // here, mixing 0x30-byte legacy nodes under a Set of 0x38-byte bucket nodes).
    int32_t count = *countField;
    serializer->Serialize(count, nullptr);
    bool loading = SerializerLoading(serializer);
    if (loading) {
        for (int32_t i = 0; i < count; ++i) {
            ComponentDescription* key = nullptr;
            ::Object::SerializeExternal<ComponentDescription>(*serializer, key);
            int32_t value = 0;
            serializer->Serialize(value, nullptr);

            if (*rootPtr == nullptr) {
                *rootPtr = AllocLegacyTreeNode(key, nullptr, value);
                *countField = *countField + 1;
                continue;
            }
            LegacyKeyIntNode* cur = *rootPtr;
            LegacyKeyIntNode* insertedNode = nullptr;
            bool dup = false;
            for (;;) {
                if (key < cur->key) {
                    LegacyKeyIntNode* left = cur->left;
                    if (!left) { insertedNode = AllocLegacyTreeNode(key, cur, value); cur->left = insertedNode; break; }
                    cur = left;
                } else if (key <= cur->key) {
                    dup = true; break;
                } else {
                    LegacyKeyIntNode* right = cur->right;
                    if (!right) { insertedNode = AllocLegacyTreeNode(key, cur, value); cur->right = insertedNode; break; }
                    cur = right;
                }
            }
            if (!dup) {
                *countField = *countField + 1;
                CitizenAI_Reg_helper_e360(rootPtr, &insertedNode);   // 0x14000e360
            }
        }
    } else {
        LegacyKeyIntNode* node = LegacyTreeMin(*rootPtr);
        while (node) {
            ::Object::SerializeExternal<ComponentDescription>(*serializer, node->key);
            serializer->Serialize(node->value, nullptr);
            node = LegacyTreeSucc(node);
        }
    }
}

} // namespace

// ============================================================
// CitizenBehavior::Serialize (0x14008f760)
// ============================================================
void CitizenBehavior::Serialize(IO::Stream& stream) {
    IO::Serializer serializer(stream, 6);
    IO::Serializer* ser = &serializer;

    const char* leg = SerializerLoading(ser) ? "LOAD" : "SAVE";
    RegistryCensus(this, leg, "enter");

    SerializeIBPArray(field_0x00, ser, ComponentEntity::TypeId());
    SerializeIBPArray(field_0x10, ser, ComponentEntity::TypeId());
    SerializeIBPArray(field_0x30, ser, ComponentEntity::TypeId());
    SerializeIBPArray(field_0x20, ser, ComponentEntity::TypeId());
    SerializeIBPArray(healers, ser, HealerComponent::TypeId());
    SerializeIBPArray(citizens, ser, CitizenComponent::TypeId());

    // Unconditional TREE #1: 0x140017a40, its keys ExternalPtr<Profession> (0x140018020; B7 LX2).
    SerializeCompRefTree<Profession>(field_0x100, ser);   // 0x140017a40 -> 0x140018020 (Profession)

    SerializeIBPArray(pastures, ser, PastureComponent::TypeId());
    SerializeIBPArray(cemeteries, ser, CemeteryComponent::TypeId());
    SerializeIBPArray(happinessComponents, ser, HappinessComponent::TypeId());
    SerializeIBPArray(attendanceComponents, ser, AttendanceComponent::TypeId());
    SerializeIBPArray(livestock, ser, LivestockComponent::TypeId());
    SerializeIBPArray(herds, ser, HerdComponent::TypeId());

    if (ser->GetVersion() > 0) {
        SerializeIBPArray(diseases, ser, DiseaseComponent::TypeId());
    }

    SerializeIBPArray(populations, ser, PopulationComponent::TypeId());
    SerializeIBPArray(buildComponents, ser, BuildComponent::TypeId());

    // ExternalPtr<ComponentDescription>[] arrays, resized via Array<Ptr8>::SetSize
    // (FUN_140001bb0) then bulk-serialized via Reimpl_ExternalPtrArray_Serialize
    // (0x1400049c0), matching FUN_140001bb0(hdr,count) + FUN_1400049c0(ser,count,ptr,p4).
    {
        int32_t count = field_0x110._size;
        ser->Serialize(count, nullptr);
        // B.3.2: Collection::Array<T>::SetSize — retail 0x140001bb0, stride 8 align 8,
        // new tail zeroed. Element is the 8-byte ExternalPtr<ComponentDescription>.
        if (SerializerLoading(ser))
            field_0x110.SetSize(count);
        // LEFT: the array body takes the element base as long long (Game/exe_bodies.h,
        // serialize_externals_array.cpp).
        ::Object::SerializeExternalArray<ComponentDescription>(*ser, count, field_0x110._data);   // 0x1400049c0
    }
    {
        int32_t count = field_0x120._size;
        ser->Serialize(count, nullptr);
        // B.3.2: Collection::Array<T>::SetSize — retail 0x140001bb0, stride 8 align 8,
        // new tail zeroed. Element is the 8-byte ExternalPtr<ComponentDescription>.
        if (SerializerLoading(ser))
            field_0x120.SetSize(count);
        ::Object::SerializeExternalArray<ComponentDescription>(*ser, count, field_0x120._data);   // 0x1400049c0
        // EXT SEAM (NOT RETAIL, docs/retail_bugs.md section 21): fix.unresolved_seeds. 0x1400049c0
        // stores null for an entry whose resource no longer loads (a mod's crop, tree or animal
        // with the mod now off); the crop field / orchard / pasture panels fault on it (0x140078830).
        if (SerializerLoading(ser) && Ext::Fix().unresolved_seeds) {
            const int32_t kept = Ext::UnresolvedSeeds::DropNulls(field_0x120._data, count);
            if (kept != count) {
                fprintf(stderr, "[UnresolvedSeeds] %d agriculture entries no longer resolve: dropped\n",
                        count - kept);
                field_0x120.SetSize(kept);
            }
        }
    }

    // LEFT: the three clocks are declared int64_t (CitizenBehavior.h, block WORK-a).
    field_0x190.Serialize(*ser);
    field_0x198.Serialize(*ser);

    // "Stats" int-assoc array at +0x150.
    SerializeIntAssocArray(field_0x150, ser);
    // Retail 0x14008fe50..0x14008fed0 (B6 row 14): on a LOAD the outer array is sized to 0x47
    // (through 0x140017640) when it is not, and every entry past the old count is sized to
    // entry 0's inner count (0x1400021d0) and zero-filled; both counts are re-read each turn.
    if (SerializerLoading(ser)) {
        const int32_t oldCount = field_0x150._size;
        if (oldCount != 0x47) field_0x150.SetSize(0x47);
        for (int32_t i = oldCount; i < field_0x150._size; ++i) {
            field_0x150._data[i].SetSize(field_0x150._data[0]._size);
            Collection::Array<int32_t>& inner = field_0x150._data[i];
            for (int32_t j = 0; j < inner._size; ++j) inner._data[j] = 0;
        }
    }

    // TREE #2 (BuildComponentRef key) on the live path (version>=2, always true
    // at version 6); legacy key/int BST otherwise (DEAD — see file header).
    if (ser->GetVersion() < 2) {
        // Retail 0x14008f760: a stack {root [RSP+0x30] = 0, count [RSP+0x38] = 0} is read by
        // 0x14000ebe0, then the root's left (+0x18) and right (+0x20) subtrees go through
        // 0x14000e320, the root through MemFree, and the record is zeroed; +0x160 is untouched.
        LegacyKeyIntNode* legacyRoot = nullptr;
        int32_t legacyCount = 0;
        SerializeLegacyKeyIntTree(&legacyRoot, &legacyCount, ser);
        if (LegacyKeyIntNode* r = legacyRoot) {
            WorkBase_TreeNodeFree(&legacyRoot, r->left);
            WorkBase_TreeNodeFree(&legacyRoot, r->right);
            System::MemoryAllocator::MemFree(r);
        }
        legacyRoot = nullptr;
        legacyCount = 0;
    } else {
        SerializeCompRefTree<ComponentDescription>(field_0x160, ser);   // 0x140017cd0 -> 0x140002340 (ComponentDescription)
    }
    // (B6 row 15: the tree used to clear the +0x160 tree header here at version 5; retail
    // 0x14008f760 has no version-5 step between the +0x160 tree and the wells -- its one
    // version-5 clear is +0x140's, below.)

    SerializeIBPArray(wells, ser, WellComponent::TypeId());

    // Stride-8 int-pair array at +0x130.
    {
        int32_t count = field_0x130._size;
        ser->Serialize(count, nullptr);
        bool loading = SerializerLoading(ser);
        if (loading) field_0x130.SetSize(count);
        WaterCellPair* pairs = field_0x130._data;
        for (int32_t i = 0; i < count; ++i) {
            ser->Serialize(pairs[i].cellA, nullptr);
            ser->Serialize(pairs[i].cellB, nullptr);
        }
    }

    if (ser->GetVersion() > 4) {
        SerializeIntAssocArray(field_0x140, ser);
    }
    if (ser->GetVersion() == 5) {
        field_0x140.SetSize(0);
    }

    if (ser->GetVersion() < 4) {
        // DEAD-PATH (version<4): legacy Time-array -> ProfessionResourceEntry /
        // SummaryEntry conversion. Never reached at our fixed version 6 — see file
        // header. Left unimplemented (no-op) rather than risk a wrong-stride write
        // into +0x170/+0x180 from an unverifiable Ghidra pointer-arithmetic reading;
        // a real pre-release save hitting this path needs a fresh Ghidra pass before
        // it can be trusted, so we intentionally stop short of guessing here.
    } else {
        int32_t profCount = professionResources._size;
        ser->Serialize(profCount, nullptr);
        bool loading = SerializerLoading(ser);
        if (loading) professionResources.SetSize(profCount);
        if (profCount > 0) {
            ProfessionResourceEntry* entry = professionResources._data;
            for (int32_t i = 0; i < profCount; ++i) {
                ser->Serialize(entry->currentCount, nullptr);
                ser->Serialize(entry->previousCount, nullptr);
                ser->Serialize(entry->maxCount, nullptr);
                ser->Serialize(entry->field_0x0C, nullptr);
                ser->Serialize(entry->_field10, nullptr);
                // Retail 0x14008ff9d: Time::Serialize(piVar7 + 4) with piVar7 = entry + 8, i.e.
                // the entry's Time at +0x18 (B6 row 13; the tree used to write the 8 bytes at +0x14).
                entry->timer.Serialize(*ser);
                ++entry;
            }
        }
        if (professionResources._size != 0x15) {
            professionResources.SetSize(0x15);
        }

        int32_t sumCount = notifyEntries._size;
        ser->Serialize(sumCount, nullptr);
        if (loading) {
            notifyEntries.SetSize(sumCount);   // FUN_14000f090
        }
        if (sumCount > 0) {
            ResourceNotifyEntry* entry = notifyEntries._data;
            for (int32_t i = 0; i < sumCount; ++i) {
                ser->Serialize(entry->current, nullptr);
                ser->Serialize(entry->maxCapacity, nullptr);
                entry->timer.Serialize(*ser);
                ++entry;
            }
        }
    }

    ser->Serialize(field_0x1A8, nullptr);
    field_0x1A0.Serialize(*ser);

    if (ser->GetVersion() > 1) {
        ser->Serialize(field_0x1AC, nullptr);
    }
    if (ser->GetVersion() > 2) {
        ser->Serialize(field_0x1B0, nullptr);
    }

    RegistryCensus(this, leg, "exit ");
}

// ============================================================
// CitizenBehavior::PostLoad (0x14008f580) — the post-load pass.
// Rebuilds a spatial grid-cell map (+0x140, stride-0x10 outer array
// sized (mapWidth/8)^2) from the residence array (+0x138/+0x130) and the
// well list (+0xe0/+0xe8), inserting each entity's grid cell into the map via
// FUN_140088a80 (already ported+registered as WellComp_AddCellEntry_88a80 —
// reused here as-is, same address/signature).
// ============================================================
// DWORD_1401c79e0 — the component type-id validated inside PostLoad's inner
// scan (the "position"-like component providing cell coords at +0x28/+0x2a).
// Table 1: 0x1401c79e0 is MapComponent's cell.
static uint32_t DWORD_1401c79e0_TypeId() {
    return MapComponent::TypeId();
}

void CitizenBehavior::PostLoad() {
    if (field_0x140._size >= 1) return;   // already populated

    Map* mapGrid = GameUpdateLoop::_singleton->m_game;
    int side = mapGrid->size >> 3;
    field_0x140.SetSize(side * side);
    std::fprintf(stderr, "[CitPostLoad] mapGrid=%p mapWidth=%d side=%d cells=%d "
            "cellMap=%p residence{ptr=%p count=%d} ent{ptr=%p count=%d}\n",
            static_cast<void*>(mapGrid), mapGrid->size, side, side * side,
            static_cast<void*>(field_0x140._data),
            static_cast<void*>(field_0x130._data),
            field_0x130._size,
            static_cast<void*>(wells._data),
            wells._size);
    std::fflush(stderr);

    int idx = 0;
    int32_t residenceCount = field_0x130._size;
    if (residenceCount > 0) {
        // +0x130 is the STRIDE-8 int-pair array (ResizeStride08Pair in Serialize
        // above); retail walks it with `lVar11 += 8` and reads only the low int
        // of each pair (0x14008f5c8). Indexing it as int32[] read every second
        // element from the pair's HIGH half, which is not a cell index at all —
        // the resulting x/y ran off the 32x32 cell map and AddCellEntry wrote
        // through a wild record pointer.
        WaterCellPair* residenceCells = field_0x130._data;
        int mapWidth = mapGrid->size;
        for (; idx < residenceCount; ++idx) {
            int cell = residenceCells[idx].cellA;
            if (cell < 0 || cell >= mapWidth * mapWidth) {
                std::fprintf(stderr, "[CitPostLoad] residence[%d] cell=%d pairHi=%d "
                        "x=%d y=%d cellIdx=%d (max %d)\n",
                        idx, cell, residenceCells[idx].cellB,
                        cell % mapWidth, cell / mapWidth,
                        (mapWidth >> 3) * ((cell / mapWidth) >> 3) + ((cell % mapWidth) >> 3),
                        side * side);
                std::fflush(stderr);
            }
            AddCellEntry(cell % mapWidth, cell / mapWidth, idx);   // 0x140088a80
        }
    }

    idx = 0;
    int32_t entCount = wells._size;
    if (entCount > 0) {
        Object::InternalPtr<WellComponent>* entArr = wells._data;
        int slot = -1;
        for (; idx < entCount; ++idx, --slot) {
            WellComponent* entIBP = entArr[idx].Get();
            ComponentEntity* comp = entIBP->_entity.Get();

            MapComponent* chosen = nullptr;
            int32_t compCount = comp->_components._size;
            for (int32_t j = 0; j < compCount; ++j) {
                Object::InternalPtr<Component>* slotPtr = &comp->_components[j];
                Component* cand = slotPtr->Get();
                if (!cand) continue;
                // Slot 2 on an entity component-array element (proved Object::Base).
                uint32_t tid = cand->Type();
                if (tid == DWORD_1401c79e0_TypeId()) {
                    slotPtr = &comp->_components[j];
                    Component* obj = slotPtr->Get();
                    if (obj && ::Object::IsDerivedFromNullSafe(obj, DWORD_1401c79e0_TypeId()))
                        chosen = static_cast<MapComponent*>(obj);   // validated just above
                    break;
                }
            }

            // Retail builds an InternalPtr<MapComponent> temporary from the find.
            ::Object::InternalPtr<MapComponent> chosenRef(chosen);
            MapComponent* chosenVal = chosenRef.Get();
            int16_t cx = chosenVal ? static_cast<int16_t>(chosenVal->tileX0) : 0;
            int16_t cy = chosenVal ? static_cast<int16_t>(chosenVal->tileY0) : 0;
            AddCellEntry(cx, cy, slot);   // 0x140088a80
        }
    }
}
