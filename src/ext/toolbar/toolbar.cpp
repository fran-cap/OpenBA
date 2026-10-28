// src/ext/toolbar/toolbar.cpp -- the extension window's page list (toolbar.h)
//
// NOT RETAIL. Standard C++ (the unit tests link it with registry.cpp).

#include "ext/toolbar/toolbar.h"

#include "ext/registry.h"

namespace Ext {
namespace Toolbar {

const Contribution* const* All(int* n) {
    static const Contribution* const list[] = { &RestrictedFoodsPage(), &ItemPriorityPage(),
                                                &TerrainEditPage(), &TownScreenshotPage() };
    *n = static_cast<int>(sizeof(list) / sizeof(list[0]));
    return list;
}

int ActivePages(const Contribution** out, int capacity) {
    int count = 0;
    int n = 0;
    const Contribution* const* list = All(&n);
    for (int i = 0; i < n; ++i) {
        const Entry* e = FindEntry(list[i]->id);
        if (e == nullptr || !Active(*e)) {
            continue;
        }
        if (out != nullptr && count < capacity) {
            out[count] = list[i];
        }
        ++count;
    }
    return count;
}

bool Offered() {
    if (GateProfileForced()) {
        return false;
    }
    return ActivePages(nullptr, 0) > 0;
}

}  // namespace Toolbar
}  // namespace Ext
