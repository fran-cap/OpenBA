#pragma once
// ============================================================================
// src/engine/io/Modification.h — IO::Modification binary layout
// ============================================================================
// Wave 4, lane D modelled only the three fields
// ?Find@Modification@IO@@QEBAHAEBVFilename@System@@@Z (retail 0x1800aea10)
// touches. WAVE 9 LANE MOD completes the record, because the mod-entry
// lifecycle triple (copy ctor 0x1800101e0, operator= 0x180026ef0, dtor
// 0x180011460) walks EVERY member of Mod and a port of them needs the whole
// 0x80 laid out, not just the stride.
//
// Every offset below was read out of one of those three bodies, which is the
// strongest source available for a layout: the copy ctor visits the members in
// declaration order and its element helper tells you the type of each.
//
//   +0x00/08/10  three refcounted System::String   (adopt/release idiom)
//   +0x18..0x2c  six int32 copied field by field
//   +0x30        Collection::Array<int32>          (op= 0x180011110, stride 4)
//   +0x40        Collection::Array<uint8>          (op= 0x180011220, stride 1)
//   +0x50        bool enabled                      (MOVZX/MOV byte)
//   +0x58        System::Filename                  (ctor-copy 0x1800067d0)
//   +0x60        Collection::Set<Filename> root+count  (clone 0x1800104f0)
//   +0x70        Collection::Array<Filename>       (op= 0x1800091f0, stride 8)
//
// The two Modification header flags come from their own accessors, which is
// why they are named rather than guessed:
//   +0x00 _dirty          — ?IsDirty@Modification@IO@@QEBA_NXZ is `MOVZX EAX,
//                           byte ptr [RCX]; RET`.
//   +0x01 _savingSettings — ?EnableSavingSettings@ (0x1800aead0) is the only
//                           writer; the ctor (0x1800b04b0) opens with
//                           `MOV word ptr [RCX],0x101`, i.e. BOTH start at 1.
//                           That is why SaveList/RefreshList take their
//                           registry branch on a normal boot.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <type_traits>   // B.3.4 PACKAGE+FS: the "IO::Modification has no vptr" pins

#include "../system/Filename.h"
#include "../system/String.h"
#include "../collection/Collection.h"   // B.4 acceptance MOD: Mod's Array / Set members

namespace Object { class External; }   // ReloadEntry::external

