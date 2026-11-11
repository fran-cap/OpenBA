// src/diag/template_dump.cpp
//
// BANISHED_TEMPLATE_DUMP=<DescriptionTypeName> -- list the placeable buildings that carry one kind of
// description. Harness only, not retail.
//
// A mod's packages cannot be read for names (the directory is compressed), and a harness that places a
// building (BANISHED_SHOWROOM's list, BANISHED_PLACEMENT_TEMPLATE) needs its template path. This walks
// every loaded toolbar item that places a building, as the toolbar itself does, and logs the template
// path of each one whose template has a description of the named type (StorageLocationDescription for
// every storage building, TradeDescription, MarketDescription, ...; "*" for every building), once, 120
// frames into the game:
//
//   [TemplateDump] Template\StorageBarn.rsc
//   [TemplateDump] 3 of 87 placeable building(s) carry StorageLocationDescription
//
// The lines go to standalone_error.log. Read only.

#include "object/External.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "system/Filename.h"
#include "Component/ComponentDescription.h"
#include "Component/Description.h"
#include "Game/Toolbar.h"
#include "Object/factory_registration.h"
#include "options.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

namespace {

constexpr int kDumpFrame = 120;

bool Carries(const ComponentDescription* tmpl, uint32_t tid) {
    for (int32_t i = 0; i < tmpl->_subDescCount; ++i) {
        const Description* d = tmpl->_subDescs[i];
        if (d != nullptr && ::Object::IsDerivedFromNullSafe(d, tid)) {
            return true;
        }
    }
    return false;
}

}  // namespace

extern "C" void TemplateDump_Tick(int frameNum) {
    static bool s_done = false;
    if (s_done || frameNum < kDumpFrame) {
        return;
    }
    s_done = true;
    const char* want = Opt().templateDump;
    if (want == nullptr || want[0] == '\0') {
        return;
    }
    const bool all = std::strcmp(want, "*") == 0;
    const uint32_t wantTid = all ? 0 : ::Object::GetTypeIdByName(want);
    if (!all && wantTid == 0) {
        std::fprintf(stderr, "[TemplateDump] no type named \"%s\": nothing listed\n", want);
        return;
    }
    ::Object::ExternalFactory* factory = ::Object::ExternalFactory::_singleton;
    if (factory == nullptr) {
        return;
    }
    const uint32_t toolbarTid = ::Toolbar::TypeId();
    std::set<std::string> seen;   // a mod can list one building in two toolbar groups
    int buildings = 0;
    int listed = 0;
    const ::Object::External* ext = nullptr;
    int guard = 0;
    while ((ext = factory->FindNext(toolbarTid, ext)) != nullptr && guard++ < 50000) {
        if (!::Object::IsDerivedFromNullSafe(ext, toolbarTid)) {
            continue;
        }
        const ::Toolbar* item = static_cast<const ::Toolbar*>(ext);
        if (item->actionType != 1 || item->componentDescription == nullptr) {
            continue;
        }
        const System::Filename path = item->componentDescription->GetPath();
        const std::string key = path._data != nullptr ? std::string(path._data) : std::string();
        if (key.empty() || !seen.insert(key).second) {
            continue;
        }
        ++buildings;
        if (all || Carries(item->componentDescription, wantTid)) {
            ++listed;
            std::fprintf(stderr, "[TemplateDump] %s\n", key.c_str());
        }
    }
    std::fprintf(stderr, "[TemplateDump] %d of %d placeable building(s) carry %s\n", listed, buildings,
                 all ? "anything" : want);
    std::fflush(stderr);
}
