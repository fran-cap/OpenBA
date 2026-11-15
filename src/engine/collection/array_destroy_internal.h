#pragma once
// ============================================================================
// src/engine/collection/array_destroy_internal.h — Wave 9 lane D2
// ============================================================================
//
// It exists so every file that includes it drives the SAME bodies and the SAME
// DestroySpec rows as array_destroy.cpp, not a copy of them — the same reason
// array_internal.h exists.
//
// Seven files include it: array_destroy.cpp, array_graphics_anim.cpp,
// array_graphics_mesh.cpp, engine/mainloop_video.cpp, graphics/wave9_lane_gfxd.cpp,
// video/imagebuffer_platform_serialize.cpp and video/save_image_to_file.cpp.
//
// The commentary — which address each row was read out of, why Body B's two
// arms leave the slot holding different things, and why 0x18001c8c0 is Body A
// and not a CRT dependency — is the banner of array_destroy.cpp.
// ============================================================================

#include <cstdint>

namespace rtcoll {

// `offs` is in RETAIL'S OWN ORDER, which is not always ascending. Three of the
// rows walk their offsets downward and the order reaches the allocator even
// though no comparator on the element buffer can see it (the slots do not
// alias). Reproduced, not sorted.
struct DestroySpec {
    const char* name;
    uint32_t    stride;
    int         nOffs;
    uint32_t    offs[3];
};

// BODY A — free the inner Array<T>'s buffer at element+off and clear its
// 16-byte header. The clear is unconditional, including when the buffer was
// already null.
void FreeInnerArraysCore(uint8_t* base, int count, const DestroySpec* s);

// BODY B — release one System::String reference per (element, offset).
// *** The two arms leave the slot holding DIFFERENT THINGS: NULL when the
// string survives, `str - 8` when it does not. *** That asymmetry is the only
// part of either body a comparator can see; see array_destroy.cpp.
void ReleaseStringsCore(uint8_t* base, int count, const DestroySpec* s);

// Collection::Array<T*>::operator=, retail 0x180003970.
void* RtColl_ArrayAssign_180003970(void* self, const void* src);

// ---- BODY A rows ----
extern const DestroySpec kD_InnerArr_110;    // 0x18001ce90
extern const DestroySpec kD_InnerArr_18;     // 0x1800419b0
extern const DestroySpec kD_InnerArr_40;     // 0x180052110
extern const DestroySpec kD_InnerArr_48;     // 0x180074100
extern const DestroySpec kD_InnerArr3_48;    // 0x18005bce0
extern const DestroySpec kD_InnerArr3_1f0;   // 0x18001c8c0's inlined eh_vector loop

// ---- BODY B rows ----
extern const DestroySpec kD_Str_08;          // 0x180011010
extern const DestroySpec kD_Str_10;          // 0x180005930 AND 0x180026b50
                                             // (two addresses, one body — the
                                             // listings are tabulated side by
                                             // side in array_destroy.cpp)
extern const DestroySpec kD_Str_48;          // 0x180014a40
extern const DestroySpec kD_Str_18;          // 0x180034440
extern const DestroySpec kD_Str2_38;         // 0x1800059e0

}  // namespace rtcoll
