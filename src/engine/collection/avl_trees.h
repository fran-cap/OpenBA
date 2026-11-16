// ============================================================================
// src/engine/collection/avl_trees.h — Wave 9 lane COLL
//   The keyed `Collection::Set`/`Collection::Map` insert side of avl_trees.cpp,
//   declared for its callers.
// ============================================================================

#pragma once

#include <cstdint>

#include "io/FileSystem.h"    // RtIO::PackfileIndexPair / Node / Tree (group D)
#include "io/Persistence.h"    // RtIo::SettingsSet / SettingsNode (group B)

namespace RtObject { struct TypeNode; struct TypeRecord; }
namespace rtsetavl { struct Node; struct Set; }

namespace rtavl {

// ---------------------------------------------------------------------------
// EFFECTIVENESS COUNTERS — branches taken INSIDE the bodies under test.
//
// WAVE9.md's rule, learned three times over: "count branches taken INSIDE the
// function under test, not proxies observed from outside it." Every outside
// proxy this wave tried was either structurally pinned or swamped by the common
// path, and the control that was wrong twice said PASS both times.
//
// So these are incremented by the ported bodies themselves, at the exact
// branches whose absence would make a green run meaningless: a rotation that
// never ran, a pre-rotation the population never reached, a descent that only
// ever went one way, a comparator that only ever returned one sign.
//
// They are unconditional plain increments. The call volume is bounded (~400
// type registrations, ~20 settings keys, one insert per file per packfile) and
// a conditional would need a load anyway.
// ---------------------------------------------------------------------------
struct ProbeCounters {
    // the 0x50-node family
    uint64_t rot50L, rot50R, preRot50L, preRot50R, retrace50Iters;
    // descent shape, all three keyed trees
    uint64_t descLeft, descRight, equalHit, rootInsert;
    // comparator outcomes — a comparator that only ever returns one sign is
    // indistinguishable from a broken one
    uint64_t strcmpLt, strcmpEq, strcmpGt;
    uint64_t icmpLt,   icmpEq,   icmpGt;
    // the null arms of the two string descents, which are asymmetric and are
    // exactly what a "tidied" comparator would collapse
    uint64_t nullSearchKey, nullNodeKey;
    // Array<T*>::operator=
    uint64_t ptrAssignRealloc, ptrAssignTreble, ptrAssignSelf;
    // the recursive free
    uint64_t freeNodes, freeStringsReleased;
};
extern ProbeCounters g_pc;

// ---- group A: Set<Pair<uint32_t, TypeRecord>>, 0x50-byte node -------------
// The container argument is the Set's root slot (`&set._root`, at +0x00 of the
// Set, so the same pointer retail passes): every body only reads / writes `[RCX]`.
RtObject::TypeNode*  RotateLeft50 (RtObject::TypeNode** root, RtObject::TypeNode*);   // 0x1800307b0
RtObject::TypeNode*  RotateRight50(RtObject::TypeNode** root, RtObject::TypeNode*);   // 0x1800308e0
void                 Retrace50    (RtObject::TypeNode** root, RtObject::TypeNode**);  // 0x180030540
// 0x180030140 (TypeSetInsert) — deleted in ENDGAME B.3.3 step 7; Object::Factory
// is a Collection::Set. 0x1800087c0 / 0x180008440 / 0x180030380 / 0x180009350
// (pair ctor, node alloc, insert core, Array<T*>::operator=) — deleted in B.4
// R-COLL: the template's insert reaches this group only through Retrace50.

// ---------------------------------------------------------------------------
// B.4 (R-COLL) — the two keyed families of the 0x30-byte node, typed.
//
// rtsetavl::Node (set_avl.h) is the ERASED node the shared retrace / rotations
// / erase take (element opaque, folded by MSVC across every 16-byte element).
// The two insert families below know their element, so they build and walk
// THESE, and hand them to the erased core at the one retrace call. Both node
// layouts are rtsetavl::Node's, asserted against it in avl_trees.cpp.
// ---------------------------------------------------------------------------

// Group B's element: two System::String data pointers (0x18000fb00 copies
// +0x00 then +0x08, AddRef'ing each).
struct StringPair {
    char* key;     // +0x00
    char* value;   // +0x08
};

// The node 0x18000f970 allocates (`MOV EDX,0x30`) and 0x18000fb00 builds:
// element at +0x00 / +0x08, parent +0x10, the two null children +0x18 / +0x20,
// and ONE dword zero at +0x28 over balance and height. It IS the settings
// map's node: B7 LX, the `Collection::Set<Pair<String,String>>` instantiation's
// own `Node` (RtIo::SettingsNode, io/Persistence.h), its pair's key and value
// the two System::Strings; the map is that Set (RtIo::SettingsSet). The nodes
// are raw memory this family builds (0x18000fb00's stores); the Set's Clear
// destroys them.
using StringPairNode = RtIo::SettingsNode;

// Group D is the packfile index: its element, node and {root, count} header are
// declared ONCE, in src/engine/io/FileSystem.h (RtIO::PackfileIndexPair /
// PackfileIndexNode / PackfileIndexTree). Nodes are raw MemAlloc storage
// (0x180008520) built by placement copy (0x180008840) and released by the
// subtree free; the one stack pair (FilenameSetInsert 0x180025a80) is built by
// the copy ctor and released at its scope exit, as retail's is.
using FilenamePair     = RtIO::PackfileIndexPair;
using FilenamePairNode = RtIO::PackfileIndexNode;

// ---- group B: Map<String,String>, 0x30-byte node --------------------------
StringPairNode*  StringPairCtor  (StringPairNode* node, const StringPair* src,
                                  StringPairNode* parent);              // 0x18000fb00
StringPairNode*  StringNodeAlloc (void* ignored, const StringPair* pair,
                                  StringPairNode* parent);              // 0x18000f970
StringPairNode** StringInsertCore(RtIo::SettingsSet*, StringPairNode**,
                                  const StringPair* pair);              // 0x18002a170
StringPairNode** StringMapInsert (RtIo::SettingsSet*, StringPairNode**,
                                  char* const* key, char* const* value);  // 0x18002a090

// ---- group D: Set<Pair<Filename,uint64>>, 0x30-byte node ------------------
FilenamePairNode*  FilenamePairCtor  (FilenamePairNode* node, const FilenamePair* src,
                                      FilenamePairNode* parent);        // 0x180008840
FilenamePairNode*  FilenameNodeAlloc (void* ignored, const FilenamePair* pair,
                                      FilenamePairNode* parent);        // 0x180008520
FilenamePairNode** FilenameInsertCore(RtIO::PackfileIndexTree*, FilenamePairNode**,
                                      const FilenamePair* pair);        // 0x180025c70
FilenamePairNode** FilenameSetInsert (RtIO::PackfileIndexTree*, FilenamePairNode**,
                                      const System::Filename* key,
                                      const RtIO::PackfileIndexValue* value);  // 0x180025a80

// ---- the recursive subtree free ------------------------------------------
void SubtreeFree(void* ignoredThis, FilenamePairNode* node);             // 0x180008340

}  // namespace rtavl

