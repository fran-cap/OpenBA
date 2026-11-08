#pragma once
// ============================================================================
// src/engine/collision2d/Surface.h — Collision2D::Surface, 0x30 bytes,
//                                        9 slots (original `Surface.h`)
// ============================================================================
//
// ENDGAME B.3 step 9, block ENG (B39_RULES.md D1/D2). Until this commit the
// class was a file-local POD in src/engine/collision2d/surface.cpp and every
// live Surface was built by the FACTREST step table, wearing the MINTED
// `??_7Surface@Collision2D@@6B@`. It is a real class now: retail's public
// constructor (??0Surface 0x18010c9a0), its own slot 0, and the factory pair
// as its own statics -- every live Surface wears the COMPILER's table.
//
// `??_7Surface@Collision2D@@6B@` @ 0x180158ee0, NINE slots:
//   [0] +0x00 0x180064ea0  (unexported) vector deleting dtor, element 0x30
//   [1] +0x08 0x180064a70  ?TypeName@Surface@Collision2D@@UEBAPEBDXZ
//   [2] +0x10 0x180064a50  ?Type@Surface@Collision2D@@UEBAIXZ
//   [3] +0x18 0x18010ca30  ?GetFileVersion@Surface@Collision2D@@UEBAHXZ (returns 1)
//   [4] +0x20 0x1800066e0  ?Compile@External@Object@@  INHERITED (not overridden)
//   [5] +0x28 0x18010ca40  ?Serialize@Surface@Collision2D@@UEAA_NAEAVStream@IO@@@Z
//   [6] +0x30 0x180064c70  ?Reconstruct@Surface@Collision2D@@UEAAPEAVExternal@Object@@XZ
//   [7] +0x38 0x180064d40  ?Destruct@Surface@Collision2D@@UEAAXXZ
//   [8] +0x40 0x180064ce0  ?Construct@Surface@Collision2D@@UEAAPEAVExternal@Object@@XZ
//
// Layout (factory pair 0x180064b60 / 0x180064c10: +0x20 zeroed as ONE qword,
// +0x28 as ONE dword; +0x2c is never written):
//   +0x20  uint32_t _reportOnly  Serialize order: _reportOnly, _color, _mask
//   +0x24  uint32_t _mask
//   +0x28  uint32_t _color
//   +0x2c  uint32_t _pad2c   never written (8-byte tail alignment)
// Retail exports three getters (`Mask` 0x180064d50, `Color` 0x180064d60,
// `IsReportOnly` 0x180064d70) that name the fields; they are not ported
// and have no caller in the tree, but the fields carry their names.
// ============================================================================

#include <cstdint>
#include <cstddef>

#include "../object/External.h"
#include "../object/Internal.h"                // Object::detail::InternalMemFree

namespace Collision2D {

class Surface : public Object::External {
public:
    // ??0Surface@Collision2D@@QEAA@XZ 0x18010c9a0 -- the REAL constructor:
    // External() (Base's and External's vptr and zero stores), Surface's vptr
    // (the compiler's), then the qword at +0x20 and the dword at +0x28.
    Surface() : Object::External() {
        _reportOnly = 0;
        _mask = 0;
        _color = 0;
    }

    // [0] 0x180064ea0 -- MSVC's VECTOR deleting destructor (retail; here
    // the virtual destructor, B3X W1) (read this pass: `TEST DL,2`,
    // `MOV EDX,0x30`, CALL 0x18007ec98 the eh-vector dtor over ~Surface
    // 0x18010ca00; deleting_dtor.cpp's old "scalar arm only" note was wrong).
    // Nothing `new[]`s a Surface.
    // ??1Surface@Collision2D@@UEAA@XZ 0x18010ca00: restamp, then ~External.
    // VIRTUAL (B3X W1); no owned resource -- the compiler chains ~External.
    ~Surface() override;
    // [1] 0x180064a70 TypeName / [2] 0x180064a50 Type, the static TypeId
    // 0x180064a60, `_TypeName` / `_ParentTypeName` and the factory pair
    // 0x180064b60 / 0x180064c10 (surface.cpp): retail's macro (ENDGAME B.3.9
    // TI-REST, B39_RULES D6); the record is `DEFINE_TYPE`d in
    // src/engine/collision2d/collision2d_type_records.cpp.
    DECLARE_TYPE(Surface)
public:
    int32_t           GetFileVersion() const override;         // [3] 0x18010ca30
    bool              Serialize(IO::Stream& stream) override;        // [5] 0x18010ca40
    Object::External* Reconstruct() override;                  // [6] 0x180064c70
    void              Destruct() override;                     // [7] 0x180064d40
    virtual Object::External* Construct();                     // [8] 0x180064ce0

    // Retail's factory pair (B39_RULES D2; declared by DECLARE_TYPE), defined
    // in surface.cpp. The class has its own placement `operator new` in retail
    // (0x180064af0, not ported): every placement construction spells
    // `::new (p) Surface()`.

public:
    uint32_t _reportOnly;     // +0x20
    uint32_t _mask;     // +0x24
    uint32_t _color;     // +0x28
    uint32_t _pad2c;   // +0x2c  never written
};

static_assert(sizeof(Surface) == 0x30, "Collision2D::Surface is 0x30 bytes (element size of slot 0)");
static_assert(offsetof(Surface, _pad2c) == 0x2C, "Surface::_pad2c");
static_assert(alignof(Surface) == 8, "Collision2D::Surface aligns to 8 (the vptr)");
static_assert(offsetof(Surface, _reportOnly) == 0x20, "Surface +0x20");
static_assert(offsetof(Surface, _mask) == 0x24, "Surface +0x24");
static_assert(offsetof(Surface, _color) == 0x28, "Surface +0x28");
static_assert(offsetof(Surface, _reportOnly) == sizeof(Object::External),
              "Surface's own bytes begin right after Object::External");

}  // namespace Collision2D
