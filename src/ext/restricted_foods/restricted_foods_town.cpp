// src/ext/restricted_foods/restricted_foods_town.cpp -- NOT RETAIL. ext.restricted_foods (class C): the game
// side. The factory walk that finds the Edible materials, the path a material is listed by, and the once-per-
// frame BeginFrame. See restricted_foods.h and docs/extensions/handins/RESTRICTEDFOODS.md.
#include "ext/restricted_foods/restricted_foods.h"

#include "Component/RawMaterialDescription.h"
#include "ext/registry.h"   // Ext::Ext().restricted_foods (the game's locked value), Ext::LockEpoch
#include "object/External.h"
#include "object/TypeInfo.h"
#include "system/Filename.h"

namespace Ext {
namespace RestrictedFoods {

std::string TemplatePath(const void* external) {
    if (external == nullptr) {
        return std::string();
    }
    const System::Filename path = static_cast<const ::Object::External*>(external)->GetPath();
    return path._data != nullptr ? std::string(path._data) : std::string();
}

namespace {

// Every loaded RawMaterialDescription with the Edible bit, with its path. The factory walk is the one the
// game uses to enumerate what a package loaded (ExternalFactory::FindNext).
std::vector<Candidate> EdibleMaterials() {
    std::vector<Candidate> out;
    ::Object::ExternalFactory* factory = ::Object::ExternalFactory::_singleton;
    if (factory == nullptr) {
        return out;
    }
    const uint32_t tid = RawMaterialDescription::TypeId();
    const ::Object::External* prev = nullptr;
    int guard = 0;
    while ((prev = factory->FindNext(tid, prev)) != nullptr && guard++ < 20000) {
        if (!::Object::IsDerivedFromNullSafe(prev, tid)) {
            continue;
        }
        const RawMaterialDescription* m = static_cast<const RawMaterialDescription*>(prev);
        if ((m->_flags & RawMaterialFlags::Edible) == 0) {
            continue;
        }
        Candidate c;
        c.material = m;
        c.path = TemplatePath(prev);
        out.push_back(c);
    }
    return out;
}

}  // namespace

void BeginFrame() {
    const uint32_t epoch = LockEpoch();
    if (NeedsResolve(epoch)) {
        Resolve(epoch, BlockedPaths().empty() ? std::vector<Candidate>() : EdibleMaterials());
    }
    SetRowActive(Ext::Ext().restricted_foods);   // class C: the game's locked value
}

}  // namespace RestrictedFoods
}  // namespace Ext
