// ============================================================================
// src/engine/engine/updateloop_loadresources.cpp — Wave 23 lane ENGINE
// ?LoadResources@UpdateLoop@Engine@@QEAAXXZ   retail 0x1800dd7d0, ord 3210
// ============================================================================
//
// WHY THIS BODY IS WORTH MORE THAN ONE CENSUS ROW. Wave 22 lane ERRCFG found
// that this function plus one eight-byte accessor are the COMPLETE set of
// still-unported functions that READ the exported static
// `?_singleton@Configuration@System@@0PEAV12@EA` (0x1801fc6f8). Lane ENGINE
// RE-DERIVED that claim rather than inheriting it — all 8 xrefs walked, READ
// split from WRITE at the instruction, plus a raw-file scan of the DLL for the
// RVA and the VA to close the "computed address / second alias" gap ERRCFG had
// left open. The claim holds exactly as written. Evidence:
// docs/history/runtime_port/WAVE23_LANE_ENGINE.md §2.
//
// The reader set was:
//     0x1800abc8b  FileSystem::Startup            READ   ported + registered
//     0x1800abd2e  FileSystem::SetupDirectories   READ   ported + registered
//     0x1800ddbe0  UpdateLoop::LoadResources      READ   <- this file
//     0x18000da70  Configuration::Get             READ   8 bytes, uncalled
//     0x1800c51e0  Configuration::Configuration   WRITE  ported
//     0x1800db954  Engine::Startup                WRITE  ported
//
// So this file removes one of the two remaining readers, and it reads the
// static by name, as `System::Configuration::_singleton`.
//
// ---------------------------------------------------------------------------
// THE ARGUMENT GHIDRA DROPS, AND WHY IT MATTERS HERE
// ---------------------------------------------------------------------------
// Ghidra's C prints the command-line lookup as
//     GetCommandLineString(Configuration::_singleton, &ret);
// TWO arguments. The disassembly at 0x1800ddbd8 is
//     LEA R8,[RBP+0x48]     ; <- the KEY string, dropped by the decompiler
//     LEA RDX,[RBP+0x60]    ; <- the hidden return slot
//     MOV RCX,[0x1801fc6f8]
//     CALL 0x1800c5300
// THREE. A typedef that omits either the return slot or the key is a wild
// write that still returns a plausible answer. Both are spelled out below.
//
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>

#include "engine/UpdateLoop.h"
#include "system/String.h"
#include "system/Filename.h"
#include "system/Configuration.h"   // ENDGAME B.3.9 SS1: Configuration::_singleton
#include "object/External.h"        // ENDGAME B.3.9 SS1: ExternalFactory::_singleton
#include "io/FileSystem.h"          // ENDGAME B.3.9 SS1: FileSystem::_singleton
#include "io/Modification.h"        // ENDGAME B.3.9 SS1: Modification::_singleton
#include "system/string_internal.h"
#include "system/widestring_internal.h"
#include "system/WideString.h"
#include "../object/external_compiled.h"   // B3R: FindOrLoad / GetCompiledPath by name
#include "object/TypeInfo.h"   // B3R: Object::Base::IsDerivedFrom
#include <new>

// ENDGAME B.3.1 step 2: `?Format@String@System@@SA?AV12@PEBDZZ` (0x1800c9730)
// is a static member of the real class now, so the local variadic declaration
// this file used to carry for the `RtSystem_String_Format` shim is gone.


// src/engine/engine/updateloop_path_exists.cpp (over Platform::PathAttributes since
// Phase D.4 SDL-5; was src/platform/win/engine_platform_win.cpp). Retail's call is
// `GetFileAttributesW(w.data) != INVALID_FILE_ATTRIBUTES` at 0x1800dd9c4.
bool RtEnginePlatformWidePathExists(const char16_t* path);

