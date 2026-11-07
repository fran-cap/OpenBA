// src/game/Goal/buildinggoal.cpp
// Clean reimplementation of BuildingGoal vtable methods.
//
// BuildingGoal (0x48 bytes) — achievement goal requiring building count.
// Vtable: 0x140160300
//
// Fields (from Ghidra decompilation):
//   +0x30: bool   requireAll (_field30)  — true = all buildings required, false = any
//   +0x38: void*  buildingTypeArray      — array of 0x18-byte entries
//   +0x40: int    buildingTypeCount      — number of entries
//
// Each entry in the array is 0x18 bytes:
//   +0x00: System::String (8 bytes) — building type name (an ExternalPtr triple)
//   +0x08: uint32 — cached ExternalFactory generation (-1 = never resolved)
//   +0x10: External* — RESOLVED ComponentDescription pointer, and the key the
//                    world-building BST is searched on. Filled lazily by
//                    ResolveExternalByName_Impl (0x14001b1d0); it is NOT a hash.
//
// The world building BST is at game+0xF0+0x160.
// BST nodes: +0x00=key (uint64), +0x10=count (int), +0x20=left, +0x28=right
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Goal/BuildingGoal.h"
#include "Goal/goal_exe_helpers.h"
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "Object/factory_registration.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include "Component/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Entity/CitizenBehavior.h"
#include "Component/ComponentDescription.h"
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "system/String.h"
#include "system/Filename.h"
#include <new>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch D
#include "achievement/Goal.h"   // ENDGAME B.3.7: QUALIFIED base calls

// ============================================================
// Construction / destruction (ENDGAME B.3.8 block GOAL)
// ============================================================
// ctor 0x1400d77a0: Goal() (the base's real ctor), the vptr (the compiler's),
// then requireAll = 1 and the entry array header zeroed.
BuildingGoal::BuildingGoal() : _field30(true), _pad31{}, _field38() {}

// The dtor inside [0] 0x140025e80 (body 0x1400d77a0): destroy the entry array at
// +0x38 (0x14001b030, the member's ~Array after this empty body); the compiler
// then runs ~Goal (retail's CALL).
BuildingGoal::~BuildingGoal() {}

// ============================================================
// BuildingGoal::Serialize (vtable +0x28, 0x1400d76a0)
// ============================================================
// Calls Goal::Serialize, then serializes:
//   1. requireAll (bool at +0x30)
//   2. the entry count (int, a local copy)
//   3. on load (encoding mode 0): resize the array (0x14001b340)
//   4. each entry: Filename temporary, String::Serialize, resolve if loading
bool BuildingGoal::Serialize(IO::Stream& stream) {
    // Achievement::Goal::Serialize (slot 5, 0x18009ee20) — QUALIFIED: an
    // unqualified call would dispatch back into this slot.
    Achievement::Goal::Serialize(stream);

    // The serializer (stack-local).
    IO::Serializer serializer(stream, 0);

    // requireAll (bool at +0x30)
    serializer.Serialize(_field30, nullptr);

    // the entry count (a local copy)
    int countLocal = _field38._size;
    serializer.Serialize(countLocal, nullptr);

    // Check if loading (encoding mode == 0 means loading)
    int encodingMode = static_cast<int>(serializer.GetDirection());   // IO::Serializer::GetDirection, retail 0x180006210
    if (encodingMode == 0) {
        // Loading: resize the entry array (0x14001b340, the row's SetSize).
        _field38.SetSize(countLocal);
    }

    // Serialize each entry
    NamedExternalRef* entry = _field38._data;
    if (countLocal > 0) {
        for (uint32_t i = 0; i < static_cast<uint32_t>(countLocal); i++) {
            // Filename temporary retail's compiler emitted for this loop body.
            System::Filename filenameBuf;
            (void)filenameBuf;

            // String::Serialize — the entry's name (+0x00)
            entry->name.Serialize(serializer);

            // On load: resolve the entry's description reference (0x14001b1d0,
            // ResolveExternalByName_Impl). Writes +0x10 (resolved) and +0x08
            // (factory generation).
            if (encodingMode == 0) {
                entry->ResolveDescription();
            }

            entry++;
        }
    }
    return true;
}

