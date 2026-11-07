// src/game/Object/factory_registration.cpp — see factory_registration.h.
//
// ENDGAME B.3.9 RF (B39_RULES.md D6): the FactoryRegistration staging list
// (`g_pending`), InvokeAllRegistrations() and the fn_BaseTypeInfo_Ctor door are
// gone. The exe records are real BaseTypeInfo class statics built at CRT init
// (Object/exe_type.h); the helpers below answer through Object::Factory.
#include "factory_registration.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "object/TypeInfo.h"   // ENDGAME B.3.3 step 6C

namespace {

// ---------------------------------------------------------------------------
// The exe-record section. Every DEFINE_EXE_TYPE expansion leaves one
// constant-initialised `BaseTypeInfo*` (its `_exeTypeInfoEntry`) in the input
// section `.data$exetype_b`. The PE linker merges `.data$*` input sections
// into `.data` SORTED BY NAME (`*(SORT(.data$*))`, the default MinGW script),
// so the two markers below bracket every entry: `.data$exetype_a` sorts before
// and `.data$exetype_c` after. The entries are data with an address constant,
// not code: they exist whether or not a dynamic initialiser ran, which is what
// makes them an INDEPENDENT count of the expansions for VerifyExeTypeInfos().
// Alignment padding between input sections, if any, is zero bytes; a null
// entry is skipped.
// ---------------------------------------------------------------------------
#if defined(_WIN32)
__attribute__((used, section(".data$exetype_a")))
::Object::BaseTypeInfo* s_exeTypeInfoBegin = nullptr;
__attribute__((used, section(".data$exetype_c")))
::Object::BaseTypeInfo* s_exeTypeInfoEnd = nullptr;

template <class F>
void WalkExeTypeInfoSection(F&& f) {
    ::Object::BaseTypeInfo** p = &s_exeTypeInfoBegin;
    ::Object::BaseTypeInfo** e = &s_exeTypeInfoEnd;
    // The LINKER relates the two markers, not the language: hide their
    // provenance so the compiler cannot reason about two distinct objects.
    __asm__ volatile("" : "+r"(p), "+r"(e));
    for (++p; p < e; ++p)
        if (*p) f(**p);
}
#else
}  // namespace

// ELF (Phase D.4 LINUX-1, exe_type.h): the entries sit in the section "exe_type_records",
// and the linker defines these two symbols at its first entry and one past its last. Same
// walk, same skip of a null entry; the markers are the linker's, not two variables of ours
// (weak: a link with no record at all -- a unit test -- gets two nulls, an empty walk).
#include "platform/common/platform_exe_type_section.h"

namespace {

template <class F>
void WalkExeTypeInfoSection(F&& f) {
    ::Object::BaseTypeInfo** p = __start_exe_type_records;
    ::Object::BaseTypeInfo** e = __stop_exe_type_records;
    __asm__ volatile("" : "+r"(p), "+r"(e));
    for (; p < e; ++p)
        if (*p) f(**p);
}
#endif

[[noreturn]] void ExeTypeFatal(const char* what, const ::Object::BaseTypeInfo* ti) {
    std::fprintf(stderr, "[FactoryReg] FATAL: %s%s%s\n", what,
                 ti ? " -- record " : "", ti ? (ti->name ? ti->name : "(unconstructed)") : "");
    std::fflush(stderr);
    std::abort();
}

}  // namespace

namespace Object {

void ForEachExeTypeInfo(ExeTypeInfoVisitFn fn, void* ctx) {
    if (!fn) return;
    WalkExeTypeInfoSection([&](const BaseTypeInfo& ti) { fn(ti, ctx); });
}

int VerifyExeTypeInfos() {
    // 1. Every collected record is in the chain: its CRT-init ctor ran.
    int entries = 0;
    WalkExeTypeInfoSection([&](const BaseTypeInfo& ti) {
        ++entries;
        bool linked = false;
        for (const BaseTypeInfo* c = BaseTypeInfo::_firstTypeInfo; c; c = c->next)
            if (c == &ti) { linked = true; break; }
        if (!linked)
            ExeTypeFatal("an exe type record is not in _firstTypeInfo (its static "
                         "initialiser did not run)", &ti);
        // 2. Factory::Startup walked it: the id is the CRC of the name.
        if (!ti.name || ti.typeId != TypeNameCRC(ti.name))
            ExeTypeFatal("an exe type record does not carry ~CRC32(name) after "
                         "Factory::Startup", &ti);
    });
    if (entries == 0)
        ExeTypeFatal("no exe type record in the .data$exetype_b section (the "
                     "linker did not bracket it between the markers)", nullptr);

    // 3. No two records of the whole chain share an id (B39_RULES D6: Startup's
    //    Insert keeps the first of two equal keys without a word).
    for (const BaseTypeInfo* a = BaseTypeInfo::_firstTypeInfo; a; a = a->next)
        for (const BaseTypeInfo* b = a->next; b; b = b->next)
            if (a->typeId == b->typeId)
                ExeTypeFatal("two records in _firstTypeInfo share a type id", b);

    printf("[FactoryReg] Registered %d exe type records in _firstTypeInfo\n", entries);
    return entries;
}

// Abstract base types (DEFINE_EXE_ABSTRACT_TYPE). The exe registers these with
// a _purecall stub (0x140003730) -- they are never instantiated, they only
// exist as parents in the type hierarchy. Moved here from the deleted
// factory_registrations_bulk.cpp (ENDGAME B.3.9 RF); same text, same stream.
Base* AbstractTypeFactory(const char* typeName) {
    std::fprintf(stderr,
        "[AbstractFactory] Unexpected instantiate of abstract type: %s\n", typeName);
    std::fflush(stderr);
    return nullptr;
}

// Before Engine::Startup has built the Factory there is no id to answer: 0, the
// value the old exe list answered before its records existed. This is the
// helper's contract for an early caller, not a guard around a faulting read.
uint32_t GetTypeIdByName(const char* name) {
    if (!name) return 0;
    const Factory* factory = Factory::Get();
    if (!factory) return 0;
    return factory->GetTypeIdFromName(name, /*existingOnly=*/true);
}

// GetTypeName faults on a miss in retail; this diagnostic helper asks
// IsTypeValid first and answers nullptr instead.
const char* GetTypeNameById(uint32_t typeId) {
    if (typeId == 0 || typeId == kInvalidTypeId) return nullptr;
    const Factory* factory = Factory::Get();
    if (!factory || !factory->IsTypeValid(typeId)) return nullptr;
    return factory->GetTypeName(typeId);
}

bool IsTypeDerivedFrom(uint32_t typeId, uint32_t ancestorId) {
    if (typeId == 0 || typeId == kInvalidTypeId || ancestorId == 0 || ancestorId == kInvalidTypeId)
        return false;
    const Factory* factory = Factory::Get();
    if (!factory || !factory->IsTypeValid(typeId) || !factory->IsTypeValid(ancestorId))
        return false;
    return factory->IsDerivedFrom(typeId, ancestorId);
}

}  // namespace Object
