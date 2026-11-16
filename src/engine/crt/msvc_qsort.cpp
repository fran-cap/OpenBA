// src/engine/crt/msvc_qsort.cpp
// Port of Runtime-x64.dll's statically linked MSVC 2012 CRT `qsort` / `shortsort`.
//
//   qsort      0x18007f050  (743 B)   Ghidra: "Library Function - Single Match qsort"
//   shortsort  0x18007f340            its small-range fallback
//
// WHY THIS EXISTS. `Save::SaveLoad::UpdateList` (saveload_update.cpp) sorts the
// save list with retail's own qsort (0x18007f050), which it calls by name as
// RtCrt_Qsort_18007f050. Every other by-address helper that
// body uses is ported; this one was tagged "CRT -- out of scope" by two lanes
// (WAVE23_LANE_INTERNAL, WAVE26_LANE_INT33) and the closure report files it
// under "C runtime routines our own CRT already provides". Our CRT provides a
// qsort SYMBOL, but nothing sat behind the retail ADDRESS, so the first
// DLL-absent click on Load Game called a null pointer: EXEC-of-0 with
// RCX=rows, RDX=13, R8=0x58, R9=the comparator -- the qsort call frame.
//
// WHY A FAITHFUL PORT AND NOT `::qsort`. qsort is not stable, and the save-list
// comparator (0x1800c3db0, saveload_saveinfo.cpp) is documented there as
// violating strict weak ordering on the placeholder rows. Which of two
// "equal" rows lands first is therefore decided by the ALGORITHM, not the
// comparator. MinGW's UCRT qsort is a different algorithm and would order the
// save list differently from retail. This body is the MSVC one: median-of-3
// pivot, explicit 62-deep lo/hi stacks, CUTOFF 8, recurse into the smaller
// half, iterate on the larger -- the same shape as the inlined exe-side
// instantiation in src/game/Work/marketwork_bst_b680.cpp, with the width and
// comparator left generic as the CRT has them.
//
// FAITHFULNESS BOUNDARY. The invalid-parameter arm (null base with num != 0,
// width == 0, null comparator) sets errno=EINVAL and calls
// _invalid_parameter_noinfo, which in a release CRT terminates the process. We
// return instead; no ported caller can reach that arm with a valid Array.

#include <cstddef>
#include <cstdint>

namespace {

using CompareFn = int (*)(const void*, const void*);

// The CRT byte-swap loop (`swap` in qsort.c): width bytes, one at a time.
inline void SwapBytes(uint8_t* a, uint8_t* b, size_t width) {
    if (a == b) return;
    while (width--) {
        uint8_t t = *a;
        *a++ = *b;
        *b++ = t;
    }
}

constexpr size_t kCutoff    = 8;    // 0x18007f0e0: `8 < size` takes the quicksort arm
constexpr size_t kStackSize = 62;   // auStack_408[62] / auStack_218[62]

} // namespace

// ============================================================================
// 0x18007f340 -- shortsort: selection sort over the INCLUSIVE range [lo, hi]
// ============================================================================
// Finds the maximum of [lo, hi] and swaps it to hi, then shrinks hi. A tie
// keeps the EARLIER element (strict `> 0` test), as in the CRT source.
extern "C" void RtCrt_Shortsort_18007f340(void* first, void* last, size_t width,
                                          CompareFn comp) {
    uint8_t* lo = static_cast<uint8_t*>(first);  // CAST-OK: crt
    uint8_t* hi = static_cast<uint8_t*>(last);  // CAST-OK: crt
    while (hi > lo) {
        uint8_t* max = lo;
        for (uint8_t* p = lo + width; p <= hi; p += width) {
            if (comp(p, max) > 0) max = p;
        }
        SwapBytes(max, hi, width);
        hi -= width;
    }
}

// ============================================================================
// 0x18007f050 -- qsort
// ============================================================================
// Transcribed against the decompilation: `_Base` is reused as `lo`, puVar11 is
// `hi`, puVar7 is `mid` (tracked through the partition), puVar6 is `loguy`,
// puVar8 is `higuy`, lVar10 is the stack pointer.
extern "C" void RtCrt_Qsort_18007f050(void* base, size_t num, size_t width,
                                      CompareFn comp) {
    if ((base == nullptr && num != 0) || width == 0 || comp == nullptr) {
        return;                                   // see FAITHFULNESS BOUNDARY
    }
    if (num < 2) return;

    uint8_t* lo = static_cast<uint8_t*>(base);  // CAST-OK: crt
    uint8_t* hi = lo + (num - 1) * width;

    uint8_t* loStk[kStackSize];
    uint8_t* hiStk[kStackSize];
    int64_t  sp = 0;

recurse:
    for (;;) {
        const size_t size = static_cast<size_t>(hi - lo) / width + 1;
        if (size <= kCutoff) {
            RtCrt_Shortsort_18007f340(lo, hi, width, comp);
            break;                                // -> pop
        }

        // ---- median-of-three into `mid` ----------------------------------
        uint8_t* mid = lo + (size >> 1) * width;
        if (comp(lo, mid) > 0)  SwapBytes(lo, mid, width);
        if (comp(lo, hi) > 0)   SwapBytes(lo, hi, width);
        if (comp(mid, hi) > 0)  SwapBytes(mid, hi, width);

        // ---- partition ---------------------------------------------------
        uint8_t* loguy = lo;
        uint8_t* higuy = hi;
        for (;;) {
            if (mid > loguy) {
                do { loguy += width; } while (loguy < mid && comp(loguy, mid) <= 0);
            }
            if (mid <= loguy) {
                do { loguy += width; } while (loguy <= hi && comp(loguy, mid) <= 0);
            }
            do { higuy -= width; } while (higuy > mid && comp(higuy, mid) > 0);

            if (higuy < loguy) break;
            SwapBytes(loguy, higuy, width);
            if (mid == higuy) mid = loguy;        // the pivot moved: follow it
        }

        // ---- collapse the run of pivot-equal elements below higuy ---------
        higuy += width;
        if (mid < higuy) {
            do { higuy -= width; } while (higuy > mid && comp(higuy, mid) == 0);
        }
        if (mid >= higuy) {
            do { higuy -= width; } while (higuy > lo && comp(higuy, mid) == 0);
        }

        // ---- recurse into the smaller half, iterate on the larger ---------
        if ((higuy - lo) >= (hi - loguy)) {
            if (lo < higuy) {
                loStk[sp] = lo;
                hiStk[sp] = higuy;
                ++sp;
            }
            if (loguy < hi) {
                lo = loguy;
                goto recurse;
            }
            break;                                // -> pop
        } else {
            if (loguy < hi) {
                loStk[sp] = loguy;
                hiStk[sp] = hi;
                ++sp;
            }
            if (lo < higuy) {
                hi = higuy;
                goto recurse;
            }
            break;                                // -> pop
        }
    }

    --sp;
    if (sp < 0) return;
    lo = loStk[sp];
    hi = hiStk[sp];
    goto recurse;
}
