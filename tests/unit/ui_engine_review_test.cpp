// ============================================================================
// tests/unit/ui_engine_review_test.cpp — review findings in src/engine/ui
// ============================================================================
// F-104  UserInterface::UpdateEvents (retail 0x18011fc60) re-reads _events'
//        base and size on every iteration: an event a handler posts during
//        the drain is delivered in the same drain, also when the post grows
//        the array.
// F-173  UserInterface::CleanRemovedDialogs (retail 0x18011f790) re-reads
//        _removedDialogs' size on every iteration: a dialog a destructor
//        queues during the drain is unlinked and deleted in the same drain.
// F-155  UserInterface::Play (retail 0x1801223d0) builds the voice handle
//        with a 32-bit SHL that wraps; the port shifts unsigned. The value is
//        pinned here; the signed-shift UB itself shows only under
//        -fsanitize=shift (PR body).
// F-129  ComboElement::UpdateText (retail 0x180112180): a fifth child that
//        holds no LabelElement returns instead of calling IsDerivedFrom on null.
// F-128  DocumentTextElement::GetNextItem (retail 0x1800cdf40): ^f / ^c / ^i
//        slot digits and ^li's font slot never index past the description's
//        own fonts / colours / sheets / bullet frames.
//
// The bodies are the real ones (userinterface.cpp, widgets_input.cpp,
// documenttext.cpp, listed in sources.cmake); the objects are raw zeroed
// storage with only the fields each body reads set, as the class ABI tests do.
// ============================================================================
#include "unit.h"

#include "ui/UserInterface.h"
#include "ui/DialogInstance.h"
#include "ui/Element.h"
#include "ui/ComboElement.h"
#include "ui/LabelElement.h"
#include "ui/DocumentTextElement.h"
#include "ui/SpriteSheet.h"
#include "audio/Audio.h"
#include "system/String.h"
#include "system/Timer.h"
#include "object/Internal.h"
#include "system/MemoryAllocator.h"
#include "collection/Collection.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>

namespace Object { namespace detail { extern InternalFactory* g_unitInternalFactory; } }

namespace {

struct ReviewFactoryScope {
    ::Object::InternalFactory  factory;
    ::Object::InternalFactory* saved;
    ReviewFactoryScope() : factory(), saved(::Object::detail::g_unitInternalFactory) {
        ::Object::detail::g_unitInternalFactory = &factory;
    }
    ~ReviewFactoryScope() { ::Object::detail::g_unitInternalFactory = saved; }
};

UI::UserInterface* NewUserInterface() {
    void* mem = std::calloc(1, sizeof(UI::UserInterface));
    return RtUi_UserInterface_Ctor(static_cast<UI::UserInterface*>(mem));
}

void PostEvent(UI::UserInterface* ui, UI::DialogInstance* dialog, int32_t id) {
    UI::Event ev{};
    ev.dialog = dialog;
    ev.eventId = static_cast<UI::EventId>(id);   // test ids, some outside the enum
    rtcoll::RtColl_ArraySetSizeFill_180033690(&ui->_events, ui->_events._size + 1, &ev);
}

// A dialog whose handler posts one more event on its first call, as
// CloseDialog's drag-cancel arm does from inside a handler.
struct PostingDialog final : UI::DialogInstance {
    UI::UserInterface* ui = nullptr;
    int calls = 0;
    int lastId = 0;
    void OnElementEvent(Object::InternalPtr<UI::Element>*, UI::EventId eventId, int32_t, int32_t,
                        int32_t, int32_t, void*) override {
        ++calls;
        lastId = static_cast<int32_t>(eventId);
        if (calls == 1) {
            PostEvent(ui, this, 99);
        }
    }
};

PostingDialog* NewPostingDialog(UI::UserInterface* ui) {
    void* mem = ::Object::detail::InternalMemAlloc(sizeof(PostingDialog), 16);
    PostingDialog* d = ::new (mem) PostingDialog();
    d->ui = ui;
    return d;
}

}  // namespace

