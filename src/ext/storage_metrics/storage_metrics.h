// ============================================================================
// src/ext/storage_metrics/storage_metrics.h -- ext.storage_metrics: a Storage tab in the Town Hall window
// ============================================================================
// NOT RETAIL. docs/extensions/handins/STORAGEMETRICS.md. Class A: it reads the town and draws a tab,
// nothing in the simulation reads it and nothing of it is saved.
//
// The Town Hall window (Dialog/TownHall.rsc, TownHallUI) gets one more tab after the game's own: the used
// and total space of the town's stockpiles, storage barns and trading posts, as figures ("12,400 /
// 15,000") and as a share ("82.6%"), and under a kind that has more than one building type (a mod's
// special stores) a line per type. The sums are storage_math.h, the town walk storage_town.cpp, the tab
// townhall_tab.cpp, its page src/ext/ui/Dialog/StorageMetricsTab.rsc.
//
// The seams, all in src/game/Dialog/townhallui.cpp (each a no-op while the row is off or for a window
// that has no tab):
//   OnCreated            TownHallCreated: adds the tab once the game's own are built
//   OnUpdate             TownHallUpdate: refreshes the figures while the tab is the one showing
//   OnSerializeSettings  SavedTab: the window's remembered tab is written as a tab retail has
//   ~TownHallUI          TownHallClosed: forgets the window's elements
// ============================================================================
#pragma once

#include <cstdint>

#include "ext/storage_metrics/storage_math.h"

class TownHallUI;
namespace UI {
class TabElement;
}

namespace Ext {
namespace StorageMetrics {

// The town's finished storage buildings, summed (storage_town.cpp). Empty with no running game.
Snapshot Gather();

void TownHallCreated(const TownHallUI* hall, UI::TabElement* tabs);
void TownHallUpdate(const TownHallUI* hall);
void TownHallClosed(const TownHallUI* hall);
// The tab index the window's settings record for `tab`: the Storage tab is written as the first tab, so
// the record never names a tab the game without the extension does not have.
// The end of TownHallUI::OnSerializeSettings. Written (the window closes): notes whether an added tab was
// showing. Read (it reopens): shows the last added tab again when the note says so. The note is kept in
// memory per building for the session; nothing of it is in the settings record or the save.
void TownHallSettings(const TownHallUI* hall, bool reading);

int32_t SavedTab(const TownHallUI* hall, int32_t tab);

}  // namespace StorageMetrics
}  // namespace Ext