// ============================================================
// BuildingGoal::Reconstruct [6] 0x140025e30 / Destruct [7] 0x140001640
// ============================================================
// retail 0x140025e30: vtable[0](this, 0) (`~T()` (slot 0, flags 0): destroy in place),
// then the ctor on `this`.
Object::External* BuildingGoal::Reconstruct() {
    this->~BuildingGoal();
    return ::new (static_cast<void*>(this)) BuildingGoal();
}
void BuildingGoal::Destruct() { this->~BuildingGoal(); }

// ============================================================
// BuildingGoal::CheckGoalState (vtable +0x40, 0x1400d7580)
// ============================================================
// Iterates the entries, resolves each name, looks the resolved description up
// in the world's building BST (game+0xF0+0x160), counts matches.
//
// Returns:
//   requireAll mode (+0x30 != 0): kMet if ALL types matched, kNotMet otherwise
//   requireAny mode (+0x30 == 0): kMet if NO types matched, kFailed if SOME matched
Achievement::Goal::GoalState BuildingGoal::CheckGoalState() const {
    CitizenBehavior* worldData = GameUpdateLoop::_singleton->m_citizenList;

    int matchCount = 0;
    int entryCount = _field38._size;
    int entryIdx = 0;

    if (entryCount > 0) {
        do {
            const NamedExternalRef& entry = _field38._data[entryIdx];

            // Retail copies the WHOLE 0x18 reference to the stack, resolves into
            // the COPY, and reads the pointer back out of it — the array entry
            // is deliberately left untouched, so each evaluation re-resolves.
            //
            // The BST key is not a hash: it is the resolved ComponentDescription
            // pointer.
            const Object::External* lookupKey;
            {
                NamedExternalRef refCopy(entry);   // the stack copy: name, generation, cached ptr
                refCopy.ResolveDescription();
                lookupKey = refCopy.resolved;
            }                                      // ~String, before the walk

            // BST lookup in the +0x160 template tree
            TemplateBucket* node = worldData->field_0x160._root;
            while (node != nullptr) {
                const Object::External* nodeKey = node->element.key;
                if (lookupKey < nodeKey) {
                    node = node->left;    // +0x20
                } else if (lookupKey > nodeKey) {
                    node = node->right;   // +0x28
                } else {
                    // Found matching node — check its entity count (+0x10)
                    if (node != nullptr && node->element.entities._size > 0) {
                        matchCount++;
                    }
                    break;
                }
            }

            entryIdx++;
        } while (entryIdx < entryCount);
    }

    // Evaluate result based on requireAll flag
    if (_field30) {
        // requireAll mode
        if (matchCount == entryCount) {
            return kMet;  // all matched — goal complete
        }
        return kNotMet;
    }

    // requireAny mode
    if (matchCount == 0) {
        return kMet;      // none matched
    }
    if (matchCount > 0) {
        return kFailed;   // some matched
    }
    return kNotMet;
}

// ============================================================
// BuildingGoal::Construct (vtable +0x50, 0x140025de0)
// ============================================================
// Placement-new on `this` (retail folded it with createInPlace).
Object::External* BuildingGoal::Construct() {
    return ::new (static_cast<void*>(this)) BuildingGoal();
}

// create 0x140025d80: MemAlloc(0x48, 8), then the (inlined) ctor.
::Object::Base* BuildingGoal::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(BuildingGoal), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(::new (p) BuildingGoal());
}
// createInPlace 0x140025de0.
::Object::Base* BuildingGoal::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(::new (storage) BuildingGoal());
}

DEFINE_EXE_TYPE(BuildingGoal, "Achievement::Goal")