// F-104: below capacity the posted event used to be dropped (the cached count).
UNIT_TEST(ui_update_events_delivers_an_event_posted_by_a_handler) {
    ReviewFactoryScope scope;
    UI::UserInterface* ui = NewUserInterface();
    PostingDialog* dlg = NewPostingDialog(ui);

    // Queue until there is room for one more without a reallocation.
    int queued = 0;
    while (queued < 2 || ui->_events._size == ui->_events._capacity) {
        PostEvent(ui, dlg, 1);
        ++queued;
    }
    UI::Event* const before = ui->_events._data;

    ui->UpdateEvents();

    CHECK(ui->_events._data == before);      // no reallocation on this path
    CHECK_EQ(dlg->calls, queued + 1);        // retail delivers the posted event
    CHECK_EQ(dlg->lastId, 99);
    CHECK_EQ(ui->_events._size, 0);
    CHECK_EQ(static_cast<int>(ui->_dragDropActive), 0);
}

// F-104: at capacity the post reallocates; the drain must read the new buffer
// (the cached base read the freed one).
UNIT_TEST(ui_update_events_survives_a_handler_post_that_grows_the_array) {
    ReviewFactoryScope scope;
    UI::UserInterface* ui = NewUserInterface();
    PostingDialog* dlg = NewPostingDialog(ui);

    int queued = 0;
    while (queued < 2 || ui->_events._size != ui->_events._capacity) {
        PostEvent(ui, dlg, 1);
        ++queued;
    }
    UI::Event* const before = ui->_events._data;

    ui->UpdateEvents();

    CHECK(ui->_events._data != before);      // the handler's post grew the array
    CHECK_EQ(dlg->calls, queued + 1);
    CHECK_EQ(dlg->lastId, 99);
    CHECK_EQ(ui->_events._size, 0);
}

namespace {

int g_victimDeleted = 0;

// The second dialog: only its deletion is observed.
struct VictimDialog final : UI::DialogInstance {
    ~VictimDialog() override { ++g_victimDeleted; }
};

// A dialog whose destructor queues another dialog for removal, as
// ~ComboElement's CloseDialog of an open drop-down does.
struct ClosingDialog final : UI::DialogInstance {
    UI::UserInterface* ui = nullptr;
    UI::DialogInstance* other = nullptr;
    ~ClosingDialog() override {
        const UI::DialogInstancePtr p = other;
        ui->_removedDialogs.SetSize(ui->_removedDialogs._size + 1, p);
    }
};

// The host has no seated MemoryAllocator. CleanRemovedDialogs frees each list
// node through MemoryAllocator::MemFree; the unit build's LinkedList::Add
// allocates nodes through Collection::detail::AllocateListNode, so the seat
// hands the free back to that allocator for the test's duration.
struct ListNodeSeat {
    ListNodeSeat() { System::g_memoryAllocatorInstrument.memFree = &Collection::detail::ListNodeFree; }
    ~ListNodeSeat() { System::g_memoryAllocatorInstrument.memFree = nullptr; }
};

template <class T>
T* NewDialog() {
    void* mem = ::Object::detail::InternalMemAlloc(sizeof(T), 16);
    return ::new (mem) T();
}

}  // namespace

UNIT_TEST(ui_clean_removed_dialogs_deletes_a_dialog_queued_during_the_drain) {
    ReviewFactoryScope scope;
    ListNodeSeat seat;
    g_victimDeleted = 0;
    UI::UserInterface* ui = NewUserInterface();
    ClosingDialog* first = NewDialog<ClosingDialog>();
    VictimDialog* second = NewDialog<VictimDialog>();
    first->ui = ui;
    first->other = second;

    ui->_dialogs.Add(first);
    ui->_dialogs.Add(second);
    const UI::DialogInstancePtr queuedFirst = first;
    ui->_removedDialogs.SetSize(1, queuedFirst);

    ui->CleanRemovedDialogs();

    CHECK_EQ(g_victimDeleted, 1);            // retail visits the appended entry
    CHECK_EQ(static_cast<int>(ui->_dialogs._count), 0);
    CHECK(ui->_dialogs._head == nullptr);
    CHECK(ui->_dialogs._tail == nullptr);
    CHECK_EQ(ui->_removedDialogs._size, 0);
}

