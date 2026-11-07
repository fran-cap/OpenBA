// ============================================================================
// src/ext/load_progress/load_progress.cpp -- ext.load_progress (class A)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/LOADPROGRESS.md; the rules in load_progress.h.
//
// The screen is a private UI::UserInterface holding one LoaderDialog over
// Dialog/Loading.rsc, built the way Engine::DialogLoaderLoopT builds its own
// (the UserInterface door, the dialog's OnOpened, the add list) and drawn the way
// DialogLoaderLoopT::Draw draws (black clear, the load timer in the three shader
// constants, the UI). Its "descText" line carries our own string
// (Dialog/ExtensionsStringTable.rsc, provided from the exe) with the mod numbers
// as the text's @0 / @1 parameters.
//
// Threads: BootBegin, the two hooks and BootEnd all run on the MAIN thread. The
// loader thread only writes the three relaxed counters. Nothing here reaches the
// ExternalFactory while the loader runs: every object is resident before
// BootBegin returns.
// ============================================================================
#include "ext/load_progress/load_progress.h"
#include "ext/registry.h"

#include "engine/UpdateLoop.h"
#include "engine/MainLoop.h"
#include "io/Modification.h"
#include "object/External.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "system/Filename.h"
#include "system/MemoryAllocator.h"
#include "system/String.h"
#include "system/Timer.h"
#include "system/Value.h"
#include "ui/Dialog.h"
#include "ui/DialogInstance.h"
#include "ui/Element.h"
#include "ui/String.h"
#include "ui/StringTable.h"
#include "ui/TextElement.h"
#include "ui/UserInterface.h"
#include "video/CompanionData.h"
#include "video/Context.h"
#include "video/Device.h"
#include "Dialog/LoaderDialog.h"
#include "ext/phone_menu/loading_screen.h"        // LoadingUiScale (P9)
#include "platform/common/display_density.h"     // UiScaleRatio (P9)

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <new>