namespace RtIO {

// Collection::Array<T> header, 16 bytes and T-independent (Collection.h in the
// Wave 2 tree static_asserts the same thing). Spelled locally so this header
// stays includable from engine/ without dragging the collection tree in.
//
// B.4 P2 R-IO: a TYPED view of the one header (the container policy: typed
// template wrappers over the one erased core). `ModArrayOf<T>` is the same 16
// bytes for every T -- a POD, trivially constructible and destructible, so no
// ctor or dtor runs that did not run before -- and `data` carries the element
// type the owning member's retail operator= / ~Array strides by (Mod's +0x30
// int32, +0x40 uint8, +0x70 Filename; the copy ctor 0x1800101e0 names each).
// `ModArray` is the erased spelling the void* core entry points still take.
template <class T>
struct ModArrayOf {
    T*      data;       // +0x00
    int32_t size;       // +0x08
    int32_t capacity;   // +0x0c
};
using ModArray = ModArrayOf<void>;
static_assert(sizeof(ModArray) == 0x10, "Collection::Array is 16 bytes");
static_assert(offsetof(ModArray, size)     == 0x08, "Array::_size @ +0x08");
static_assert(offsetof(ModArray, capacity) == 0x0c, "Array::_capacity @ +0x0c");
static_assert(sizeof(ModArrayOf<int32_t>) == sizeof(ModArray), "one header for every T");

// One node of the Filename-keyed set at Mod+0x60. Allocated at 0x180010570
// (`MOV EDX,0x28` size, `LEA R8D,[RDX-0x20]` align 8) and cloned at 0x1800104f0.
struct ModSetNode {
    // B.4 Z MIRROR: the real class. The node is raw MemAlloc storage
    // (0x180010570) that is never constructed, copied or destroyed as a
    // ModSetNode: `key` is placement-constructed by the copy ctor and released
    // by the Set<Filename> node frees, so typing it changes no code.
    System::Filename   key;      // +0x00 — copy-constructed by 0x1800067d0
    ModSetNode*        parent;   // +0x08
    ModSetNode*        left;     // +0x10
    ModSetNode*        right;    // +0x18
    // +0x20 / +0x22: the AVL balance factor and height. SIGNED: the retrace
    // (0x1800274c0) tests them with `CMP CX,1 / JLE`, `CMP word [RDX+0x20],-1 /
    // JGE` and `TEST CX,CX / JS`, and AvlUpdate writes +0x20 = rh - lh, +0x22 =
    // max(lh, rh) (0x180027529 / 0x180027525). The clone copies both verbatim.
    int16_t            balance;  // +0x20
    int16_t            height;   // +0x22
    uint32_t           _pad24;   // +0x24
};
static_assert(sizeof(ModSetNode) == 0x28, "node alloc at 0x180010570 asks for 0x28");
static_assert(offsetof(ModSetNode, parent) == 0x08, "0x1800105e8 MOV [RDI+8],RSI");
static_assert(offsetof(ModSetNode, left)   == 0x10, "0x180010535 MOV [RBX+0x10],RAX");
static_assert(offsetof(ModSetNode, right)  == 0x18, "0x18001054e MOV [RBX+0x18],RAX");
static_assert(offsetof(ModSetNode, balance) == 0x20, "0x180027529 MOV [RDX+0x20],CX");
static_assert(offsetof(ModSetNode, height)  == 0x22, "0x180027525 MOV [RDX+0x22],AX");
static_assert(offsetof(ModSetNode, key) == 0x0, "ModSetNode::key");
static_assert(offsetof(ModSetNode, _pad24) == 0x24, "ModSetNode::_pad24");

// (B.4 acceptance MOD: the `ModSet` header POD is gone; Mod+0x60 is the real
// `Collection::Set<System::Filename, FilenameICompare>` below. `ModSetNode`
// stays: it is the erased node the width-8 AVL core (the retrace 0x1800274c0
// and its two rotations, modification_refresh.cpp) is typed on, the same 0x28
// bytes as `ModFilenameSet::Node`, asserted below.)

// 0x18007eefc -- `__ascii_stricmp`, the comparator of the mod path set
// (modification_refresh.cpp).
int ModStrICmp(const char* a, const char* b);

// The `Compare` policy of the mod path set: retail's `_stricmp`, not
// `AVLCompare`'s signed `strcmp`.
struct FilenameICompare {
    static bool Less(const System::Filename& a, const System::Filename& b) {
        return ModStrICmp(a._data, b._data) < 0;
    }
};

}  // namespace RtIO

namespace Collection {
namespace detail {
// `System::Filename`'s null state. Only a POINTER-shaped key has one, and the
// descent treats it asymmetrically -- a null NODE key skips the left test, a
// null SEARCH key descends left and then reports EQUAL. That is retail's, read
// off 0x1800afd50 / 0x1800afd81 (RefreshList's inlined insert), and it is
// exactly the shape `InsertCorePattern` already carries for `char*`. Declared
// here, beside the one Set that keys on a Filename, so every translation unit
// that names the set sees it before anything instantiates it.
template <> struct KeyTraits<System::Filename> {
    static bool IsNull(const System::Filename& k) { return k._data == nullptr; }
};
}  // namespace detail
}  // namespace Collection

