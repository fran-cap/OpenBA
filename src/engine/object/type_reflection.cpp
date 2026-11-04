// ============================================================================
// src/engine/object/type_reflection.cpp — ENDGAME B.3.3 step 3
// ============================================================================
//
// `Object::BaseTypeInfo` and `Object::Factory` as real classes, under the retail
// names, on `Collection::Set`. The layouts, the retail addresses and the naming
// evidence are in src/engine/object/TypeInfo.h; the design rules are
// docs/history/endgame_b/B33_RULES.md §1-§3.
//
// WHY THIS FILE IS SEPARATE FROM type_system.cpp
// ----------------------------------------------
// type_system.cpp owns the registry: the `Rt*` ABI shims, the mangled-name and
// retail-address doors, and `RT_EXPORT_ONCE`. None of that can link into
// `make unit` (tests/unit/rt_port_stub.cpp stubs `ResolveExport`/`ResolveAddr`
// but not `RtResolveCached`). The CLASSES have to be unit-testable — B33_RULES
// §8 asks for the CRC, StripNamespace, the sentinel, the chain order, Startup,
// and a find at two tree depths — so the bodies live here and the doors stay
// there. It is the same split `src/engine/collection/set_core.cpp` has against
// `Collection.h`, one level up.
//
// THE ALLOCATOR SEAM is the only thing this file needs from outside, and it has
// a malloc branch under -DBANISHED_UNIT_TEST exactly as
// src/engine/collection/linkedlist.cpp does.
// ============================================================================

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "object/TypeInfo.h"

#if !defined(BANISHED_UNIT_TEST)
#include "system/MemoryAllocator.h"   // B3R: MemAlloc / MemFree by name
#endif

