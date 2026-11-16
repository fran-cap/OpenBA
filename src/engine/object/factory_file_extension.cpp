// ============================================================================
// src/engine/object/factory_file_extension.cpp — Wave 24 lane INTERNAL8
// ============================================================================
//
// ?GetTypeIdForFileExtension@Factory@Object@@QEBAIPEBD@Z   retail 0x1800c04c0
//
// The EIGHTH of this lane's eight bodies, and the one that was never an
// `internal` row at all. Wave 23 lane INTERNAL found that three of the census's
// four `ordinal` gap rows are PHANTOMS — ordinals 443, 3214 and 4462 resolve to
// names that already carry a `ported=1` export row — and that the fourth,
// ordinal 2990, is a REAL row misfiled out of `Object`. This is that row.
//
//
// EXTENT: 0x1800c04c0..0x1800c0574, 181 bytes, two RET sites (0x1800c0567 for
// the not-found return and 0x1800c0574 for the found return). Bounded by
// Ghidra's function body, NOT by `.pdata` — see the extent note in
// src/engine/save/saveload_saveinfo.cpp for why `.pdata` is the wrong
// instrument for this question in this binary.
//
// WHAT IT IS: a full IN-ORDER TRAVERSAL of Factory's AVL tree, visiting every
// registered type and `_stricmp`-ing the caller's extension against each
// BaseTypeInfo's `fileExtension` (+0x18). NOT a keyed lookup — the tree is
// keyed by type id, and the extension is not the key, so there is nothing to
// descend on. First match wins; it returns the NODE's key (`*(uint32_t*)node`,
// not `info->typeId`), which the profile build asserts are equal.
//
// A type whose `fileExtension` is null is SKIPPED, not compared: the
// `TEST RCX,RCX / JZ` at 0x1800c04fb jumps to the successor step. Every
// Component/Description registration in the exe passes nullptr there, so on a
// live tree the overwhelming majority of nodes take that arm.
// ============================================================================

#include <cstdint>

#include "object/TypeInfo.h"
#include "rt_libm.h"   // B3R: rtcrt::StrICmp (0x18007eefc), called by name

using RtObject::TypeNode;

// ============================================================================
// unsigned int Object::Factory::GetTypeIdForFileExtension(const char* ext) const
// ----------------------------------------------------------------------------
// The traversal, transcribed from the disassembly rather than from Ghidra's C.
// That distinction earns its keep here: the decompiler renders the ascent as a
// single loop, and the shipped body has FOUR separate arms with one of them
// dead. They are reproduced with the dead arm marked rather than removed, so
// the reasoning is checkable against the listing.
//
//   0x1800c04ca  node = this->root            ; MOV RBX,[RCX]
//   0x1800c04d3  if (!node) return 0
//   0x1800c04e0  while (node->left)  node = node->left      ; leftmost
//   0x1800c04f1  if (!node) return 0          ; unreachable: node was non-null
//
//   visit (0x1800c04f3):
//   0x1800c04f3    info = node->value.info    ; [RBX+0x10]
//   0x1800c04f7    ext  = info->fileExtension ; [RAX+0x18]
//   0x1800c04fe    if (ext && _stricmp(ext, wanted) == 0) return *(u32*)node
//
//   successor:
//   0x1800c050c    if (node->right) {                       ; [RBX+0x40]
//   0x1800c0515        node = node->right;
//                      while (node->left) node = node->left;
//                  } else {
//   0x1800c0523        p = node->parent;                    ; [RBX+0x30]
//   0x1800c0527        if (!p)            node = node->parent;   // -> null, ends
//   0x1800c052c        else if (p->left == node) node = p;
//   0x1800c0537        else if (!p)       node = node->parent;   // *** DEAD ***
//   0x1800c0540        else { while (p && p->right == node) { node = p; p = p->parent; }
//                             node = node->parent; }
//                  }
//   0x1800c0556    if (node) goto visit
//   0x1800c055b  return 0
//
// THE DEAD ARM IS AT 0x1800c0537 and it is a re-test of `p` that 0x1800c0527
// has already proved non-null. It is transcribed as an assertion-shaped
// comment rather than as code, because writing an `if` that can never be true
// is not more faithful — it compiles to nothing and it invites a later reader
// to "fix" the surrounding logic to make it reachable.
//
// THE LAST ARM'S EXIT IS `node = node->parent`, NOT `node = p`. Those differ:
// the loop advances BOTH `node` and `p`, so on exit `node` is the last child
// walked and `node->parent` is its parent — the same value `p` holds only if
// the loop ran at least once and stopped on the `p->right != node` test. When
// the loop stops because `p` went null, `node->parent` is null too and the
// traversal ends. Transcribing `node = p` there would read an already-advanced
// pointer and is exactly the kind of algebraically-plausible substitution that
// hides where terms cancel.
// ============================================================================