namespace RtIO {

// The set itself. `Key == T` is retail's rule (Collection.h's Set banner), the
// element IS the key, and `align8(sizeof(System::Filename)) == 8` selects
// `AvlOps<Node, 0x8>` -- modification_refresh.cpp's three bodies.
using ModFilenameSet = Collection::Set<System::Filename, FilenameICompare>;
static_assert(sizeof(ModFilenameSet) == 0x10, "CopyCtor zeroes [RSI] and [RSI+8] then strides 0x10");
static_assert(sizeof(ModFilenameSet::Node) == sizeof(ModSetNode),
              "Set<Filename>::Node is ModSetNode's 0x28 bytes (node alloc 0x180010570)");
static_assert(offsetof(ModFilenameSet::Node, parent)  == offsetof(ModSetNode, parent),  "parent");
static_assert(offsetof(ModFilenameSet::Node, left)    == offsetof(ModSetNode, left),    "left");
static_assert(offsetof(ModFilenameSet::Node, right)   == offsetof(ModSetNode, right),   "right");
static_assert(offsetof(ModFilenameSet::Node, balance) == offsetof(ModSetNode, balance), "balance @ +0x20");
static_assert(offsetof(ModFilenameSet::Node, height)  == offsetof(ModSetNode, height),  "height @ +0x22");

// One entry of Modification's mod array: `IO::Modification::Mod`
// (`?GetModification@` spells it `UMod@12@`).
//
// B.4 acceptance MOD (`behaviour:`): the real class. Its members are what
// retail constructs and destroys, and its special members are retail's:
//   * Mod(const Mod&)        0x1800101e0 (modification.cpp), statement for
//                            statement in retail's order;
//   * operator=(const Mod&)  0x180026ef0 (modification.cpp);
//   * ~Mod()                 0x180011460 (modification.cpp, out of line as
//                            retail's is): no body; retail releases files
//                            (0x1800098d0), set (0x180010470 and the two zero
//                            stores), file, bytes (0x1800112a0), ints
//                            (0x1800111a0), description, author, name --
//                            reverse declaration order, which is exactly the
//                            member destruction;
//   * Mod()                  RefreshList's stack record (0x1800af790..
//                            0x1800af7d7): every member zeroed in declaration
//                            order EXCEPT +0x24 / +0x28 / +0x2c and the pad at
//                            +0x51..+0x57, which retail leaves unwritten.
// Its arrays are rows of the erased core: `ModList` below
// (`rtcoll::kSpec_Mod`: assign = the copy ctor, destroy = ~Mod).
struct Mod {
    Mod();
    Mod(const Mod& src);
    Mod& operator=(const Mod& src);
    ~Mod();

    System::String name;          // +0x00
    System::String author;        // +0x08
    System::String description;   // +0x10

    int32_t  version;           // +0x18
    int32_t  userVersion;           // +0x1c
    int32_t  order;         // +0x20 — SaveList writes the loop index here
    int32_t  iconWidth;           // +0x24
    int32_t  iconHeight;           // +0x28
    int32_t  crc;           // +0x2c

    Collection::Array<uint32_t> ints;    // +0x30 — op= 0x180011110, ~Array 0x1800111a0 (filled from ?GetIconData@ `?$Array@I`)
    Collection::Array<uint8_t>  bytes;   // +0x40 — op= 0x180011220, ~Array 0x1800112a0

    uint8_t  enabled;       // +0x50 — Enable() writes this byte
    uint8_t  _pad51[7];     // +0x51 — never written

    System::Filename file;  // +0x58 — the mod's .pkm path; Find()'s key