namespace {

// B3R: the file-local door typedefs that stood here are gone; every call below
// names its body.

// ---- the exported DATA this body reads ----
// ENDGAME B.3.9 SS1 (B39_RULES D7): all four cells are class statics with a
// redirect in their namespace's *PortInit; the RT_DATA_SLOT lazy-cache doors
// they used to read through are gone (B9-10 — this body runs on the LOADER
// THREAD, exactly the surface the rule calls out).
// B.4 (R-ENG): each is read by name at its use (ExternalFactory, Modification,
// FileSystem, and `?_singleton@Configuration@System@@` -- the "one of the two
// remaining readers" the file banner discusses; the other is Configuration::Get).
// `MOV EDX,dword ptr [0x1801f9be0]` — BaseTypeInfo's FIRST dword is the typeId.
// Retail reads +0x00 and nothing else, which is the whole content of
// reference_basetypeinfo_name_is_content_only.md.
// ENDGAME B.3.9 OBJ: the class record (the minted one is retired).
uint32_t ExternalListTypeId() {
    return ::Object::ExternalList::TypeId();
}

// ENDGAME B.3.1 step 2. The two helpers that used to live here — `AddRefInto`
// (retail's inlined Adopt: null the destination, bump the refcount, take the
// pointer) and an `AdjustDelimiters` name-door call — occurred only ever as a
// PAIR, at 0x1800dd815, 0x1800dda58 and 0x1800ddc4a. That pair IS the body of
// `??0Filename@System@@QEAA@AEBVString@1@@Z` (0x18000c780), so those sites are
// now `System::Filename f(<the String>)` and both helpers are gone. The fifth
// occurrence (0x1800dd95a, the GetPathFilename result copied into `dir`) has NO
// AdjustDelimiters after it and stays a plain String copy — retail's String
// copy ctor 0x180006cb0, spelled as `dir = ret.AsString()`.

// 0x1800dd978-0x1800dd9fa. "Is this compiled resource reachable at all?" —
// packfile first, loose file second, and the two FileSystem bytes at +0x18 and
// +0x19 gate them independently.
bool CompiledResourceExists(const System::Filename& compiled) {
    // 0x1800dd988 MOV RDI,[0x1801fc6a8]; 0x1800dd98f CMP byte ptr [RDI+0x18],0: no test
    // (B6 14d G16: Engine::Startup mints the FileSystem before any loop loads).
    const IO::FileSystem* fs = IO::FileSystem::_singleton;

    if (fs->_existsInPackfileGate != 0) {
        if (fs->ExistsInPackfile(compiled))   // 0x1800ab180
            return true;
    }
    if (fs->_platformExistsGate == 0) return false;

    // Retail hands the Filename to ??0WideString(const String&) as the String
    // it is (0x1800dd9b0 passes `this` straight in); ~WideString releases it.
    System::WideString w(compiled.AsString());
    return RtEnginePlatformWidePathExists(w._data);
}

// 0x1800dd850 / 0x1800ddc6f / 0x1800dda9a. Retail calls IsDerivedFrom and
// THROWS THE ANSWER AWAY at two of the three sites — the inlined
// `External::Cast<ExternalList>` whose result nothing reads. Transcribed as
// written: it is a virtual-free call today, but a later build could make it
// observable, and "simplifying" it away is exactly the class of edit that
// turns a faithful port into a different function.
const ::Object::ExternalList* FindAndCheck(const System::Filename& name, bool keep) {
    const ::Object::External* ext = ::Object::ExternalFactory::_singleton->FindOrLoad(name);   // 0x1800bf320
    if (ext == nullptr) return nullptr;
    const bool derived = ext->IsDerivedFrom(ExternalListTypeId());                           // 0x1800c0390
    if (!keep) return nullptr;
    // External::Cast<ExternalList>: the checked downcast.
    return derived ? static_cast<const ::Object::ExternalList*>(ext) : nullptr;
}

unsigned g_calls     = 0;
unsigned g_ownList   = 0;   // the loop's own Resources.rsc was found
unsigned g_modsSeen  = 0;   // enabled mods walked
unsigned g_modsFound = 0;   // ... of which had a compiled Resources.rsc
unsigned g_refUsed   = 0;   // the `-ref` command-line override fired

}  // namespace