namespace Object {

// ============================================================================
// The allocator seam
// ============================================================================
//
// Retail INLINES `?MemAlloc@MemoryAllocator@System@@SAPEAX_KH@Z` (0x1800c7a50)
// and `?MemFree@...` (0x1800c7ad0) into Startup rather than calling them:
// 0x1800c07e5..0x1800c0826 is Enter(&_singleton->cs) / Allocate(_singleton,
// bytes, 8) / Leave, which is 0x1800c7a50's whole body with align=8, and
// 0x1800c0873..0x1800c08a6 is 0x1800c7ad0's body with retail's `if (p)` hoisted
// to 0x1800c086e. So calling the two wrappers is the same instruction sequence,
// and it goes through the one allocator door instead of pinning 0x1800c6e60 /
// 0x1800c6c00 / the +0x640 lock offset in a third place. (This paragraph moved
// here verbatim from type_system.cpp's "allocator boundary" banner, which owned
// these two calls before the class did.)
//
// The Set nodes and the child arrays are split into two pairs only so a leak
// report can tell them apart; both land on the same door.
#if defined(BANISHED_UNIT_TEST)

// The host has no MemoryAllocator singleton and no registry. malloc is the
// whole seam, and `align` is ignored because malloc already returns storage
// aligned for any scalar — the Set asks for 8 and gets at least 16.
void* TypeTreeAlloc(uint64_t bytes, int /*align*/) { return std::malloc((size_t)bytes); }
void  TypeTreeFree(void* p)                        { std::free(p); }
void* TypeChildAlloc(uint64_t bytes, int /*align*/) { return std::malloc((size_t)bytes); }
void  TypeChildFree(void* p)                        { std::free(p); }

#else

void* TypeTreeAlloc(uint64_t bytes, int align) {
    return System::MemoryAllocator::MemAlloc(bytes, align);   // 0x1800c7a50
}
void TypeTreeFree(void* p) {
    // The `if (p)` lives INSIDE 0x1800c7ad0 (`TEST RCX,RCX; JZ ret` is its
    // first instruction). The call sites still test, because retail does too,
    // at 0x1800c086e.
    System::MemoryAllocator::MemFree(p);                      // 0x1800c7ad0
}
void* TypeChildAlloc(uint64_t bytes, int align) { return TypeTreeAlloc(bytes, align); }
void  TypeChildFree(void* p)                    { TypeTreeFree(p); }

#endif

// ?Type@Base@Object@@UEBAIXZ   retail 0x1800c0380 — `XOR EAX,EAX; RET`.
//
// An unregistered type answers 0, which is the SAME value
// `Factory::GetTypeIdFromName` returns for a name that is not in the tree.
// That collision is retail's and is relied on: `WriteComponentTypeId` treats 0
// as "no component".
uint32_t Base::Type() const { return 0u; }

// ?IsDerivedFrom@Base@Object@@QEBA_NI@Z   retail 0x1800c0390
//
// ORDER MATTERS AND IS PRESERVED: retail loads `_singleton` into RDI BEFORE
// making the virtual call (0x1800c039a-0x1800c03a6). Harmless today, kept
// because it is what the disassembly says. The unguarded find inside
// `Factory::IsDerivedFrom` is retail's too — a missing registration faults
// rather than answering false.
bool Base::IsDerivedFrom(uint32_t typeId) const {
    const Factory* factory = Factory::Get();
    const uint32_t ownId   = Type();
    return factory->IsDerivedFrom(ownId, typeId);
}

// ============================================================================
// TypeNameCRC — the type-id hash, as ONE function
// ============================================================================
//
// Retail's ?GetTypeFromNameCRC@Factory@Object@@AEBAIPEBD@Z (0x1800c0930)
// widens the ASCII name to UTF-16LE on the stack and CRC-32s the resulting
// BYTES, so the id of "Base" is the CRC of "B\0a\0s\0e\0". The table it uses is
// System::CRC::_crcTable (exported at 0x1801f9080). All 256 entries of that
// table were compared against the textbook reflected-CRC-32 table
// (polynomial 0xEDB88320) and are IDENTICAL, so we generate it rather than
// reading the DLL's — PLAN.md §4.1 decision 1 wants ported code to survive
// cutover, and a table we can regenerate does.
//
//     python: t[i] = fold8(i, 0xEDB88320);  compared byte-for-byte with
//     pefile.get_data(0x1f9080, 1024)  ->  identical: True
//
// Moved here from type_system.cpp's anonymous namespace by B.3.3 step 3 so the
// class, the registry shim and the mint all call the SAME body. The one thing
// that must NOT happen is a second, independently written copy of a CRC that
// agrees on today's names and diverges on tomorrow's.
namespace {

uint32_t g_crcTable[256];
bool     g_crcTableBuilt = false;

void BuildCrcTable() {
    if (g_crcTableBuilt) return;
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1u) ? ((c >> 1) ^ 0xEDB88320u) : (c >> 1);
        g_crcTable[i] = c;
    }
    g_crcTableBuilt = true;
}

}  // namespace

// Faithful transcription of GetTypeFromNameCRC's body. The 264-byte stack
// buffer is retail's own (132 wide characters); a longer type name overflows
// it in retail exactly as it does here, and no bound check is added because
// adding one would change behaviour retail relies on never needing.
uint32_t TypeNameCRC(const char* name) {
    BuildCrcTable();

    uint8_t wide[264];
    int len = 0;
    for (const char* p = name; *p; ++p) ++len;
    for (int i = 0; i < len; ++i) {
        // (short)param_1[i] in the decompilation: MSVC's `char` is signed, so
        // this sign-extends. Irrelevant for the ASCII type names the engine
        // registers, spelled out so nobody has to rediscover it.
        const int16_t w = static_cast<int16_t>(static_cast<signed char>(name[i]));
        wide[i * 2 + 0] = static_cast<uint8_t>(static_cast<uint16_t>(w) & 0xFFu);
        wide[i * 2 + 1] = static_cast<uint8_t>((static_cast<uint16_t>(w) >> 8) & 0xFFu);
    }

    uint32_t crc = 0xFFFFFFFFu;
    const int nbytes = len * 2;
    for (int i = 0; i < nbytes; ++i)
        crc = g_crcTable[(crc & 0xFFu) ^ wide[i]] ^ (crc >> 8);
    return ~crc;   // len == 0 leaves crc at 0xFFFFFFFF, so the id is 0
}