    ModFilenameSet set;     // +0x60 — the package's paths (clone 0x1800104f0, destroy 0x180010470)
    Collection::Array<System::Filename> files;   // +0x70 — op= 0x1800091f0, ~Array 0x1800098d0
};
static_assert(sizeof(Mod) == 0x80,
              "Find() 0x1800aea10: `lVar3 = lVar3 + 0x80` strides the array by this");
static_assert(offsetof(Mod, name)        == 0x00, "CopyCtor 0x180010208 MOV [RCX],RBP");
static_assert(offsetof(Mod, author)      == 0x08, "CopyCtor 0x180010219 MOV [RCX+0x8],RBP");
static_assert(offsetof(Mod, description) == 0x10, "CopyCtor 0x18001022d MOV [RCX+0x10],RBP");
static_assert(offsetof(Mod, version)     == 0x18, "CopyCtor 0x180010244");
static_assert(offsetof(Mod, userVersion)     == 0x1c, "CopyCtor 0x18001024a");
static_assert(offsetof(Mod, order)   == 0x20, "SaveList 0x1800b0654");
static_assert(offsetof(Mod, iconWidth)     == 0x24, "CopyCtor 0x180010256");
static_assert(offsetof(Mod, iconHeight)     == 0x28, "CopyCtor 0x18001025c");
static_assert(offsetof(Mod, crc)     == 0x2c, "CopyCtor 0x180010262");
static_assert(offsetof(Mod, ints)    == 0x30, "CopyCtor 0x180010265 ADD RCX,0x30");
static_assert(offsetof(Mod, bytes)   == 0x40, "CopyCtor 0x180010283 LEA RCX,[RBX+0x40]");
static_assert(offsetof(Mod, enabled) == 0x50, "Enable 0x1800aeabe MOV [RDX+RAX+0x50],R8B");
static_assert(offsetof(Mod, _pad51)  == 0x51, "never written");
static_assert(offsetof(Mod, file)    == 0x58,
              "Find() 0x1800aea10: `*(char**)(*(this+8) + 0x58 + lVar3)`");
static_assert(offsetof(Mod, set)     == 0x60, "CopyCtor 0x1800102ae LEA R14,[RDI+0x60]");
static_assert(offsetof(Mod, files)   == 0x70, "CopyCtor 0x1800102e0 LEA RCX,[RBX+0x70]");

}  // namespace RtIO

namespace rtcoll {
// Array<Mod>'s row (modification.cpp): stride 0x80, align 8 (0x180028676's
// MemAlloc), assign = Mod's copy ctor (0x1800101e0: the CALL in SetSize
// 0x1800286a5 and in operator= 0x180010110), no init arm (SetSize 0x180028610
// is only ever called with a fill), destroy = ~Mod (0x180011460: the
// per-element CALL in ~Array 0x180010b80 and SetSize 0x1800286c7 / 0x180028733).
extern const ArraySpec kSpec_Mod;
}  // namespace rtcoll

namespace RtIO {

// `Collection::Array<IO::Modification::Mod>`: retail's ~Array 0x180010b80,
// operator= 0x180010110 and SetSize 0x180028610 are this class's members over
// the one erased core, driven by `kSpec_Mod`.
using ModList = Collection::Array<Mod, Collection::DefaultAllocator,
                                  Collection::ExplicitSpec<&rtcoll::kSpec_Mod>>;
static_assert(sizeof(ModList) == 0x10, "Collection::Array is 16 bytes");

// The reload queue's element in ?SetupFileSystem@Modification@IO@@ —
// `{uint32 typeId, External*, Filename}`, stride 0x18, built at [RBP-0x71]
// (modification_setupfilesystem.cpp) and copied by the array's assign arm
// 0x180028cd0 (collection/array.cpp kSpec_ModExternal18). THE FOUR BYTES AT
// +0x04 ARE NEVER WRITTEN by anybody (retail's first store is a DWORD and the
// next is at +0x08).
// B.4 Z MIRROR2 (`behaviour:`): `file` is a real System::Filename and the
// entry has retail's stack-pair ctor (0x1800af052..0x1800af0b2: typeId from
// the Type() slot, external, then the Filename copy ctor 0x1800067d0 inlined
// on +0x10, AdjustDelimiters twice; +0x04 left unwritten) and its implicit
// dtor is retail's release of +0x10 right after the push (0x1800af0d5). The
// queue's ELEMENTS are raw storage of the erased core (kSpec_ModExternal18
// copies through the copy ctor and SetupFileSystem's teardown runs
// `file.~Filename()` per element), never built or destroyed as ReloadEntry.
struct ReloadEntry {
    ReloadEntry(uint32_t t, ::Object::External* e, const System::Filename& f)
        : typeId(t), external(e), file(f) {}

