// ============================================================================
// src/engine/volume/box.cpp — Volume::Box
// ============================================================================
//
// Wave 1, Lane 3 of the Runtime-x64.dll port (docs/runtime_port/PLAN.md §4.2/§6).
// Every function below is a byte-faithful port of the decompilation at the
// retail address named in its comment (Runtime-x64.dll, image base
// 0x180000000, decompiled 2026-09-06). See src/engine/volume/Volume.h for
// the governing ABI note and the Box layout.
//
// Volume::Box has no dedicated original source file recovered from the
// profile build's debug strings (only Frustum.cpp/.h are listed — see
// src/engine/volume/README.md); its methods are ported here, and Rectangle's
// in src/engine/volume/rectangle.cpp.
// ============================================================================

#include "volume/Volume.h"

// ??0Box@Volume@@QEAA@XZ   retail 0x180011ea0
// Default ctor: empty box (min = +1.0e8f, max = -1.0e8f) so the first
// Extend() call always wins on both ends.
Box* RtVolume_Box_Ctor(Box* self) {
    self->min = { RtMathConst::kBigPos, RtMathConst::kBigPos, RtMathConst::kBigPos };
    self->max = { RtMathConst::kBigNeg, RtMathConst::kBigNeg, RtMathConst::kBigNeg };
    return self;
}

// ??4Box@Volume@@QEAAAEAV01@AEBV01@@Z   retail 0x180011e70  (operator=)
Box* RtVolume_Box_Assign(Box* self, const Box* other) {
    Box before = *self;   // a copy nothing reads
    *self = *other;
    return self;
}

// ?Extend@Box@Volume@@QEBA?AV12@AEBVVector3@Math@@@Z   retail 0x180011ff0
// Returns a NEW Box (hidden return slot): {min(this.min, p), max(this.max, p)}.
Box* RtVolume_Box_Extend(const Box* self, Box* ret, const Vector3* p) {
    // Retail's exact comparison form (both branches test `<=` and pick `p`
    // on the true side) — NOT rewritten as `<`/`>` with the operands
    // swapped: that reads as equivalent for ordinary values but disagrees
    // on NaN (retail's `<=` is false on NaN and keeps `self`; a naive `<`
    // with the operands flipped would be false on NaN too but for the
    // WRONG reason, and picks `p` instead).
    ret->min.x = p->x <= self->min.x ? p->x : self->min.x;
    ret->min.y = p->y <= self->min.y ? p->y : self->min.y;
    ret->min.z = p->z <= self->min.z ? p->z : self->min.z;
    ret->max.x = self->max.x <= p->x ? p->x : self->max.x;
    ret->max.y = self->max.y <= p->y ? p->y : self->max.y;
    ret->max.z = self->max.z <= p->z ? p->z : self->max.z;
    return ret;
}

// ?GetCorners@Box@Volume@@QEBAXPEAVVector3@Math@@@Z   retail 0x1800121b0
// Writes the box's 8 corners through `out` (caller-owned array of 8 Vector3),
// x-major then y then z: {minx,maxx} x {miny,maxy} x {minz,maxz}.
void RtVolume_Box_GetCorners(const Box* self, Vector3* out) {
    out[0] = { self->min.x, self->min.y, self->min.z };
    out[1] = { self->min.x, self->min.y, self->max.z };
    out[2] = { self->min.x, self->max.y, self->min.z };
    out[3] = { self->min.x, self->max.y, self->max.z };
    out[4] = { self->max.x, self->min.y, self->min.z };
    out[5] = { self->max.x, self->min.y, self->max.z };
    out[6] = { self->max.x, self->max.y, self->min.z };
    out[7] = { self->max.x, self->max.y, self->max.z };
}

// ?Intersect@Box@Volume@@QEBA_NAEBV12@@Z   retail 0x180012160
// Standard AABB overlap test: overlap on every axis.
bool RtVolume_Box_Intersect(const Box* self, const Box* other) {
    bool ours = self->min.x <= other->max.x && other->min.x <= self->max.x &&
                self->min.y <= other->max.y && other->min.y <= self->max.y &&
                self->min.z <= other->max.z && other->min.z <= self->max.z;
    return ours;
}