// ============================================================================
// Object::BaseTypeInfo
// ============================================================================
//
// ?_firstTypeInfo@BaseTypeInfo@Object@@2PEAV12@EA — retail 0x1801fc6e0.
//
// A PLAIN STATIC, and it has to be one: the constructor below runs at CRT-init,
// where the port registry does not exist yet, so it cannot RESOLVE a cell — it
// must own one. Zero-initialised before any dynamic initialiser runs, which is
// what makes static-init order across translation units a non-issue and is the
// whole reason the exe-side staging list in src/game/Object/factory_registration.h
// existed (B33_RULES §2). standalone/rt_data_mint.cpp routes the minted row's
// two doors (name and address 0x1801fc6e0) here, so there is exactly ONE cell.
BaseTypeInfo* BaseTypeInfo::_firstTypeInfo = nullptr;

// ?_typeInfo@Base@Object@@0VBaseTypeInfo@2@A — retail 0x1801f9c50. ENDGAME
// B.3.9 OBJ (B39_RULES D6 W3): the ROOT record, exactly as minted -- name
// "Base", parent null (StripNamespace leaves a null parent alone), no
// extension, no factory pair. Head-inserted at CRT init by the ported ctor;
// defined after `_firstTypeInfo` so the one TU's order is plain to read (the
// list head is constant-initialised either way). The minted record is retired
// in the same commit.
BaseTypeInfo Base::_typeInfo("Base", nullptr, nullptr, nullptr, nullptr);

// ============================================================================
// ??0BaseTypeInfo@Object@@QEAA@PEBD00P6APEAVBase@1@XZP6APEAV21@PEAX@Z@Z
//                                        retail 0x180006320 / profile 0x1800234e0
//
// Stores the five arguments, stamps typeId = 0xFFFFFFFF, head-inserts into
// `_firstTypeInfo`, then rewrites `parentName` to point past the LAST ':' it
// contains (so "Object::Base" becomes "Base"). Only the parent is stripped —
// which is why Startup's parent lookup (the CRC of a stripped parent) can match
// a child's unstripped `name`.
//
// The store ORDER is retail's, not tidied: name, parentName, factoryFn, the
// 0xFFFFFFFF sentinel, fileExtension, inPlaceFactoryFn, next, then the global.
// ============================================================================
BaseTypeInfo::BaseTypeInfo(const char* nameIn, const char* parentNameIn,
                           const char* fileExtensionIn,
                           FactoryFn factory, InPlaceFn inPlace) {
    BaseTypeInfo* const prevHead = _firstTypeInfo;

    name             = nameIn;
    parentName       = parentNameIn;
    factoryFn        = factory;
    typeId           = 0xFFFFFFFFu;
    _pad04           = 0;
    fileExtension    = fileExtensionIn;
    inPlaceFactoryFn = inPlace;
    next             = prevHead;
    _firstTypeInfo   = this;

    StripNamespace();
}

// ============================================================================
// ?StripNamespace@BaseTypeInfo@Object@@QEAAXXZ
//
// Retail's backward scan, transcribed exactly (0x180006381-0x18000639b): it
// starts at the NUL terminator, walks down while `q > p`, and when the loop
// falls out at `q == p` it tests that byte once more before giving up. An empty
// parent string therefore still gets its single (NUL) byte tested.
// ============================================================================
void BaseTypeInfo::StripNamespace() {
    if (!parentName) return;

    const char* p = parentName;
    int len = 0;
    for (const char* s = p; *s; ++s) ++len;
    const char* q = p + len;
    while (q > p) {
        if (*q == ':') { ++q; parentName = q; return; }
        --q;
    }
    if (*q == ':') ++q;
    parentName = q;
}

// ============================================================================
// Object::Factory
// ============================================================================

// ?_singleton@Factory@Object@@0PEAV12@EA — retail 0x1801fc6e8. The class
// static (ENDGAME B.3.9 OBJ, B39_RULES D7); the minted row is redirected onto
// it in RtObjectPortInit, so the name and address doors answer these bytes.
// The mint never zeroes or re-seeds it; the language zero-initialises it and
// the ctor door publishes into it. (The host has no singleton: in `make unit`
// it stays null, which is what Get() answered there before.)
Factory* Factory::_singleton = nullptr;

