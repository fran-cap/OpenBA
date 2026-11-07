// ============================================================================
// tests/unit/save_abi_test.cpp — ENDGAME B.3.7 block B2 (Save)
//   Save::SaveLoad, Save::SaveLoad::SaveInfo, Save::SaveLoad::ModInfo,
//   Save::SavedState
//
// The conversion rests on these claims, each pinned here against the SHIPPED
// header (src/engine/save/SaveLoad.h), not a local copy:
//
//   1. LAYOUT — every offset, sizeof and alignof read from retail bytes
//      (ModInfo 0x20 is closed from three retail bodies, B37_RULES 13.2).
//   2. NOT TRIVIALLY COPYABLE — SaveLoad, SaveInfo and ModInfo all have deep
//      retail copies (RULE B7-8); none may ever be memcpy'd as a value.
//   3. THE INLINE CTORS WRITE ONLY WHAT RETAIL WRITES — ModInfo() zeroes the
//      two Strings and nothing else (0x180034010's init arm); SaveInfo(index)
//      is UpdateList state 1's stack template (eleven stores, and NEVER +0x0c,
//      +0x20, +0x24 — +0x28 is a QWORD store, so +0x2c IS written);
//      SavedState() / SavedState(id) are 0x1800c0ee0 /
//      0x1800c0ec0. Each is placement-newed into POISONED storage.
//   4. ARGUMENT PASSING (RULE B7-5) — `SetSaveSprite`'s Sprite and
//      `SaveMetaData`'s String arrive by HIDDEN POINTER; `Draw`'s
//      `ExternalPtr<Material>` is a trivially copyable 8-byte value (a bare
//      register); the two String getters return BY VALUE (non-virtual retslot,
//      the doors keep `(this, ret)`). The member-pointer types are pinned, and
//      one raw RETAIL-ORDER call — through the `engine_funcs.h` typedef shape
//      `void (*)(void* saveLoad, void* sprite)` — lands both qwords.
//   5. THE INLINE ACCESSORS ARE RETAIL'S BODIES — GetSaveInfo is unchecked
//      `_data + index*0x58` (a negative index is NOT caught), GetSaveCount is
//      +0x58, GetOperation +0x00, GetSaveState +0x40.
//
// WHAT IS *NOT* PROVEN HERE: every out-of-line body (the UpdateXxx machines,
// CheckMod, the copy ctors and destructors) stands on the port registry and the
// allocator and is verified by the real run and the resave gate. No test here
// constructs an object whose destructor is out of line: storage is poisoned,
// placement-newed, inspected, and never destroyed.
// ============================================================================
#include "unit.h"

#include "save/SaveLoad.h"
#include "system/String.h"
#include "object/External.h"

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <new>
#include <type_traits>

