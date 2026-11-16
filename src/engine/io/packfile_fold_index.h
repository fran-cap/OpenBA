#pragma once
// ============================================================================
// src/engine/io/packfile_fold_index.h -- the package directory's hash index
//                                        (block PATHINDEX, NOT RETAIL, exact)
// ============================================================================
//
// A `Collection::FoldIndex` (src/engine/collection/fold_index.h, which has the
// exactness argument) beside `IO::FileSystem::_packfileIndex`, the merged
// path -> {pack, file} directory of every mounted .pkg. It answers the four
// descents of that tree: `ExistsInPackfile` (filesystem_find.cpp),
// `GetPackfileId` (filesystem.cpp), `OpenFile` (filesystem_openfile.cpp) and
// LoadPackages' `IndexFind` (filesystem_packages.cpp).
//
// The tree changes in four places, and each keeps the index in step:
// LoadPackages' insert (`PackIndexInsert` on the fresh node), ext.package_cache's
// Restore (`PackIndexInsert` on each node it builds back), ClosePackages and
// ~FileSystem (`PackIndexClear`, before their nodes are freed). The override arm
// of LoadPackages rewrites a node's value, never its key, so the index is
// untouched by it.
// ============================================================================

#include "collection/fold_index.h"   // OPENBA_PATHINDEX_CHECK
#include "io/FileSystem.h"

namespace IO {

void PackIndexInsert(const RtIO::PackfileIndexTree* tree, const RtIO::PackfileIndexNode* node);
void PackIndexClear(const RtIO::PackfileIndexTree* tree);

// True when the index can answer for `tree` and `key`: `*out` is then the node
// the descent finds (null when it finds none). False: the caller walks.
bool PackIndexFind(const RtIO::PackfileIndexTree* tree, const char* key, RtIO::PackfileIndexNode** out);

// The debug exe's check (fold_index.h): report when the index answered and the
// walk disagrees.
void PackIndexCheck(bool answered, const char* key, const RtIO::PackfileIndexNode* indexed,
                    const RtIO::PackfileIndexNode* walked);

}  // namespace IO