// ?Get@Factory@Object@@SAPEAV12@XZ — `MOV RAX,[0x1801fc6e8]; RET`.
Factory* Factory::Get() {
    return _singleton;
}

uint32_t Factory::GetTypeFromNameCRC(const char* name) const {
    // `this` IS READ BY NOTHING. Retail's RCX is dead from the first
    // instruction (the body opens `LEA RAX,[RSP+0x...]` and never touches RCX
    // again); the CRC depends only on the name. The parameter is kept because
    // the mangling is a const member and the caller passes it.
    return TypeNameCRC(name);
}

// ============================================================================
// ?Startup@Factory@Object@@QEAA_NXZ      retail 0x1800c0630  ord 4096
//
// STEP 6 OF Engine::Startup's QUEUE, and the reflection system's own bring-up:
// it is the ONLY code that overwrites `BaseTypeInfo::typeId`'s 0xFFFFFFFF ctor
// sentinel, and the only code that builds the AVL tree every later lookup walks
// (CreateObject, GetTypeIdFromName, IsDerivedFrom, GetTypeName,
// GetTypeIdForFileExtension). Called once per process.
//
// TWO PASSES over 212 instructions:
//
//   1. Walk `_firstTypeInfo`. For each entry, CRC-32 its `name` into `typeId`,
//      then insert {typeId, info, nullptr, {}} into the tree keyed by typeId.
//      The CRC is the SAME algorithm as Factory::GetTypeFromNameCRC — retail
//      has it twice, once inline here and once out of line there — so this
//      reuses TypeNameCRC above rather than transcribing it a second time.
//
//   2. In-order walk of the tree just built. For each node whose `info` has a
//      `parentName`, CRC that name, look the parent's node up, store
//      `&parentNode->value` as this record's `parent`, and push
//      `&thisNode->value` onto the parent's `children` array.
//
// THE INSERT IS NOW `Collection::Set<TypeEntry,…>::Insert`, not the by-address
// call to 0x180030140 this body used to make. It is the SAME CODE: `Set::Insert`
// runs `detail::InsertCorePattern<Policy>` and `AvlOps<Node, 0x30>::Retrace`,
// which is `PortedRetrace48` — the very bodies `rtavl::TypeSetInsert` used to
// call. What is gone is the wrapper that built the 0x30-byte Pair on the stack,
// copy-assigned its `Array` through 0x180009350 and destroyed it through
// 0x1800093e0; with `children` `{nullptr, 0, 0}` on every insert Startup makes,
// that copy-assign and that destructor are both no-ops on a zeroed Array, so
// removing them removes no observable effect. `TypeSetInsert` and its
// 0x180030140 registration are DELETED (B.3.3 step 7): the sweep found no
// reader of that address by name, by wrapper, in the mint tables or in
// rt_export_addrs.inc.
//
// TWO UNGUARDED DEREFS THAT ARE LOAD-BEARING. Both are kept (CLAUDE.md, "Don't
// 'fix' a faithful crash with a null check"):
//
//   * When a `parentName` names a type that is NOT registered, retail's tree
//     search leaves RCX == 0 and `LEA RBP,[RCX+8]` (0x1800c079d) produces
//     `(TypeRecord*)8`, which the very next instruction dereferences at +0x20 —
//     a read of address 0x28. That fault IS how a missing parent registration
//     announces itself, and it is a fault we want, in our own code, with a
//     symbol name on it.
//   * `info->name` is dereferenced by the CRC with no null test.
//
// RETURNS `true` UNCONDITIONALLY — `MOV AL,1` at 0x1800c0907 is the only exit.
// ============================================================================
bool Factory::Startup() {
    // ---- Pass 1 ------------------------------------------------------------
    //
    // `_firstTypeInfo` is read ONCE into RBX at 0x1800c065b and then followed
    // through `info->next`; the loop does not re-read the global, so a
    // BaseTypeInfo constructed DURING Startup would not be seen. Nothing
    // constructs one during Startup, but the port matches the read anyway.
    for (BaseTypeInfo* info = BaseTypeInfo::_firstTypeInfo; info; info = info->next) {
        info->typeId = TypeNameCRC(info->name);

        TypeEntry entry;
        // Retail stamps 0xFFFFFFFF into rec.typeId at 0x1800c06e3 and then
        // overwrites it with info->typeId at 0x1800c0706 — the sentinel is
        // dead. `_pad04` is written by NEITHER retail nor the insert wrapper,
        // so it is unobservable; it is zeroed here only so this struct has no
        // indeterminate member.
        entry.typeId               = info->typeId;
        entry._pad04               = 0;
        entry.record.typeId        = info->typeId;
        entry.record._pad04        = 0;
        entry.record.info          = info;
        entry.record.parent        = nullptr;
        // `entry.record.children` is zeroed by its ctor (the three zero stores
        // retail writes at 0x1800c06e3..0x1800c06fa); its ~Array at the end of
        // the iteration is retail's two bare zero stores 0x1800c0728 /
        // 0x1800c072d -- at -O2 GCC keeps no stack copy at all (the insert is
        // inline and stores straight into the node), so no free is emitted.

        // R8 and R9 are the SAME address at 0x1800c070f/0x1800c070a: the key IS
        // the record's first field. `Collection::Set` says the same thing by
        // making `Key` the element's leading sub-object.
        _types.Insert(entry);
    }

    // ---- Pass 2: link parents, fill children -------------------------------
    for (TypeSet::Iterator it = _types.Begin(); it != _types.End(); ++it) {
        TypeSet::Node* const node = it.GetNode();
        BaseTypeInfo* const info = node->element.record.info;
        const char* const parentName = info->parentName;
        if (!parentName) continue;                    // 0x1800c076b

        const uint32_t parentId = TypeNameCRC(parentName);

        // 0x1800c0779..0x1800c079a — the same descend `Find` makes.
        TypeSet::Node* const p = _types.Find(parentId);

        // FAITHFUL: `LEA RBP,[RCX+8]` on a null RCX. A parentName naming an
        // unregistered type yields (TypeRecord*)8 and faults on the next line,
        // exactly as retail does. The +8 IS `&p->element.record` (`TypeEntry`'s
        // key is 8 bytes wide and the record follows it), and that is how it is
        // spelled since B.4 P2: GCC lowers the member address to the same
        // `p + 8` add on a null `p` (the B11 object comparison at -O0 and -O2
        // checked that this body's code did not change).
        //
        // NOT A GUARD. The deref below is unchanged and still faults; this only
        // puts a NAME on the fault before it happens.
        if (!p) {
            // UNQUALIFIED `printf`: src/app/log_strip.h does
            // `#define printf(...) ((void)0)` and the preprocessor cannot
            // replace a qualified name (Makefile's check-log-strip gate).
            printf("[RtPort] Factory::Startup: type \"%s\" (id 0x%08X) names "
                        "parent \"%s\" (id 0x%08X), which is NOT in the type "
                        "chain. The faithful LEA below yields (TypeRecord*)8 "
                        "and the next read faults at 0x28 -- as retail does.\n",
                        info->name ? info->name : "(null)", node->element.record.typeId,
                        parentName, parentId);
            std::fflush(stdout);
        }

        TypeRecord* const parentRec = &p->element.record;
        node->element.record.parent = parentRec;

        // ---- Array<TypeRecord*>::SetSize(count + 1, &node->value) ----------
        //
        // Pass 2's tail is `Collection::Array<TypeRecord*>::SetSize` INLINED —
        // instruction for instruction the J3 body in
        // src/engine/collection/array.cpp (the exact-equal no-op, the
        // `capacity = n*3` rule that fires only on a growth after the first,
        // the `if (dst == 0) continue`, the copy-then-free). It is transcribed
        // here rather than routed through `Collection::Array<T>::SetSize`
        // because retail inlined it: this call site has no `ArraySpec`, no
        // registered address, and a stride and align baked in as immediates.
        const int64_t oldSize = parentRec->children._size;             // MOVSXD
        const int32_t newSize = static_cast<int32_t>(oldSize) + 1;
        if (newSize == static_cast<int32_t>(oldSize)) continue;    // 0x1800c07b0

        TypeRecord** const old = parentRec->children._data;
        parentRec->children._size = newSize;
        if (newSize > parentRec->children._capacity) {                  // SIGNED
            parentRec->children._capacity = newSize;
            // The growth rule: triple ONLY when this is a one-element growth of
            // a non-empty array, i.e. never on the first push. A type with two
            // or more children is what exercises it.
            if (newSize == static_cast<int32_t>(oldSize) + 1 && oldSize != 0)
                parentRec->children._capacity = newSize * 3;            // 32-bit LEA
            parentRec->children._data = static_cast<TypeRecord**>(TypeChildAlloc(  // LEFT: TypeChildAlloc (TypeInfo.h) is the inlined Array<TypeRecord*> growth allocator (void*); an allocation result by meaning, not by the tier-I name shape
                static_cast<uint64_t>(
                    static_cast<int64_t>(parentRec->children._capacity)) * 8, 8));
        }

        // 0x1800c082c: start at oldSize when the buffer did NOT move (append
        // only), at 0 when it did (copy then append).
        for (int64_t i = (old == parentRec->children._data) ? oldSize : 0;
             i < newSize; ++i) {
            TypeRecord** const dst = parentRec->children._data + i;
            if (!dst) continue;      // retail's TEST RDX,RDX at 0x1800c0849
            *dst = (i < oldSize) ? old[i] : &node->element.record;
        }

        if (old != parentRec->children._data && old)                     // 0x1800c086e
            TypeChildFree(old);
    }

    return true;   // MOV AL,1 at 0x1800c0907 is Startup's only exit
}