namespace {

using Save::SaveLoad;
using Save::SavedState;
using SaveInfo = Save::SaveLoad::SaveInfo;
using ModInfo  = Save::SaveLoad::ModInfo;

constexpr uint8_t kPoison = 0xcd;

uint32_t U32At(const void* base, size_t off) {
    uint32_t v;
    std::memcpy(&v, static_cast<const uint8_t*>(base) + off, sizeof v);
    return v;
}
uint64_t U64At(const void* base, size_t off) {
    uint64_t v;
    std::memcpy(&v, static_cast<const uint8_t*>(base) + off, sizeof v);
    return v;
}

// ---- 1 + 2: layout and copy semantics --------------------------------------
UNIT_TEST(save_layout_sizes_and_alignment) {
    CHECK_EQ((int)alignof(SaveLoad), 8);
    CHECK_EQ((int)alignof(SaveInfo), 8);
    CHECK_EQ((int)alignof(ModInfo), 8);
    CHECK_EQ((int)alignof(SavedState), 8);
    CHECK_EQ((int)sizeof(SaveLoad::SaveInfoArray), 0x10);
    CHECK_EQ((int)sizeof(Collection::Array<ModInfo>), 0x10);

    CHECK(!std::is_polymorphic<SaveLoad>::value);
    CHECK(!std::is_polymorphic<SaveInfo>::value);
    CHECK(!std::is_polymorphic<ModInfo>::value);
    CHECK(!std::is_polymorphic<SavedState>::value);

    CHECK(!std::is_trivially_copyable<SaveLoad>::value);
    CHECK(!std::is_trivially_copyable<SaveInfo>::value);
    CHECK(!std::is_trivially_copyable<ModInfo>::value);
    CHECK(!std::is_trivially_copyable<SavedState>::value);   // user dtor frees _data

    // The RtSave:: names are aliases of the classes, not second layouts.
    CHECK((std::is_same<RtSave::SaveLoad, SaveLoad>::value));
    CHECK((std::is_same<RtSave::SaveInfo, SaveInfo>::value));
    CHECK((std::is_same<RtSave::ModInfo, ModInfo>::value));
    CHECK((std::is_same<RtSave::SavedState, SavedState>::value));
    CHECK((std::is_same<RtSave::Sprite, UI::Sprite>::value));
    CHECK_EQ((int)RtSave::kOpDelete, 5);
}

// ---- 3: the inline constructors write only what retail writes ---------------
UNIT_TEST(save_modinfo_default_ctor_is_the_array_init_arm) {
    alignas(ModInfo) uint8_t storage[sizeof(ModInfo)];
    std::memset(storage, kPoison, sizeof storage);
    ModInfo* m = new (storage) ModInfo();
    CHECK(m->name == nullptr);
    CHECK(m->path == nullptr);
    // 0x180034010's init arm stores the two String qwords and nothing else.
    for (size_t off = 0x10; off < 0x20; off += 4)
        CHECK_EQ(U32At(storage, off), 0xcdcdcdcdu);
}

UNIT_TEST(save_saveinfo_template_ctor_is_updatelist_state1) {
    alignas(SaveInfo) uint8_t storage[sizeof(SaveInfo)];
    std::memset(storage, kPoison, sizeof storage);
    SaveInfo* row = new (storage) SaveInfo(7);

    CHECK(row->thumbnail.spriteSheet == nullptr);               // +0x00 qword 0
    CHECK_EQ(row->thumbnail.spriteIndex, -1);                   // +0x08 dword -1
    CHECK(row->description == nullptr);                         // +0x10
    CHECK(row->imageBuffer == nullptr);                         // +0x18
    CHECK(row->thumbPixels == nullptr);                         // +0x28
    CHECK_EQ(row->sortKey._lowDateTime, 0u);                    // +0x30
    CHECK_EQ(row->sortKey._highDateTime, 0u);                   // +0x34
    CHECK(row->name == nullptr);                                // +0x38
    CHECK_EQ(row->saveIndex, 7);                                // +0x40
    CHECK_EQ(row->field44, -1);                                 // +0x44
    CHECK(row->mods._data == nullptr);                          // +0x48
    CHECK_EQ(row->mods._size, 0);                               // +0x50
    CHECK_EQ(row->mods._capacity, 0);                           // +0x54

    // The three the retail stack template NEVER writes.
    CHECK_EQ(U32At(storage, 0x0c), 0xcdcdcdcdu);
    CHECK_EQ(U32At(storage, 0x20), 0xcdcdcdcdu);
    CHECK_EQ(U32At(storage, 0x24), 0xcdcdcdcdu);
    // +0x28 is a QWORD store in retail (0x1800c1fc3), so +0x2c is zero.
    CHECK_EQ(U32At(storage, 0x2c), 0u);

    // The blank "new save" row is the -1 template.
    std::memset(storage, kPoison, sizeof storage);
    SaveInfo* blank = new (storage) SaveInfo(-1);
    CHECK_EQ(blank->saveIndex, -1);
    CHECK_EQ(U32At(storage, 0x20), 0xcdcdcdcdu);
}

UNIT_TEST(save_savedstate_ctors_setid_and_shallow_assign) {
    alignas(SavedState) uint8_t a[sizeof(SavedState)];
    alignas(SavedState) uint8_t b[sizeof(SavedState)];
    std::memset(a, kPoison, sizeof a);
    std::memset(b, kPoison, sizeof b);

    SavedState* s0 = new (a) SavedState();           // 0x1800c0ee0
    CHECK_EQ(s0->_id, 0u);
    CHECK_EQ(s0->_size, 0u);
    CHECK(s0->_data == nullptr);

    SavedState* s1 = new (b) SavedState(0x42u);      // 0x1800c0ec0
    CHECK_EQ(s1->_id, 0x42u);
    CHECK_EQ(s1->_size, 0u);
    CHECK(s1->_data == nullptr);

    s1->SetId(9u);                                   // 0x1800c0e90
    CHECK_EQ(U32At(b, 0), 9u);

    // ??4 0x180030bd0 — two qword copies: the buffer pointer is SHARED.
    int payload = 0;
    s1->_size = 4;
    s1->_data = &payload;
    *s0 = *s1;
    CHECK_EQ(s0->_id, 9u);
    CHECK_EQ(s0->_size, 4u);
    CHECK(s0->_data == &payload);
    // (Neither is destroyed: ~SavedState frees through the allocator.)
}

// ---- 4: argument passing ----------------------------------------------------
UNIT_TEST(save_member_signatures_pin_the_argument_passing) {
    // Hidden-pointer arguments, in retail order.
    CHECK((std::is_same<decltype(&SaveLoad::SetSaveSprite),
                        void (SaveLoad::*)(const UI::Sprite*)>::value));
    CHECK((std::is_same<decltype(&SaveLoad::SaveMetaData),
                        void (SaveLoad::*)(void*, int, int, System::String*, int)>::value));
    // References where retail's mangling spells references.
    CHECK((std::is_same<decltype(&SaveLoad::LoadMetaData),
                        void (SaveLoad::*)(System::String&, void**, int&, int&, int&,
                                           Collection::Array<ModInfo>&)>::value));
    CHECK((std::is_same<decltype(&SaveLoad::SetSaveName),
                        void (SaveLoad::*)(const System::String&, int)>::value));
    // Draw's ExternalPtr is by value AND trivially copyable: a bare register.
    CHECK((std::is_same<decltype(&SaveLoad::Draw),
                        void (SaveLoad::*)(Video::Context&,
                                           Object::ExternalPtr<Graphics::Material>)>::value));
    CHECK(std::is_trivially_copyable<Object::ExternalPtr<Graphics::Material>>::value);
    // The two retslot getters return the class by value (non-virtual: the
    // doors keep `(this, ret)` and placement-new the prvalue).
    CHECK((std::is_same<decltype(&SaveLoad::GetAutoSaveName),
                        System::String (SaveLoad::*)() const>::value));
    CHECK((std::is_same<decltype(&SaveLoad::GenerateSaveName),
                        System::String (SaveLoad::*)() const>::value));
    // The enum comes back in a register (`?AW4`), not a retslot.
    CHECK((std::is_same<decltype(&SaveLoad::GetOperation),
                        SaveLoad::Operation (SaveLoad::*)() const>::value));
    CHECK_EQ((int)sizeof(SaveLoad::Operation), 4);
}

// A door with the registered entry point's shape, delegating the way
// saveload.cpp's does. Called below through the engine_funcs.h typedef
// `void (*)(void* saveLoad, void* sprite)` — a raw retail-order caller.
void SetSaveSpriteDoor(SaveLoad* self, const UI::Sprite* sprite) {
    self->Save::SaveLoad::SetSaveSprite(sprite);
}

UNIT_TEST(save_setsavesprite_hidden_pointer_raw_retail_order_call) {
    alignas(SaveLoad) uint8_t storage[sizeof(SaveLoad)];
    std::memset(storage, kPoison, sizeof storage);

    alignas(UI::Sprite) uint8_t spriteBytes[sizeof(UI::Sprite)];
    for (size_t i = 0; i < sizeof spriteBytes; ++i) spriteBytes[i] = static_cast<uint8_t>(0x10 + i);

    using RawFn = void (*)(void* saveLoad, void* sprite);
    RawFn raw = reinterpret_cast<RawFn>(&SetSaveSpriteDoor);
    raw(storage, spriteBytes);

    // Both qwords, pad0C included (retail: two MOV qword pairs).
    CHECK(std::memcmp(storage + 0x60, spriteBytes, 0x10) == 0);
    // Nothing else moved.
    CHECK_EQ(U64At(storage, 0x58), 0xcdcdcdcdcdcdcdcdull);
    CHECK_EQ(U64At(storage, 0x70), 0xcdcdcdcdcdcdcdcdull);
}

// ---- 5: the inline accessors ------------------------------------------------
UNIT_TEST(save_inline_accessors_are_retail_bodies) {
    alignas(SaveLoad) uint8_t storage[sizeof(SaveLoad)];
    std::memset(storage, kPoison, sizeof storage);
    SaveLoad* sl = reinterpret_cast<SaveLoad*>(storage);

    alignas(SaveInfo) uint8_t rows[3 * sizeof(SaveInfo)];
    sl->_saves._data = reinterpret_cast<SaveInfo*>(rows + sizeof(SaveInfo));
    sl->_saves._size = 2;
    sl->_operation = SaveLoad::kOpSave;
    SavedState* state = reinterpret_cast<SavedState*>(rows);   // any address
    sl->_saveState = state;

    CHECK_EQ(sl->GetSaveCount(), 2);                                   // +0x58
    CHECK_EQ((int)sl->GetOperation(), (int)SaveLoad::kOpSave);         // +0x00
    CHECK(sl->GetSaveState() == state);                                // +0x40
    CHECK(&sl->GetSaveInfo(1) ==
          reinterpret_cast<const SaveInfo*>(rows + 2 * sizeof(SaveInfo)));
    // MOVSXD + IMUL with no bounds check: -1 is one row BEFORE _data.
    CHECK(&sl->GetSaveInfo(-1) == reinterpret_cast<const SaveInfo*>(rows));
}

}  // namespace