namespace Ext {
namespace LoadProgress {
namespace {

constexpr const char* kStringTableRsc = "Dialog/ExtensionsStringTable.rsc";
constexpr const char* kKeyGameData    = "LoadProgressGameData";   // "Loading game data"
constexpr const char* kKeyMod         = "LoadProgressMod";        // "Loading mod @0 of @1"

Engine::UpdateLoop::ExtLoadProgress g_progress;

UI::UserInterface*                      g_ui = nullptr;
Object::InternalPtr<UI::DialogInstance> g_dialog;
UI::TextElement*                        g_line = nullptr;
UI::StringTable*                        g_table = nullptr;
Text                                    g_shown = {Line::GameData, -1, -1};
int64_t                                 g_elapsedTicks = 0;

int32_t EnabledMods() {
    const IO::Modification* mods = IO::Modification::_singleton;
    int32_t enabled = 0;
    for (int32_t i = 0; i < mods->_mods._size; ++i) {
        if (mods->_mods._data[i].enabled != 0)
            ++enabled;
    }
    return enabled;
}

// Dialog/Loading.rsc's "descText", or null. FindElement answers a miss with a
// sentinel, not null (SetRandomLoadingTip, splash_loop_stubs.cpp, normalises it the same way).
UI::TextElement* FindLine(UI::DialogInstance* dialog) {
    const Object::InternalPtr<UI::Element> found = dialog->FindElement(System::String("descText"));
    UI::Element* e = found.Get();
    const uintptr_t raw = reinterpret_cast<uintptr_t>(e);   // CAST-OK: ptr_int
    if (raw < 0x10000 || raw > 0x00007FFFFFFFFFFFULL)
        return nullptr;
    if (!Object::IsDerivedFromNullSafe(e, UI::TextElement::TypeId()))
        return nullptr;
    return static_cast<UI::TextElement*>(e);
}

void ShowText(const Text& t) {
    if (g_line == nullptr || SameText(t, g_shown))
        return;
    g_shown = t;
    const char* key = (t.line == Line::Mod) ? kKeyMod : kKeyGameData;
    const UI::String text{Object::ExternalPtr<UI::StringTable>(g_table), System::String(key)};
    if (!text.IsValid())
        return;
    g_line->SetText(&text);
    if (t.line == Line::Mod) {
        char number[16];
        std::snprintf(number, sizeof(number), "%d", t.mod);
        g_line->SetParameter(0, System::String(number));
        std::snprintf(number, sizeof(number), "%d", t.mods);
        g_line->SetParameter(1, System::String(number));
    }
}

Text CurrentText() {
    return TextFor(g_progress.mod.load(std::memory_order_relaxed),
                   g_progress.mods.load(std::memory_order_relaxed),
                   g_progress.lists.load(std::memory_order_relaxed));
}

// Engine::LoaderLoop::_extFrame.
bool Hook(Engine::LoaderLoop& /*loop*/, int32_t point, const System::Time* time) {
    if (point == Engine::LoaderLoop::kExtUpdate) {
        ShowText(CurrentText());
        g_elapsedTicks += time->_ticks;
        g_ui->Update(*time);
        return true;
    }

    // kExtDraw: DialogLoaderLoopT::Draw's body over our UI (loops.cpp).
    Engine::MainLoop* ml = Engine::MainLoop::_singleton;
    Video::Context* ctx = ml->_context;
    ctx->_contextData->Clear(Video::ClearFlags::Color | Video::ClearFlags::Depth | Video::ClearFlags::Stencil,
                             0u, 1.0f, 0);

    const float secs = System::Timer::TicksToSeconds(g_elapsedTicks);
    ctx->_time.x = secs;
    ctx->_time.y = 0.0f;
    ctx->_time.z = 0.0f;
    ctx->_time.w = 0.0f;
    ctx->_dirty |= RtVideo::kDirtyGlobalConstants;
    ctx->_unknown03b8.x = secs;
    ctx->_unknown03b8.y = 0.0f;
    ctx->_unknown03b8.z = 0.0f;
    ctx->_unknown03b8.w = 0.0f;
    Math::Vector4& staging = ctx->_pixelConstantStaging.pixel[4];
    staging.x = secs;
    staging.y = 0.0f;
    staging.z = 0.0f;
    staging.w = 0.0f;
    ctx->_dirty |= RtVideo::kDirtyGlobalConstants | RtVideo::kDirtyPixelConstants;

    g_ui->Draw(*ctx);
    return true;
}

}  // namespace

void BootBegin(const UI::Dialog* loadingDialog, int32_t uiScalePercent) {
    if (!Ext().load_progress || loadingDialog == nullptr)
        return;
    const int32_t mods = EnabledMods();
    if (mods == 0) {
        fprintf(stderr, "[LoadProgress] no mod enabled: the boot stays retail's\n");
        return;
    }

    // Our string table, provided from the exe (src/ext/resources): resident for the process.
    g_table = Object::ExternalFactory::_singleton->FindOrLoadAs<UI::StringTable>(
        System::Filename(kStringTableRsc));
    if (!Object::IsDerivedFromNullSafe(g_table, UI::StringTable::TypeId())) {
        fprintf(stderr, "[LoadProgress] %s missing: the boot stays retail's\n", kStringTableRsc);
        g_table = nullptr;
        return;
    }

    // The UserInterface door (as UserInterface::ShowDialog builds its nested one).
    void* mem = System::MemoryAllocator::MemAlloc(sizeof(UI::UserInterface), 4);
    g_ui = RtUi_UserInterface_Ctor(static_cast<UI::UserInterface*>(mem));

    // LoaderDialog_FromSplashDLT's construction (loaderdialog.cpp), then AddDialog's
    // OnOpened + add list (DialogLoaderLoopT::Startup does the same two steps).
    void* dlgMem = System::MemoryAllocator::MemAlloc(sizeof(LoaderDialog), 8);
    LoaderDialog* dlg = ::new (dlgMem) LoaderDialog(g_ui, loadingDialog,
                                                    Object::InternalPtr<UI::DialogInstance>());
    g_dialog = Object::InternalPtr<UI::DialogInstance>(dlg);
    g_ui->AddDialog(&g_dialog);

    // The splash's own UI scale (GameLoadDialogLoopT::Startup step 5): the back
    // buffer height against 900 pixels, truncated to a whole percent.
    const float heightRatio =
        900.0f / static_cast<float>(::Video::Device::_singleton->backBufferHeight);
    // P9: with ext.phone_menu on, the Options UI scale instead (phone_menu's loading_screen.h).
    g_ui->_uiScale = Ext::PhoneMenu::LoadingUiScale(
        static_cast<float>(static_cast<int32_t>(heightRatio * 100.0f)) / 100.0f,
        Platform::Display::UiScaleRatio(uiScalePercent));

    g_line = FindLine(dlg);
    if (g_line != nullptr) {
        ShowText(Text{Line::GameData, 0, 0});
    } else {
        fprintf(stderr, "[LoadProgress] Dialog/Loading.rsc has no descText: spinner only\n");
    }

    g_elapsedTicks = 0;
    Engine::UpdateLoop::_extLoadProgress = &g_progress;
    Engine::LoaderLoop::_extFrame = &Hook;
    fprintf(stderr, "[LoadProgress] boot loading screen on (%d mod(s) enabled)\n", mods);
}

void BootEnd() {
    if (Engine::LoaderLoop::_extFrame != &Hook)
        return;
    Engine::LoaderLoop::_extFrame = nullptr;
    Engine::UpdateLoop::_extLoadProgress = nullptr;

    // DialogLoaderLoopT's own teardown order: CloseDialog (Shutdown), then the
    // UserInterface door's destructor (~DialogLoaderLoopT), then the storage.
    g_ui->CloseDialog(&g_dialog);
    g_dialog = Object::InternalPtr<UI::DialogInstance>();
    g_line = nullptr;
    RtUi_UserInterface_Dtor(g_ui);
    System::MemoryAllocator::MemFree(g_ui);
    g_ui = nullptr;
    fprintf(stderr, "[LoadProgress] boot loading screen off (lists=%d)\n",
            g_progress.lists.load(std::memory_order_relaxed));
}

}  // namespace LoadProgress
}  // namespace Ext