    uint32_t             typeId;    // +0x00
    uint32_t             _pad04;   // +0x04 — NEVER WRITTEN
    ::Object::External*  external;  // +0x08
    System::Filename     file;      // +0x10
};
static_assert(offsetof(ReloadEntry, external) == 0x08, "0x1800af058 MOV [RBP-0x69],R14");
static_assert(offsetof(ReloadEntry, file)     == 0x10, "0x1800af064 MOV [RBP-0x61],R13");
static_assert(sizeof(ReloadEntry) == 0x18, "0x180028d2d stride, cap*3*8");
static_assert(offsetof(ReloadEntry, typeId) == 0x0, "ReloadEntry::typeId");
static_assert(offsetof(ReloadEntry, _pad04) == 0x4, "ReloadEntry::_pad04");

// (B.4 Z RT2: the RtIO::Modification POD mirror is gone; IO::Modification below
// is the one layout.)

}  // namespace RtIO

// ============================================================================
// ENDGAME B.3.4 PACKAGE+FS — the REAL `IO::Modification`.
// ============================================================================
//
// Design: B34_RULES.md §1.5 rule 1 (retail name, retail namespace), §4.3 (the
// ctor/dtor door question), §1.5 rule 3 (the PODs stay), B34_BRIEFS §E.4.
//
// *** IT HAS NO VTABLE, AND IT IS A SINGLETON. ***
// ------------------------------------------------
// There is no `??_7Modification@IO@@6B@`: not in the retail export table, not
// in `standalone/rt_data_mint_tables.inc` (the only Modification row there is
// the SINGLETON POINTER `?_singleton@Modification@IO@@2PEAV12@EA` at
// 0x1801fc6b0), and `deleting_dtor.cpp` carries no row for it. B34_RULES §4.2's
// own table names this as the one exception in the sweep: "Modification's dtor
// 0x1800b0470 is a singleton destructor, not a vtable row". Every exported
// member is `QEAA`/`QEBA`.
//
// *** THE CONSTRUCTOR AND DESTRUCTOR DO NOT CONVERT IN THIS STEP. ***
// -------------------------------------------------------------------
// Not because of a vtable — there is none — but because neither door is in
// PACKAGE+FS's file set: `??0Modification@IO@@QEAA@XZ` is registered by
// `src/engine/engine/singleton_ctors.cpp:909` and `??1Modification@IO@@QEAA@XZ`
// by `src/engine/io/iobase_vtables.cpp:579`, both of which stay with the core
// owner. The destructor's ORDER is retail's and is reproduced, not tidied
// (B34_BRIEFS §E.4): it clears the exported singleton — tagged
// `UNPROMOTABLE(HARMFUL)` in `reg_mint_promoted.cpp` — then destroys
// `_savedMods`, then `_mods`. Nothing here changes any of that.
//
// *** `Mod` IS A REAL CLASS (B.4 acceptance MOD). ***
// ---------------------------------------------------
// `IO::Modification::Mod` is retail's nested struct — `?GetModification@` spells
// it `UMod@12@`, `U` for struct, `12` for `Modification::IO`. It is NOT nested
// here (it stays `RtIO::Mod`, the spelling its readers in runtime/engine,
// runtime/save and src/game/Dialog use), but it is the real class: String /
// Filename / Array / Set members with retail's copy ctor, operator= and
// destructor (see its banner above). `ModSetNode` stays the erased node of
// the width-8 AVL core; `ModArrayOf<T>` stays the header of
// FileSystem::_userPackFilenames (B7: the PackageFile icon / preview return
// slots are `Collection::Array<T>` by value).
//
// *** WHAT IS AND IS NOT DECLARED. ***
// ------------------------------------
// Only the members this tree actually ports. `?IsDirty@Modification@IO@@QEBA_NXZ`
// (0x18000daa0) and `?EnableSavingSettings@` (0x1800aead0) are real retail
// members and are deliberately ABSENT: nothing in this tree ports or calls
// them, and an uncalled port is an unverified port. The two unexported sorts
// (0x180028760 / 0x180028fc0, src/engine/io/modification_sort.cpp) are NOT
// members — their retail signatures carry no `this` — so they stay the free
// functions they are.
// ============================================================================