// ============================================================================
// ?LoadResources@UpdateLoop@Engine@@QEAAXXZ   retail 0x1800dd7d0
// ============================================================================
// ENDGAME B.3.7: the body is the member.
// LOADER THREAD (Loader::Run calls it before the loop's slot-6 Load).
void Engine::UpdateLoop::LoadResources() {
    ++g_calls;

    // ---- 0x1800dd7ef: the loop's OWN resource list ------------------------
    // vtable slot 10, GetResourceFilename -- an ordinary virtual call since
    // B.3.7. The base (src/engine/engine/loops.cpp) returns null, which makes
    // this block a no-op rather than a crash for a loop that does not override it.
    const char* root = GetResourceFilename();
    if (root != nullptr) {
        System::String spec = System::String::Format("%sResources.rsc", root);
        System::Filename name(spec);       // 0x18000c780: Adopt + AdjustDelimiters

        const ::Object::ExternalList* list = FindAndCheck(name, true);
        _resourceList = list;
        if (list) ++g_ownList;
        // `name` then `spec` release here — retail's 0x1800dd8a4 / 0x1800dd8b6,
        // in that order, which is C++'s reverse declaration order.
    }

    // ---- 0x1800dd8c8: every ENABLED modification --------------------------
    // Retail reads Modification::_singleton here, NOT `Engine::_modification`,
    // and derefs it unguarded (0x1800dd8c8 MOV R13,[0x1801fc6b0]; 0x1800dd8d2
    // CMP [R13+0x10],R12D). B6 (14d G15): the tree's null test is gone --
    // Engine::Startup constructs the Modification (0x1800dba95) before any loop
    // loads its resources.
    {
        IO::Modification* mods = IO::Modification::_singleton;
        const int32_t count = mods->_mods._size;
        RtIO::Mod* base = mods->_mods._data;
        // EXT SEAM (NOT RETAIL, docs/extensions/handins/LOADPROGRESS.md): ext.load_progress's
        // count of the mod lists this walk loads.
        ExtLoadProgress* const progress = _extLoadProgress;
        int32_t enabledOrdinal = 0;
        if (progress != nullptr) {
            int32_t enabled = 0;
            for (int32_t i = 0; i < count; ++i) {
                if (base[i].enabled != 0)
                    ++enabled;
            }
            progress->mods.store(enabled, std::memory_order_relaxed);
        }
        for (int32_t i = 0; i < count; ++i) {
            RtIO::Mod& mod = base[i];
            if (mod.enabled == 0) continue;
            ++g_modsSeen;
            if (progress != nullptr)
                progress->mod.store(++enabledOrdinal, std::memory_order_relaxed);

            // Filename::GetPathFilename(&mod->file, &ret, /*keepSlash*/true).
            System::String dir;
            {
                // System::Filename::GetPathFilename (0x1800c6740), by name
                // (B3R); the by-value result is constructed in `ret`.
                System::Filename ret(mod.file.GetPathFilename(true));
                dir = ret.AsString();   // 0x1800dd95a: AddRef, no delimiter pass
            }

            System::String spec =
                System::String::Format("%sResources.rsc:resource", dir._data);

            bool exists;
            {
                System::Filename probe(spec);
                System::Filename compiled = ::Object::ExternalFactory::GetCompiledPath(probe);   // 0x1800bec10

                exists = CompiledResourceExists(compiled);
                // `compiled` then `probe` release — retail's own order.
            }

            if (exists) {
                ++g_modsFound;
                System::Filename name(spec);
                // Result DISCARDED by retail — see FindAndCheck's comment.
                FindAndCheck(name, false);
            }
            // `spec` then `dir` release at the bottom of the iteration.
        }
    }

    // ---- 0x1800ddb16: the `-ref <path>` command-line override --------------
    // THE READ OF ?_singleton@Configuration@System@@ THIS WHOLE FILE IS ABOUT.
    System::String value;
    {
        // `key` is scoped so that it still releases exactly where retail
        // releases it (0x1800ddc10), before the length test below.
        System::String key("ref");      // 0x18000b630: zero, then Set
        // System::Configuration::GetCommandLineString (0x1800c5300), by name
        // (B3R): its by-value result is constructed into `value`, the hidden
        // return slot the door was handed.
        ::new (&value) System::String(
            ::System::Configuration::_singleton->GetCommandLineString(key));
    }

    // 0x1800ddc44: `CMP dword ptr [RAX-0x8],0` — the LENGTH word, not the
    // pointer. An empty-but-allocated String must not load anything.
    // `GetLength` (0x1800063c0) IS that test, null case included.
    if (value.GetLength() != 0) {
        ++g_refUsed;
        System::Filename name(value);
        FindAndCheck(name, false);
    }

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/LOADPROGRESS.md): this call is done.
    if (ExtLoadProgress* const progress = _extLoadProgress) {
        progress->mod.store(0, std::memory_order_relaxed);
        progress->lists.fetch_add(1, std::memory_order_relaxed);
    }
}

// EXT SEAM (NOT RETAIL, docs/extensions/handins/LOADPROGRESS.md): null by default.
Engine::UpdateLoop::ExtLoadProgress* Engine::UpdateLoop::_extLoadProgress = nullptr;

extern "C" void RtEngineLoadResourcesSummary() {
    printf("[RtPort]   Engine::UpdateLoop::LoadResources: calls=%u ownList=%u "
           "modsEnabled=%u modsWithResources=%u refOverride=%u\n",
           g_calls, g_ownList, g_modsSeen, g_modsFound, g_refUsed);
}