// F-155: the handle is retail's wrapped 32-bit `(index * 2) | (counter << 9)`.
UNIT_TEST(ui_play_handle_wraps_like_retail_shl) {
    System::Timer::_secondsPerTick = 1.0 / 10000000.0;
    void* uiMem = std::calloc(1, sizeof(UI::UserInterface));
    auto* ui = static_cast<UI::UserInterface*>(uiMem);
    void* sceneMem = std::calloc(1, sizeof(Audio::Scene));
    auto* scene = static_cast<Audio::Scene*>(sceneMem);
    void* chans = std::calloc(0x100, sizeof(Audio::Scene::Channel));
    scene->channels._data = static_cast<Audio::Scene::Channel*>(chans);
    scene->channels._size = 0x100;
    static int fakeSound;
    ui->_sounds[0] = reinterpret_cast<Audio::SoundEffect*>(&fakeSound);
    ui->_audioScene = scene;

    scene->playGeneration = (1 << 23);               // counter 2^23 + 1: bit 23 shifts out
    ui->Play(0);
    CHECK_EQ(scene->playGeneration, (1 << 23) + 1);
    CHECK_EQ(scene->channels._data[0].handleTag, 0x200u);

    scene->playGeneration = 0x7fffffff;              // the counter itself wraps
    ui->Play(0);
    CHECK_EQ(static_cast<uint32_t>(scene->playGeneration), 0x80000000u);
    CHECK_EQ(scene->channels._data[1].handleTag, 2u);   // slot 1, counter bits all shifted out

    std::free(chans);
    std::free(sceneMem);
    std::free(uiMem);
}

namespace {

// The host never runs Factory::Startup, so every type id is 0xFFFFFFFF; this
// element answers a distinct one so it is not taken for a LabelElement.
struct PlainElement final : UI::Element {
    void ComputeSize() override {}
    uint32_t Type() const override { return 0x51a1u; }
};

}  // namespace

// F-129: the fifth child is neither a LabelElement nor holds one.
UNIT_TEST(ui_combo_update_text_without_a_label_returns) {
    ReviewFactoryScope scope;
    alignas(16) static unsigned char childMem[sizeof(PlainElement)];
    PlainElement* child = ::new (static_cast<void*>(childMem)) PlainElement();

    void* comboMem = std::calloc(1, sizeof(UI::ComboElement));
    auto* combo = static_cast<UI::ComboElement*>(comboMem);
    UI::Element* kids[5] = {child, child, child, child, child};
    combo->_children._data = kids;
    combo->_children._size = 5;
    combo->_children._capacity = 5;
    System::String item("item");
    combo->_strings._data = &item;
    combo->_strings._size = 1;
    combo->_strings._capacity = 1;
    combo->_currentItem = 0;
    CHECK(child->Type() != UI::LabelElement::TypeId());

    combo->UpdateText();                    // retail: IsDerivedFrom(null) faults

    CHECK_EQ(child->_children._size, 0);
    combo->_children._data = nullptr;
    combo->_strings._data = nullptr;
    std::free(comboMem);
}

namespace {

// A DocumentTextElement in raw zeroed storage, with `text` as its source and
// the given numbers of font / colour / sheet / bullet-frame slots. Each array
// is allocated ten slots long so an unbounded digit reads a known value.
struct DocText {
    UI::DocumentTextElement* e = nullptr;
    UI::Font* fonts[10] = {};
    int32_t colors[10] = {};
    UI::SpriteSheet* sheets[10] = {};
    ::Sprite frames[10] = {};

    DocText(const char* text, int nFonts, int nColors, int nSheets, int nFrames) {
        void* mem = std::calloc(1, sizeof(UI::DocumentTextElement));
        e = static_cast<UI::DocumentTextElement*>(mem);
        ::new (static_cast<void*>(&e->_sourceText)) System::String(text);
        e->_textLength = static_cast<int32_t>(std::strlen(text));
        for (int i = 0; i < 10; ++i) {
            colors[i] = 0x1000 + i;
            frames[i].spriteSheet = nullptr;
            frames[i].spriteIndex = -1;
        }
        e->_fonts._data = fonts;
        e->_fonts._size = nFonts;
        e->_fonts._capacity = 10;
        e->_colors._data = colors;
        e->_colors._size = nColors;
        e->_colors._capacity = 10;
        e->_sheets._data = sheets;
        e->_sheets._size = nSheets;
        e->_sheets._capacity = 10;
        e->_frameTable._data = frames;
        e->_frameTable._size = nFrames;
        e->_frameTable._capacity = 10;
    }
    ~DocText() {
        e->_sourceText.~String();
        std::memset(static_cast<void*>(e), 0, sizeof(UI::DocumentTextElement));
        std::free(e);
    }
};

}  // namespace