namespace IO {

class Modification {
public:
    // ---- the ported members, each with its retail address -----------------
    // Bodies: src/engine/io/modification.cpp (the first five),
    // modification_refresh.cpp (RefreshList),
    // modification_setupfilesystem.cpp (SetupFileSystem),
    // filesystem.cpp (Find).

    // ?GetCount@Modification@IO@@QEBAHXZ                       0x18000da90
    int GetCount() const;

    // ?GetModification@Modification@IO@@QEBAAEBUMod@12@H@Z     0x18000da80
    // Returns `const Mod&` (`RtIO::Mod`, see the banner). The index is SIGN-extended
    // before the `<< 7`: a negative index strides BACKWARDS, and that is
    // load-bearing, not a decompiler artefact.
    const RtIO::Mod& GetModification(int index) const;

    // ?Enable@Modification@IO@@QEAAXH_N@Z                      0x1800aeab0
    void Enable(int index, bool value);

    // ?Swap@Modification@IO@@QEAAXHH@Z                         0x1800aeb20
    void Swap(int a, int b);

    // ?SaveList@Modification@IO@@QEAAXXZ                       0x1800b05f0
    void SaveList();

    // ?RefreshList@Modification@IO@@QEAAXXZ                    0x1800af5f0
    void RefreshList();

    // ?SetupFileSystem@Modification@IO@@QEAAXXZ                0x1800aebb0
    void SetupFileSystem();

    // ?Find@Modification@IO@@QEBAHAEBVFilename@System@@@Z      0x1800aea10
    int Find(const System::Filename& file) const;

    // ---- the 0x28 bytes, at retail's offsets -------------------------------
    // PUBLIC for the same reason `IO::Serializer`'s are: unconverted bodies
    // across `src/engine/engine` and `src/engine/save` read them by name, and
    // `offsetof` on them has to be well formed for the pins below.
    uint8_t  _dirty;           // +0x00 — IsDirty 0x18000daa0
    uint8_t  _savingSettings;  // +0x01 — EnableSavingSettings 0x1800aead0
    uint8_t  _pad02[6];
    // B.4 acceptance MOD: the two Array<Mod>s are `RtIO::ModList`, retail's
    // Array<Mod> (row kSpec_Mod). IO::Modification is never constructed or
    // destroyed as a C++ object: Engine::Startup MemAllocs it and runs the
    // ctor 0x1800b04b0 (Ctor_Modification, which zeroes the six fields by
    // name), and Engine::Shutdown runs ??1 0x1800b0470 (RtIo_Modification_Dtor:
    // ~Array 0x180010b80 on each, i.e. DestroyAndFree) and then MemFree, so no
    // implicit constructor or destructor of these members ever runs.
    RtIO::ModList _mods;        // +0x08 {data, size +0x10, capacity +0x14}
    RtIO::ModList _savedMods;   // +0x18 {data, size +0x20, capacity +0x24}

    // ?_singleton@Modification@IO@@2PEAV12@EA (0x1801fc6b0) -- the class
    // static (ENDGAME B.3.9 IO, B39_RULES D7; retail's `2` = public). Defined
    // in modification.cpp; RtIoPortInit redirects the minted row onto it, so
    // the name door, the address door, the manifest publish and this member
    // are the same eight bytes. Written by the ??0Modification door
    // (engine/singleton_ctors.cpp) and ??1Modification (iobase_vtables.cpp).
    static Modification* _singleton;