// ============================================================================
// The lookups. Every one of them is the SAME descent — `Set::Find` — followed
// by retail's unguarded read of the found node's record.
// ============================================================================

// ?GetTypeIdFromName@Factory@Object@@QEBAIPEBD_N@Z   retail 0x1800c0580
//
// `existingOnly` IS UNUSED IN RETAIL. The retail disassembly never touches R8
// (0x1800c0580..0x1800c05b6). The profile build shows why: it guards an assert,
//     "!existingOnly || IsTypeValid(type)"  "Type is not registered!"
//     Object\Factory.cpp:0x59
// which retail compiles out. Every one of our call sites passes `true`. The
// parameter is accepted and ignored, deliberately, and this comment is the
// reason it is not "cleaned up".
uint32_t Factory::GetTypeIdFromName(const char* name, bool existingOnly) const {
    (void)existingOnly;
    const uint32_t crc = TypeNameCRC(name);
    return _types.Find(crc) ? crc : 0u;
}

// ?GetTypeName@Factory@Object@@QEBAPEBDI@Z   retail 0x1800c0a60
// Returns `FindNode(id)->value.info->name`, unguarded.
const char* Factory::GetTypeName(uint32_t typeId) const {
    const TypeSet::Node* n = _types.Find(typeId);
    const TypeRecord* rec = &n->element.record;
    return rec->info->name;
}

