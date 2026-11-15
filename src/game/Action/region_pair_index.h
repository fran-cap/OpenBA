#pragma once
// src/game/Action/region_pair_index.h -- a hash mirror of RegionPairCache::index (NOT RETAIL).
//
// RegionPairCache (Action/PathSearch.h, Map +0x78) keeps up to 0x20000 (larger, smaller) region
// pairs in a ring and indexes them with an intrusive AVL tree (key0 = larger id, key1 = smaller id,
// key2 = ring slot). Map::PathDistance 0x14009f810 asks RegionPairCache::Find 0x1400a5ac0 thousands
// of times per Update; each Find is a 15-17 level descent of that tree. The index is a SET keyed on
// (key0, key1): Store 0x1400a5b30 inserts a key only when its descent did not find it, and every
// removal (Store's ring-full eviction, Expire 0x1400a5d10) removes the node its descent found. So
// "the slot of key K" is a function of the set of live (key, slot) pairs, not of the tree's shape.
//
// This table holds exactly the same (key, slot) pairs. It is updated at the same three places the
// tree is (one Insert beside Store's AvlInsert30, one Erase beside each WorkCellBST_Remove_ca60 on
// the cache's index), and answers Find's and Store's lookups in one probe. The tree itself is still
// built, rebalanced and removed from exactly as retail does: BuildStructure reads it directly
// (buildstructure.cpp) and the dtor frees it.
//
// OWNERSHIP. The table belongs to one cache: Reset(cache) at Pf_RegionPairCache_Build. Every
// call names its cache; a cache that is not the owner (a unit test's raw record, a second game
// before its Build) is answered by the caller's AVL path (Owns() false). Main thread only, as the
// cache is.
//
// VERIFY. At COMPPROF=4 (g_verifyOn) Find and Store also run retail's descent and compare
// (StressHarness_Verify "PairCacheIndex").
#include <cstdint>

struct RegionPairCache;

namespace RegionPairIndex {

// Empties the table and makes `owner` its cache.
void Reset(const RegionPairCache* owner);
// The cache whose index this table mirrors.
bool Owns(const RegionPairCache* cache);
// The ring slot of (hi, lo), or -1 when the pair is not indexed. hi >= lo (retail's normalisation).
int Find(int hi, int lo);
// Mirror an AvlInsert30 of {hi, lo, slot} (the key was absent).
void Insert(int hi, int lo, int slot);
// Mirror a WorkCellBST_Remove_ca60 of the node keyed (hi, lo).
void Erase(int hi, int lo);
// Live pairs (unit tests and the verify shadow).
int Count();

}  // namespace RegionPairIndex