    // ---- ext.menu_entry (NOT RETAIL, docs/extensions/handins/MENUENTRY.md) -------------------
    // Engine-side seams (DESIGN.md hook rule 5), null by default and under the faithful profile.
    //
    // RefreshList, loop 1, per .pkm found: _extModCached fills `out` (a fresh stack Mod whose
    // `file` is set) with the record ReadPackfile and the path-set loop would build, and answers
    // kModRead (nothing cached: read the file), kModFilled (filled; the settings part runs as
    // retail's) or kModSkipped (the file is one retail discards: unreadable or another
    // platform's). _extModStored hands the record a read built (null: the file was discarded).
    // Loop 2: _extOverridesRestore true = every mod's `files` was rebuilt as the loop would;
    // _extOverridesBuilt runs after the loop did run.
    static constexpr int kModRead = 0;
    static constexpr int kModFilled = 1;
    static constexpr int kModSkipped = 2;
    static int (*_extModCached)(const System::Filename& file, RtIO::Mod& out);
    static void (*_extModStored)(const System::Filename& file, const RtIO::Mod* record);
    static bool (*_extOverridesRestore)(Modification& mod);
    static void (*_extOverridesBuilt)(const Modification& mod);
    // SetupFileSystem: _extRemountSkip runs after the `_dirty` test; true = the package state a
    // remount would build is the one mounted now, and acts 1-4 are skipped (the order stamp and
    // `_dirty = 0` still run). _extRemountDone runs at the end of act 5 of a remount that ran,
    // with act 3's counts (queued reloads, ForceUnloads).
    static bool (*_extRemountSkip)(Modification& mod);
    static void (*_extRemountDone)(Modification& mod, int32_t reloads, int32_t unloads);
};

static_assert(sizeof(Modification) == 0x28, "ctor 0x1800b04b0 zeroes +0x08..+0x27");
static_assert(offsetof(Modification, _mods)      == 0x08, "Find()'s `*(longlong*)(this + 8)`, count at +0x10");
static_assert(offsetof(Modification, _savedMods) == 0x18,
              "SaveList 0x1800b0619: ModArrayCopy(this+0x18, this+0x08)");
static_assert(offsetof(Modification, _dirty) == 0x0, "Modification::_dirty");
static_assert(offsetof(Modification, _savingSettings) == 0x1, "Modification::_savingSettings");
static_assert(offsetof(Modification, _pad02) == 0x2, "Modification::_pad02");
// The guard, as an assertion so a future `virtual` fails the build rather than
// the boot: a vptr would push every field eight bytes down under doors that are
// registered BY ADDRESS and under a live singleton five other files read.
static_assert(!std::is_polymorphic<Modification>::value,
              "IO::Modification must never acquire a vtable: retail has none for it");
static_assert(std::is_standard_layout<Modification>::value,
              "the doors reach these fields through a reinterpret_cast");

}  // namespace IO

// ==== B3R W0-P0c: Runtime bodies reached by retail address ====
// ENDGAME B.3R (B3R_RULES R5): each body below is registered at the retail
// address in its trailing comment and read through the registry today. It is
// declared here, copied from its definition (linkage, namespace, types), so the
// reader can call it by name. Nothing calls it through this declaration yet.

namespace RtIo {
// ---- io/iobase_vtables.cpp
extern "C" void RtIo_Modification_Dtor(IO::Modification* self);  // 0x1800b0470
}  // namespace RtIo

namespace RtIO {
// ---- io/modification_sort.cpp
extern "C" void RtIo_Modification_SortMods(Mod* data, int32_t count, void*);  // 0x180028760
// ---- io/modification.cpp
void* RtColl_ArrayFilenameDtor_1800098d0(ModArrayOf<System::Filename>* a);  // 0x1800098d0
void* RtColl_ArrayI32Assign_180011110(Collection::Array<uint32_t>* dst,
                                      const Collection::Array<uint32_t>* src);  // 0x180011110
void* RtColl_ArrayU8Assign_180011220(Collection::Array<uint8_t>* dst,
                                     const Collection::Array<uint8_t>* src);    // 0x180011220
}  // namespace RtIO
// ==== end B3R W0-P0c ====
