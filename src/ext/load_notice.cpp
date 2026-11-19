// src/ext/load_notice.cpp -- see load_notice.h (block EXTNOTICE, NOT RETAIL).
#include "ext/load_notice.h"

#include "ext/registry.h"

#include <cstring>

namespace Ext {

PseudoModNotice PseudoModNoticeFor(const char* path, int32_t savedFormatVersion) {
    if (!IsPseudoModPath(path)) return PseudoModNotice::NotInBuild;
    // A retired row's entry: no notice (registry.h RETIRED ROWS), as PseudoModLoadStatus says 0.
    if (RetiredSaveEntry(path + std::strlen(kPseudoModPrefix), savedFormatVersion))
        return PseudoModNotice::None;
    const Entry* e = FindEntry(path + std::strlen(kPseudoModPrefix));
    if (e == nullptr || e->tier != Tier::Extension || !IsLockedClass(e->cls))
        return PseudoModNotice::NotInBuild;
    if (savedFormatVersion > e->formatVersion) return PseudoModNotice::NewerVersion;
    if (!Requested(*e)) return PseudoModNotice::TurnedOff;
    return PseudoModNotice::None;
}

const char* PseudoModNoticeDialog(PseudoModNotice notice) {
    switch (notice) {
    case PseudoModNotice::NotInBuild:   return "Dialog/Extensions.rsc:extensionNotInBuild";
    case PseudoModNotice::NewerVersion: return "Dialog/Extensions.rsc:extensionNewer";
    case PseudoModNotice::TurnedOff:    return "Dialog/Extensions.rsc:extensionTurnedOff";
    case PseudoModNotice::None:         break;
    }
    return nullptr;
}

}  // namespace Ext