// ============================================================
// BuildingGoal_Probe — diagnostic, env-gated (BANISHED_GOAL_PROBE)
// ============================================================
// The base game ships NO scenarios: `Dialog\Goal.rsc` is the only goal resource
// in data0/data1.pkg, and BuildingGoal only ever gets built by the tutorial or by
// a mod. So nothing in a sandbox autostart run evaluates a BuildingGoal, and any
// claim that this path "works" from a normal run would be a null test
// (reference_null_test_measures_a_world_without_the_feature).
//
// This probe builds a real one-entry BuildingGoal on the stack against a live
// world, runs the actual Slot8 evaluator, and reports every stage: the world
// building BST contents, the resolved description pointer, and the verdict. Set
// BANISHED_GOAL_PROBE to a template name (default "Template\StorageBarn.rsc" —
// the starting town builds one).
extern "C" void BuildingGoal_Probe(const char* templateName) {
    if (GameUpdateLoop::_singleton == nullptr) return;
    CitizenBehavior* worldData = GameUpdateLoop::_singleton->m_citizenList;
    if (!worldData) return;

    // --- Dump the world building BST (keyed on resolved description pointer) ---
    // Iterative in-order walk with an explicit stack; the tree is small (one node
    // per distinct built building type).
    TemplateBucket* stack[64];
    int sp = 0, dumped = 0;
    TemplateBucket* node = worldData->field_0x160._root;
    fprintf(stderr, "[GoalProbe] world building BST @ %p:\n",
            static_cast<void*>(&worldData->field_0x160._root));
    while ((node || sp) && dumped < 64) {
        while (node && sp < 64) { stack[sp++] = node; node = node->left; }
        if (!sp) break;
        node = stack[--sp];
        fprintf(stderr, "[GoalProbe]   key=%p count=%d\n",
                static_cast<const void*>(node->element.key), node->element.entities._size);
        dumped++;
        node = node->right;
    }
    if (dumped == 0) fprintf(stderr, "[GoalProbe]   (empty)\n");

    // --- Build a one-entry requireAll BuildingGoal and evaluate it ---
    // B4 Y GAMEA: a real BuildingGoal on the stack (was a zeroed vptr-less byte
    // image); its ~BuildingGoal frees the entry array the probe freed by hand.
    // Evaluated through a QUALIFIED (non-dispatching) member call, as before.
    BuildingGoal goalObject;
    BuildingGoal* goal = &goalObject;
    goal->_field30 = true;                             // requireAll
    goal->_field38.SetSize(1);                         // seeds generation -1
    NamedExternalRef* entry = goal->_field38._data;
    if (!entry) { fprintf(stderr, "[GoalProbe] resize FAILED (null array)\n"); fflush(stderr); return; }

    fprintf(stderr, "[GoalProbe] seeded entry gen=%d ptr=%p (gen must be -1, else the "
                    "resolver would treat it as cached)\n",
            entry->generation, static_cast<const void*>(entry->resolved));

    new (&entry->name) System::String(templateName);   // +0x00 name

    // Resolve a copy for reporting — Slot8 deliberately resolves into a throwaway
    // reference and leaves the array entry alone, so read the pointer from our own copy.
    const Object::External* resolved;
    {
        NamedExternalRef refCopy(*entry);
        refCopy.ResolveDescription();
        resolved = refCopy.resolved;
    }

    goal->_field38._size = 1;                          // count = 1
    int verdict = static_cast<int>(goal->BuildingGoal::CheckGoalState());

    fprintf(stderr, "[GoalProbe] name=\"%s\" resolved=%p verdict=%d "
                    "(requireAll: 1=met, 0=not met)\n",
            templateName, static_cast<const void*>(resolved), verdict);
    if (!resolved) {
        fprintf(stderr, "[GoalProbe]   resolved=NULL — ResolveExternalByName uses find-ONLY "
                        "Find, so the template must already be in the ExternalFactory "
                        "(see StartTown_ResolveTemplate's FindOrLoad fallback).\n");
    }
    fflush(stderr);
    // ~BuildingGoal: the entry's ~String, then MemFree of the array (0x14001b030).
}
