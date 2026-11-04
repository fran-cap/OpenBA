#pragma once
// ============================================================================
// src/engine/object/expand_memo.h -- the ExpandRelativePath memo
//                                     (block SAVELOAD, NOT RETAIL, exact)
// ============================================================================
//
// WHY. `ExternalFactory::FindOrLoad` expands every request through
// `ExpandRelativePath` (src/engine/io/external_path.cpp, 0x1800bd360): four
// path splits, two six-term concatenations, a package-directory probe and the
// answer's concatenation -- ~25 String allocations and ~10 AdjustDelimiters
// per call. A save load asks it once per resource REFERENCE (every entity's
// Description, MaterialInstance, ...), ~100k times for a few thousand distinct
// names; the census (LOADPLAN §2.2) put FindOrLoad at 73% of the 785 ms
// `ser.entities` stage. The answer for one request never changes while its
// inputs do not, so the second and later identical requests take it from here.
//
// WHAT THE ANSWER DEPENDS ON (read off the body, every input):
//   1. the request's characters                      -> the KEY
//   2. `appendIdentifier`                            -> the KEY
//   3. the factory's path-stack top (filename, directory). The memo is used
//      ONLY while the stack is EMPTY (the caller tests it); a request made
//      while an .rsc is being parsed always runs the body. (Nothing in this
//      tree pushes the stack today: `PushPath` has no caller.)
//   4. whether the candidate `<dataDir><stackDir><dir><fname>_<ident>.crs`
//      exists (three probes: the gated package probe, GetFileAttributesW, the
//      ungated package probe). Block PATHINDEX: WITH AN EMPTY STACK THE ANSWER
//      DOES NOT DEPEND ON IT. "Found" builds `<stackDir><dir><fname>.<ext>`,
//      "not found" builds `<dir><fname>.<ext>`, and an empty stack makes
//      `stackDir` a null Filename, which `operator+` (string.cpp: a null or
//      empty side ADOPTS the other side) turns into exactly `<dir>`'s
//      characters. The empty filename's fallback reads the stack's filename,
//      also null, so `fname` and `ext` come out empty either way. The data
//      directory and the package directory feed only the candidate, so they
//      do not feed the answer either.
// So with an empty stack the answer is a function of the key alone, and an
// entry stays true for the life of the process: there is no context to drop.
// (Block SAVELOAD's first version kept only package answers under a
// filesystem / data-directory / package-generation context; PATHINDEX dropped
// both restrictions; the debug exe's check in external_path.cpp covers it.)
//
// WHAT A HIT SKIPS. Only work with no effect outside the body: its own
// allocations, the probes (reads; GetFileAttributesW is a stat), the `g_pc`
// counters (nothing reads them), and AdjustDelimiters calls on fresh buffers or
// on already-normalised shared ones (the data directory, a no-op: AdjustDelimiters
// is idempotent, tests/unit/filename_adjust_test.cpp). The request's own buffer
// is never written by the body (its one copy, in GetPathIdentifer, happens only
// for a request with no '/' or '\', where AdjustDelimiters writes nothing).
// The returned Filename is a fresh, unshared buffer holding the same
// characters, as the body's is. `Find`, `Add` and `External::Load` in
// FindOrLoad run exactly as before: the memo is below them.
//
// THREADS. Every call is made under `ResLoadGuard` (ExpandRelativePath takes
// it first), so the state below is never touched by two threads at once.
// ============================================================================

#include <cstddef>

#include "res_load_lock.h"   // the ResLoad capability

namespace RtObject {

// The cached answer for (path[0..len), appendIdentifier), NUL-terminated, or
// null. When the answer is the request's own characters the result is `path`
// itself, so `path` must be NUL-terminated at `len` (a Filename's buffer is).
// REQUIRES(ResLoad): the THREADS rule above, checked by clang's thread-safety analysis.
const char* ExpandMemoFind(const char* path, int len, bool appendIdentifier) REQUIRES(ResLoad);

// Remember `result` (resultLen characters) for that request.
void ExpandMemoStore(const char* path, int len, bool appendIdentifier, const char* result, int resultLen)
    REQUIRES(ResLoad);

// For the unit tests: the entry count, and a full reset.
size_t ExpandMemoSize();
void   ExpandMemoClear();

}  // namespace RtObject
