// ============================================================================
// src/platform/common/platform_exe_type_section.h -- how the exe-type record section is
// spelled on a non-Windows target (src/game/Object/exe_type.h, factory_registration.cpp).
//
// The records' slots go to one section and the walk reads it between two markers. On ELF the
// linker defines __start_<section> / __stop_<section> for a section whose name is a C
// identifier. Mach-O has no such symbols: the linker answers section$start$<seg>$<sect> and
// section$end$<seg>$<sect> labels instead, and a section name needs its segment ("__DATA,x").
// This header is the one place that difference is spelled, so game code carries no vendor #if.
// ============================================================================
#pragma once

namespace Object {
struct BaseTypeInfo;
}

// PLATFORM_EXE_TYPE_KEEP: what keeps a record's slot through the link besides `used`. On ELF
// `used` only holds the slot through the compiler (clang: llvm.compiler.used); with ThinLTO
// (the Android release library, P12) nothing references the section's input sections and the
// NDK's --gc-sections drops every slot, leaving __start / __stop with nothing between them.
// `retain` marks the section SHF_GNU_RETAIN, which gc-sections keeps. Mach-O's `used` is
// already no_dead_strip.
#if defined(__APPLE__)
#define PLATFORM_EXE_TYPE_KEEP
#define PLATFORM_EXE_TYPE_SECTION "__DATA,exe_type_records"
extern "C" ::Object::BaseTypeInfo* __start_exe_type_records[] __asm("section$start$__DATA$exe_type_records");
extern "C" ::Object::BaseTypeInfo* __stop_exe_type_records[] __asm("section$end$__DATA$exe_type_records");
#else
#if defined(__has_attribute) && __has_attribute(retain)
#define PLATFORM_EXE_TYPE_KEEP retain,
#else
#define PLATFORM_EXE_TYPE_KEEP
#endif
#define PLATFORM_EXE_TYPE_SECTION "exe_type_records"
// Weak: a link with no record at all (a unit test) gets two nulls, an empty walk.
extern "C" __attribute__((weak)) ::Object::BaseTypeInfo* __start_exe_type_records[];
extern "C" __attribute__((weak)) ::Object::BaseTypeInfo* __stop_exe_type_records[];
#endif
