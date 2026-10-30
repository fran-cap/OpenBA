// ============================================================================
// src/ext/state_codecs.cpp -- the class C extensions' save-tail codecs, one line each (savetail.h)
// ============================================================================
// NOT RETAIL. DESIGN.md "Class C state: the contract". The serializer goes through FindCodec and never
// includes an extension's header. A class B row has no entry here, so its record's payload stays empty.
// ============================================================================

#include "ext/savetail.h"

#include "ext/restricted_foods/restricted_foods.h"   // RestrictedFoods::Codec()
#include "ext/item_priority/item_priority.h"   // ItemPriority::Codec()
#include "ext/tool_names/tool_names.h"   // ToolNames::Codec()

#include <cstring>

namespace Ext {
namespace SaveTail {
namespace {

const StateCodec* const* Codecs(int& n) {
    static const StateCodec* const kCodecs[] = { &RestrictedFoods::Codec(), &ItemPriority::Codec(), &ToolNames::Codec() };
    n = static_cast<int>(sizeof(kCodecs) / sizeof(kCodecs[0]));
    return kCodecs;
}

}  // namespace

const StateCodec* FindCodec(const char* id) {
    if (id == nullptr) {
        return nullptr;
    }
    int n = 0;
    const StateCodec* const* codecs = Codecs(n);
    for (int i = 0; i < n; ++i)
        if (std::strcmp(codecs[i]->id, id) == 0) {
            return codecs[i];
        }
    return nullptr;
}

void ResetAllState() {
    int n = 0;
    const StateCodec* const* codecs = Codecs(n);
    for (int i = 0; i < n; ++i) {
        codecs[i]->reset();
    }
    ResetAllHostState();   // the shared hosts' per-town state goes with the town too
}

}  // namespace SaveTail
}  // namespace Ext
