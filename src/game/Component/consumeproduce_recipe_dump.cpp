// src/game/Component/consumeproduce_recipe_dump.cpp
//
// One-shot dump of a ConsumeProduceDescription's recipe table.
//
// The tavern stalls at "no materials" and the useful question is not "why is the work
// stuck" but "which recipe is selected and what does that recipe actually eat". Reading
// it out of the live description answers both, and beats reading ToolKit/*.rsc — the
// .pkm mods override data0.pkg, so the authored source on disk is not necessarily what
// is running.
//
// Layout, read off ConsumeProduceUI::Slot4 (0x14005F3D0) and ConsumeProduce::Execute
// (0x1400e7840). The description holds a recipe array at +0x28 with the count at +0x30,
// stride 0x28 (5 qwords), and its UI string table at +0x20. Per recipe entry:
//
//   +0x00  qword  accepted-INPUT array   (stride 0x10, first qword = material description)
//   +0x08  int    input count
//   +0x10  qword  the PRODUCT's description  (Execute treats a stored good matching this
//                                             as "belongs here", same as an input)
//   +0x18  int    materialType -- index into the world material table at
//                                 (*(GameWorld+0xf0))[0x2e], where [t*4] is supply and
//                                 [t*4+1] demand (Execute's depletion test)
//   +0x20  ---    name key, resolved against the string table at desc+0x20
//
// A "material description" object (both the product at +0x10 and each input) carries a
// child list at +0x28 / count +0x30 holding a RawMaterialDescription, whose +0x5c is the
// category flag mask and whose +0x38 is its sprite name. Those flags are the same ones
// HappinessComponent::IsActive ANDs against the tavern's wantMask 0x20000 -- so this dump
// also says, directly, which recipe can ever satisfy the happiness goods arm.

#include "ui/UserInterface.h"
#include "object/TypeInfo.h"
#include "system/String.h"
#include <cstdint>
#include <cstdio>
#include <new>
#include "object/External.h"   // ENDGAME B.3.6 UI callers (K4)
#include "ui/String.h"         // ENDGAME B.3.6 UI callers (K4)
#include "ui/StringTable.h"    // ENDGAME B.3.6 UI callers (K4)
#include "Component/RawMaterialDescription.h"
#include "Component/ComponentDescription.h"
#include "Component/ConsumeProduceDescription.h"

namespace {

// Read a RawMaterialDescription's category flags and sprite name.
bool ReadRawMat(const RawMaterialDescription* rd, const char** outName, unsigned* outFlags) {
    if (!rd) return false;
    *outFlags = rd->_flags;
    const char* nm = rd->_nameSmall._data;
    if (nm && nm[0]) *outName = nm;
    return true;
}

bool IsRawMat(const Object::Base* c) {
    if (!c) return false;
    const uint32_t tid = c->Type();
    if (tid != RawMaterialDescription::TypeId()) return false;
    return ::Object::IsDerivedFromNullSafe(c, RawMaterialDescription::TypeId());
}

// The recipe table mixes two shapes and the first pass only handled one, which is why
// every INPUT printed its flags but came back nameless: a recipe's product is a wrapper
// description whose CHILD list holds the RawMaterialDescription, while an entry in the
// input array is (or can be) the RawMaterialDescription itself. Try the object directly
// first, then fall back to its child list.
void DescribeMaterial(const ComponentDescription* matDesc, const char** outName, unsigned* outFlags) {
    *outName = "?";
    *outFlags = 0;
    if (!matDesc) return;
    // Either shape answers Object::Base's Type(); the RawMaterialDescription one is not
    // a ComponentDescription, so the direct read goes through the common base.
    const Object::Base* self = matDesc;
    if (IsRawMat(self)) {
        ReadRawMat(static_cast<const RawMaterialDescription*>(self), outName, outFlags);
        return;
    }
    const int n = matDesc->_subDescCount;
    Description* const* arr = matDesc->_subDescs;
    if (!arr || n <= 0 || n > 4096) return;
    for (int j = 0; j < n; ++j) {
        Description* c = arr[j];
        if (!c) continue;
        if (!IsRawMat(c)) continue;
        ReadRawMat(static_cast<RawMaterialDescription*>(c), outName, outFlags);
        return;
    }
}

// The recipe LABEL -- the exact text the panel puts in the Product dropdown.
//
// This is the readable identity, and it is on the recipe ENTRY (its name, a key into
// the description's UI string table), not on the material. Chasing the name through the
// material description gave "?" for every input: the RawMaterialDescription reached that
// way has an EMPTY sprite-name field, even though its flags read fine.
// ConsumeProduceUI::Slot4 (0x14005F3D0) builds the combo text this way instead, so this
// reproduces that path exactly.
const char* RecipeLabel(UI::StringTable* stringTable, const System::String& name, char* buf, int cap) {
    buf[0] = 0;
    if (!stringTable) return buf;
    UI::String uiStr(Object::ExternalPtr<UI::StringTable>(stringTable), name);
    System::String sys(uiStr.operator System::String());
    const char* d = sys._data;
    if (d) { int i = 0; for (; i < cap - 1 && d[i]; ++i) buf[i] = d[i]; buf[i] = 0; }
    return buf;
}

}  // namespace

// Dump every recipe once per description. `selected` is the component's live
// consumeResourceId (+0x50) so the chosen row is marked in the output.
extern "C" void ConsumeProduce_DumpRecipes(const ConsumeProduceDescription* cpDescV, int selected) {
    static const ConsumeProduceDescription* s_done[8] = {};
    static int   s_n = 0;
    if (!cpDescV) return;
    for (int i = 0; i < s_n; ++i) if (s_done[i] == cpDescV) return;
    if (s_n < 8) s_done[s_n++] = cpDescV;

    const ConsumeProduceDescription* d = cpDescV;
    const int count = d->_recipeCount;
    const ConsumeProduceRecipe* base = d->_recipes;
    fprintf(stderr, "[Recipes] desc=%p count=%d selected(+0x50)=%d\n",
            cpDescV, count, selected);
    if (!base || count <= 0 || count > 256) { fflush(stderr); return; }

    for (int i = 0; i < count; ++i) {
        const ConsumeProduceRecipe& e = base[i];
        const int matType = static_cast<int>(e.materialType);
        const int inCount = e.inputCount;

        const char* pName; unsigned pFlags;
        DescribeMaterial(e.product, &pName, &pFlags);
        char label[128];
        RecipeLabel(d->_stringTable, e.name, label, sizeof(label));
        fprintf(stderr, "[Recipes]  [%d]%s label=\"%s\" product=%s flags=0x%x matType=%d inputs=%d\n",
                i, (i == selected ? " <== SELECTED" : ""), label, pName, pFlags,
                matType, inCount);

        if (!e.inputs || inCount <= 0 || inCount > 256) continue;
        for (int k = 0; k < inCount; ++k) {
            const ComponentDescription* in = e.inputs[k].material;
            const char* iName; unsigned iFlags;
            DescribeMaterial(in, &iName, &iFlags);
            fprintf(stderr, "[Recipes]        input[%d] = %s flags=0x%x ptr=%p\n",
                    k, iName, iFlags, static_cast<const void*>(in));
        }
    }
    fflush(stderr);
}

// (ConsumeProduce_RecipeLabelAt, the single-entry label reader for a town-stock probe
// that no longer exists, had no caller left: deleted, B4 wave X.)
