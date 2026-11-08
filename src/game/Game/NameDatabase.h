#pragma once
// ============================================================================
// src/game/Game/NameDatabase.h — ENDGAME B.3.8 block LOOP (B38_RULES D4, sec. 2.2)
//
// `NameDatabase` (0x60): the ".ndb" name-generation resource (Game/Names/
// Cities.ndb, Female.ndb, ...), a real `Object::External` subclass. Layout from
// factory 0x14001c660, dtor body 0x1400a4790, Serialize 0x1400a45e0.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md):
//   reflected "NameDatabase" (file extension "ndb"), parent "Object::External",
//   table 0x14015eea0, 9 slots, sizeof 0x60.
//   [0] 0x14001c790 scalar deleting dtor (body 0x1400a4790)
//   [1] TypeName  [2] Type
//   [3] 0x1400a4780 GetFileVersion `MOV EAX,0x401`
//   [4] Compile — OWN `MOV EAX,1` (not the import)
//   [5] 0x1400a45e0 Serialize              [6] 0x14001c730 Reconstruct
//   [7] 0x140001640 Destruct               [8] 0x14001c6d0 Construct
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "object/External.h"
#include "Object/exe_type.h"
#include "collection/Collection.h"

// One element of the +0x20 array (stride 0xc): Serialize 0x1400a45e0 writes it
// as four raw bytes, a uchar and a uint. No field has a retail name.
struct NameDatabaseEntry {
    uint32_t context;     // +0x00 the 4-gram: SerializeBytes(4); the name generator reads it as one dword
    uint8_t  isWordEnd;     // +0x04 uchar
    uint8_t  _pad05[3];      // +0x05
    uint32_t nextCharMask;     // +0x08 uint
};
static_assert(sizeof(NameDatabaseEntry) == 0xC, "stride 0xc (0x14001c7c0)");
static_assert(offsetof(NameDatabaseEntry, context) == 0x0, "NameDatabaseEntry::context");
static_assert(offsetof(NameDatabaseEntry, _pad05) == 0x5, "NameDatabaseEntry::_pad05");
static_assert(offsetof(NameDatabaseEntry, isWordEnd) == 0x04, "+0x04");
static_assert(offsetof(NameDatabaseEntry, nextCharMask) == 0x08, "+0x08");

class NameDatabase final : public Object::External {
    DECLARE_EXE_TYPE(NameDatabase)

public:
    NameDatabase();                                               // factory 0x14001c660 body

    // body 0x1400a4790
    ~NameDatabase() override;                                     // [0] 0x14001c790
    int32_t GetFileVersion() const override { return 0x401; }     // [3] 0x1400a4780
    CompileResult Compile(const System::Filename&) override { return 1; }   // [4] own `return 1`
    bool Serialize(IO::Stream& stream) override;                        // [5] 0x1400a45e0
    Object::External* Reconstruct() override;                     // [6] 0x14001c730
    void Destruct() override;                                     // [7] 0x140001640 this->~T()
    virtual Object::External* Construct();                        // [8] 0x14001c6d0

public:
    // Four Collection::Arrays (B.4 GAME-b: the factory's ctor zeroed the four
    // headers, Array()'s stores; the dtor 0x1400a4790 is four trivial-element
    // ~Arrays back to front, the compiler's member order).
    Collection::Array<NameDatabaseEntry> _array1;   // +0x20: stride 0xc
    Collection::Array<uint16_t>          _array2;   // +0x30: stride 0x2
    Collection::Array<char>              _array3;   // +0x40: stride 0x1, the reject-name C-string pool (Serialize 0x1400a45e0 hands it to Serialize(int, char*))
    Collection::Array<int32_t>           _array4;   // +0x50: stride 0x4
};

static_assert(sizeof(NameDatabase) == 0x60, "NameDatabase is 0x60");
static_assert(offsetof(NameDatabase, _array1) == 0x20, "+0x20");
static_assert(offsetof(NameDatabase, _array2) == 0x30, "+0x30");
static_assert(offsetof(NameDatabase, _array3) == 0x40, "+0x40");
static_assert(offsetof(NameDatabase, _array4) == 0x50, "+0x50");

