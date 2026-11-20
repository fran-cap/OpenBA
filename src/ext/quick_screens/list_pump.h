// ============================================================================
// src/ext/quick_screens/list_pump.h -- ext.quick_screens: the save list (class A)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/SPLASH.md section 2.3; quick_screens.h has
// the loop (PumpListSteps) and the budget.
// ============================================================================
#pragma once

namespace Save {
class SaveLoad;
}

namespace Ext {
namespace QuickScreens {

// Called by SaveLoadDialog::OnUpdate once per frame while `saveLoad` runs its
// list operation: takes further steps of Save::SaveLoad::UpdateList inside this
// frame, each as soon as the persistence worker is idle, for at most
// kListPumpBudgetMs. Main thread only (it calls Save::SaveLoad::Update).
void PumpSaveList(Save::SaveLoad* saveLoad);

}  // namespace QuickScreens
}  // namespace Ext