// ?IsTypeValid@Factory@Object@@QEBA_NI@Z — the one lookup that is ALLOWED to
// answer "no": it is the membership test the profile build's asserts call.
bool Factory::IsTypeValid(uint32_t typeId) const {
    return _types.Find(typeId) != nullptr;
}

// ?IsDerivedFrom@Factory@Object@@QEBA_NII@Z
//
//     if (typeId == baseTypeId) return true;
//     record = FindNode(typeId)->value;          // faults on a miss
//     while (record->typeId != baseTypeId) {
//         record = record->parent;
//         if (!record) return false;
//     }
//     return true;
//
// The base chain is the `parent` links Startup filled in its second pass, NOT
// the `_firstTypeInfo` list.
bool Factory::IsDerivedFrom(uint32_t typeId, uint32_t baseTypeId) const {
    if (typeId == baseTypeId) return true;

    // Retail's `ADD RAX,0x8` happens on the found pointer WHETHER OR NOT it is
    // null, so a missing registration reads address 0x8 and faults.
    const TypeSet::Node* n = _types.Find(typeId);
    const TypeRecord* rec = &n->element.record;
    for (;;) {
        if (rec->typeId == baseTypeId) return true;
        rec = rec->parent;
        if (!rec) return false;
    }
}

// ?GetParentType@Factory@Object@@QEBAII@Z — 0 at a root, which is the same
// value an unregistered name yields from GetTypeIdFromName.
uint32_t Factory::GetParentType(uint32_t typeId) const {
    const TypeSet::Node* n = _types.Find(typeId);
    const TypeRecord* rec = &n->element.record;
    return rec->parent ? rec->parent->typeId : 0u;
}

