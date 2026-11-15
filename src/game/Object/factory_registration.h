#pragma once
// ============================================================================
// src/game/Object/factory_registration.h — the exe's type records and the
// by-name / by-id helpers over Object::Factory.
//
// ENDGAME B.3.9 RF (B39_RULES.md D6): the FactoryRegistration staging list,
// InvokeAllRegistrations() and REGISTER_TYPE are gone. Every exe class's
// reflection record is a real `Object::BaseTypeInfo` class static
// (DEFINE_EXE_TYPE, Object/exe_type.h) that CRT init links into
// `BaseTypeInfo::_firstTypeInfo`; Engine::Startup -> Object::Factory::Startup
// then assigns each typeId = ~CRC32(name) and builds the id-keyed tree that
// every lookup below walks.
//
// Lookup mechanism (Object::External::Load and friends): typeId is read from
// the .rsc payload, then the tree at Object::Factory::_singleton is searched
// by id. The matching node holds a pointer to the class's BaseTypeInfo; its
// +0x20 slot is the class's CreateNewObject.
// ============================================================================

#include <cstdint>

namespace Object {

class BaseTypeInfo;
class Base;

// The factory of an ABSTRACT exe base (DEFINE_EXE_ABSTRACT_TYPE, exe_type.h):
// retail's is a `_purecall` stub; ours logs the name to stderr and answers null.
Base* AbstractTypeFactory(const char* typeName);

// The exe records (one per DEFINE_EXE_TYPE expansion), as the linker collected
// them in `.data$exetype_b`. Walks the section, not the chain, so it is valid
// from CRT init on; `typeId` is the CRC only after Object::Factory::Startup.
// Diagnostics only; nothing in the tree calls it.
typedef void (*ExeTypeInfoVisitFn)(const BaseTypeInfo& info, void* ctx);
void ForEachExeTypeInfo(ExeTypeInfoVisitFn fn, void* ctx);

// THE COUNT PROOF (B39_RULES D6 / RF item 1c). Call once, after
// Object::Factory::Startup. Every exe record the linker collected must be in
// `_firstTypeInfo` (its CRT-init constructor ran and linked it), carry
// typeId == ~CRC32(name), and every id in the whole chain must be unique
// (Factory::Startup's Insert silently keeps the first of two equal ids). Any
// failure is FATAL: stderr + abort(), in every build (not a printf, which the
// perf build strips). Returns the number of exe records.
int VerifyExeTypeInfos();

// A registered type's id by NAME: Object::Factory::GetTypeIdFromName (retail
// 0x1800c0580 — CRC the name, answer it iff the tree holds it). 0 for a name
// the tree does not hold, and 0 before Engine::Startup has built the Factory.
uint32_t GetTypeIdByName(const char* name);

// Reverse of the above: the registered type NAME for a type id, or nullptr if
// the tree does not hold it (or for 0 / 0xFFFFFFFF). Diagnostics only — a raw
// type id in a log ("component tid=0x9d9af271") is unreadable, and this
// codebase has repeatedly mis-labelled a DAT_ type-id global in comments.
const char* GetTypeNameById(uint32_t typeId);

// Type-derivation test over the Factory tree (Factory::IsDerivedFrom's parent
// links, which Startup's second pass built): true when the type `typeId` is
// `ancestorId` or derives from it. false for 0 / 0xFFFFFFFF and for an id the
// tree does not hold (where Factory::IsDerivedFrom itself would fault). Nothing
// in the tree calls it.
bool IsTypeDerivedFrom(uint32_t typeId, uint32_t ancestorId);

}  // namespace Object
