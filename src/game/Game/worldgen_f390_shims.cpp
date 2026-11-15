// src/game/Game/worldgen_f390_shims.cpp
// Link shims for worldgen_f390_rivers.cpp (cd460/e910). That stage declares a few
// helpers as GLOBAL extern "C" symbols; the two it calls are defined here once so the
// standalone exe links.
//
// All semantics match how rivers.cpp actually calls them (verified at the call sites):
//   - FUN_140014c30           is only called on a null AVL root (dead path) -> no-op
//   - TradeComponent_helper_f9e0 copies a stride-4 {data,count,cap} int bucket

#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include <cstdint>
#include <cmath>
#include "system/Value.h"      // ENDGAME B.3.4b System caller batch (Random/Time/Color)
#include "Game/exe_bodies.h"
#include "Action/PathSearch.h"   // RawArray<T>

extern "C" {

// vector<int> bucket deep-copy ({data@+0, count@+8, cap@+0xC}, stride-4 elements).
// Its one caller is worldgen_f390_rivers.cpp's cluster-bucket resize.
void TradeComponent_helper_f9e0(RawArray<int32_t>* d, const RawArray<int32_t>* s) {
    if (d == s) return;
    int32_t* old = d->data;
    if (old) System::MemoryAllocator::MemFree(old);
    d->size = 0;
    d->data = nullptr;
    int32_t cnt = s->size;
    d->size = cnt;
    if (cnt > 0) {
        d->capacity = cnt;
        d->data = static_cast<int32_t*>(System::MemoryAllocator::MemAlloc((uint64_t)cnt << 2, 4));
        int32_t* dp = d->data;
        const int32_t* sp = s->data;
        for (int32_t i = 0; i < cnt; ++i) dp[i] = sp[i];
    }
}

// AVL recursive subtree free — rivers.cpp only calls this on a null root (the emulated
// cluster partition uses no AVL tree), so this is a no-op.
void FUN_140014c30(void** /*root*/, void* /*node*/) {}

}