// ?GetChildTypeCount@Factory@Object@@QEBAHI@Z
int32_t Factory::GetChildTypeCount(uint32_t typeId) const {
    const TypeSet::Node* n = _types.Find(typeId);
    const TypeRecord* rec = &n->element.record;
    return rec->children._size;
}

// ?GetChildType@Factory@Object@@QEBAIIH@Z — the index is NOT bounds-checked in
// retail either; the Array read is raw.
uint32_t Factory::GetChildType(uint32_t typeId, int32_t index) const {
    const TypeSet::Node* n = _types.Find(typeId);
    const TypeRecord* rec = &n->element.record;
    return rec->children._data[index]->typeId;
}

// ?GetTypeIdForFileExtension@Factory@Object@@QEBAIPEBD@Z   retail 0x1800c04c0
//
// An in-order walk comparing `info->fileExtension` with `_stricmp` — the +0x18
// field, which is a FILE EXTENSION and not an "abstract" flag. Types that
// register a null extension (every Component/Description in the exe) are
// skipped; the ones that pass a real string are the Externals loaded from disk.
uint32_t Factory::GetTypeIdForFileExtension(const char* extension) const {
    if (!extension) return 0u;
    for (TypeSet::Iterator it = _types.Begin(); it != _types.End(); ++it) {
        const BaseTypeInfo* info = it.GetNode()->element.record.info;
        const char* ext = info ? info->fileExtension : nullptr;
        if (!ext) continue;
        // _stricmp, spelled out rather than pulled from <cstring>'s
        // platform-specific spelling: the comparison is ASCII-only and the
        // engine's extensions are all lowercase literals.
        const char* a = ext;
        const char* b = extension;
        for (;;) {
            unsigned char ca = (unsigned char)*a++;
            unsigned char cb = (unsigned char)*b++;
            if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca - 'A' + 'a');
            if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb - 'A' + 'a');
            if (ca != cb) break;
            if (ca == 0)  return it.GetNode()->element.record.typeId;
        }
    }
    return 0u;
}

// ?CreateObject@Factory@Object@@QEBAPEAVBase@2@I@Z       retail 0x1800c0ae0
// ?CreateObject@Factory@Object@@QEBAPEAVBase@2@IPEAX@Z   retail 0x1800c0aa0
//
// A tail-jump through BaseTypeInfo+0x20 (+0x28 for the in-place overload).
// Retail's `MOV RCX,[RAX+0x10]` at 0x1800c0b03 runs on a null RAX when the id is
// not registered (the descent's miss arm, 0x1800c0b01 `XOR EAX,EAX`).
Base* Factory::CreateObject(uint32_t typeId) const {
    const TypeSet::Node* n = _types.Find(typeId);
    // NOT RETAIL (review F-144): retail has no such check. An id that names no
    // registered type (callers pass ids read from resources and saves) leaves `n`
    // null, and `&n->element.record` is a null member access (undefined
    // behaviour); retail then faults reading address 0x10. The miss creates
    // nothing. Every registered id takes retail's path unchanged.
    if (n == nullptr)
        return nullptr;
    const TypeRecord* rec = &n->element.record;
    return rec->info->factoryFn();
}

Base* Factory::CreateObject(uint32_t typeId, void* storage) const {
    const TypeSet::Node* n = _types.Find(typeId);
    // NOT RETAIL (review F-144): the same miss as above, retail 0x1800c0aa0.
    if (n == nullptr)
        return nullptr;
    const TypeRecord* rec = &n->element.record;
    return rec->info->inPlaceFactoryFn(storage);
}

}  // namespace Object