// F-128: a slot digit at or past the description's own count is an abandoned
// escape; one inside it is stored as before.
UNIT_TEST(ui_doctext_font_and_colour_digits_stay_inside_the_description) {
    {
        DocText d("^c9", 3, 3, 1, 4);
        UI::DocumentTextElement::DocumentItem item{};
        CHECK(!d.e->GetNextItem(item));
        CHECK_EQ(d.e->_colorIndex, 0);
    }
    {
        DocText d("^f7", 3, 3, 1, 4);
        UI::DocumentTextElement::DocumentItem item{};
        CHECK(!d.e->GetNextItem(item));
        CHECK_EQ(d.e->_fontIndex, 0);
    }
    {
        DocText d("^c2^f2", 3, 3, 1, 4);     // in range: stored, as retail
        UI::DocumentTextElement::DocumentItem item{};
        CHECK(!d.e->GetNextItem(item));
        CHECK_EQ(d.e->_colorIndex, 2);
        CHECK_EQ(d.e->_fontIndex, 2);
    }
}

UNIT_TEST(ui_doctext_sprite_slot_past_the_sheets_is_abandoned) {
    // Slot 9 of a one-sheet description: the tenth array entry holds a real
    // sheet with a sprite named "B", so an unbounded read finds it.
    void* sheetMem = std::calloc(1, sizeof(UI::SpriteSheet));
    auto* sheet = static_cast<UI::SpriteSheet*>(sheetMem);
    void* infoMem = std::calloc(1, sizeof(UI::SpriteInfo));
    auto* info = static_cast<UI::SpriteInfo*>(infoMem);
    ::new (static_cast<void*>(&info->name)) System::String("B");
    info->width = 7;
    info->height = 5;
    sheet->m_sprites._data = info;
    sheet->m_sprites._size = 1;
    sheet->m_sprites._capacity = 1;
    {
        DocText d("^i9B", 3, 3, 1, 4);
        for (int i = 1; i < 10; ++i) {
            d.sheets[i] = sheet;
        }
        UI::DocumentTextElement::DocumentItem item{};
        CHECK(!d.e->GetNextItem(item));
        CHECK_EQ(static_cast<int>(item.kind), 0);
    }
    {
        DocText d("^i0B", 3, 3, 1, 4);       // in range: the sprite item, as retail
        d.sheets[0] = sheet;
        UI::DocumentTextElement::DocumentItem item{};
        CHECK(d.e->GetNextItem(item));
        CHECK_EQ(static_cast<int>(item.kind), 1);
        CHECK_EQ(item.index, 0);
        CHECK_EQ(item.first, 0);
        CHECK_EQ(item.width, 7);
    }
    info->name.~String();
    sheet->m_sprites._data = nullptr;
    std::free(infoMem);
    std::free(sheetMem);
}

UNIT_TEST(ui_doctext_bullet_past_the_frame_table_is_abandoned) {
    {
        DocText d("^li", 3, 3, 1, 1);
        d.e->_fontIndex = 1;                 // a font slot with no bullet frame
        UI::DocumentTextElement::DocumentItem item{};
        CHECK(!d.e->GetNextItem(item));
        CHECK_EQ(static_cast<int>(item.kind), 0);
    }
    {
        DocText d("^li", 3, 3, 1, 1);        // slot 0 has its frame: the bullet, as retail
        UI::DocumentTextElement::DocumentItem item{};
        CHECK(d.e->GetNextItem(item));
        CHECK_EQ(static_cast<int>(item.kind), 1);
        CHECK_EQ(item.first, -1);
    }
}
