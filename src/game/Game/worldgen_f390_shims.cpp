// src/game/Game/worldgen_f390_shims.cpp
// Link shim for worldgen_f390_rivers.cpp (cd460/e910). That stage declares
// TradeComponent_helper_f9e0 (a copy of a stride-4 {data,count,cap} int bucket) as a
// GLOBAL extern "C" symbol; it is defined here once so the standalone exe links.

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

}
