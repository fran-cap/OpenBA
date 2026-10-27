// ============================================================================
// src/ext/phone_menu/touch_scroll.cpp -- ext.phone_menu: a tall dialog scrolls instead of shrinking (P11b)
// ============================================================================
// The rules are in touch_scroll.h; the arithmetic is size_math.cpp (ShrinkCap, GrowCap).
//
// HOW RETAIL BUILDS WHAT IS CHANGED HERE (read off the engine's bodies):
//   * A scrolling list is a UI::ContainerElement whose children are [clip region, bar, bar]
//     (ContainerDescription::CreateInstance 0x1800ccb90); the clip region (UI::ClipRegionElement) holds
//     the content as its child 0 and measures it (`_innerWidth` / `_innerHeight`). The container's height
//     is its `_requestedHeight` when that is positive (shrunk to the content when `_clampHeight` is set),
//     the content's otherwise; the clip region gets what is left after the bars (ContainerElement::
//     ComputeSize 0x1800cc850). So a list is capped by writing its instance's `_requestedHeight`, and
//     the scroll bar, its thumb and the wheel follow by themselves.
//   * Element owns its children by raw pointer (`_children`, deleted by RemoveChildren 0x180115570) and
//     knows its `_parent`; only a dialog's ROOT knows its DialogInstance (`_dialogParent`). A tab's pages
//     are its children 4 + 2 t (TabElement::GetContainerIndex 0x1800da110), shown and hidden by their
//     kElementFlagHidden bit (UpdateContainers 0x1800d9710). So a page is moved into a frame by pointer
//     surgery: the frame takes the page's slot and hidden bit, the page becomes the clip region's child.
//   * A frame is built as TabElement::AddTab builds a page (0x1800da130): the description's slot-8
//     CreateInstance against a scratch UI::Dialog.
// ============================================================================

#include "ext/phone_menu/touch_scroll.h"
#include "ext/phone_menu/size_math.h"
#include "ext/phone_menu/touch_math.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <new>

#include "object/External.h"
#include "object/TypeInfo.h"
#include "system/Filename.h"
#include "ui/ClipRegionElement.h"
#include "ui/ContainerDescription.h"
#include "ui/ContainerElement.h"
#include "ui/Dialog.h"
#include "ui/DialogInstance.h"
#include "ui/ElementDescription.h"
#include "ui/ScrollElement.h"
#include "ui/TabElement.h"
#include "ui/UserInterface.h"
#include "video/Device.h"   // Video::Device::_singleton: the back buffer the safe area is cut from

namespace Ext {
namespace PhoneMenu {

namespace {

constexpr const char* kFrameRsc = "Dialog/PhoneScroll.rsc:phoneScroll";

// The pages moved into a frame: the element's name (what retail's controllers find it by) and the end of
// its description's path (the object it was built from), both required.
struct Target {
    const char* name;
    const char* path;   // lower case, compared with the description path's tail
};
const Target kTargets[] = {
    {"graphicsOptions", "options.rsc:graphicsoptions"},   // the Options dialog's Video page
    {"soundOptions", "options.rsc:soundoptions"},         // its Audio page
    {"professionTable", "profession.rsc:professiontable"},  // the Professions dialog's list
};

// The frame's description, loaded once (null when the embedded resource did not load: nothing is wrapped).
const UI::ElementDescription* g_frame = nullptr;
bool g_frameTried = false;

const UI::ElementDescription* Frame() {
    if (g_frameTried) return g_frame;
    g_frameTried = true;
    UI::ElementDescription* d =
        ::Object::ExternalFactory::_singleton->FindOrLoadAs<UI::ElementDescription>(
            System::Filename(kFrameRsc));
    if (!::Object::IsDerivedFromNullSafe(d, UI::ContainerDescription::TypeId())) {
        std::fprintf(stderr, "[PhoneMenu] %s did not load (%p): pages keep retail's frame\n", kFrameRsc,
                     static_cast<const void*>(d));
        return nullptr;
    }
    g_frame = d;
    return g_frame;
}

bool EndsWithNoCase(const char* s, const char* tail) {
    if (s == nullptr) return false;
    const size_t n = std::strlen(s);
    const size_t m = std::strlen(tail);
    if (n < m) return false;
    s += n - m;
    for (size_t i = 0; i < m; ++i) {
        if (std::tolower(static_cast<unsigned char>(s[i])) != tail[i]) return false;
    }
    return true;
}

bool IsTarget(const UI::Element* e) {
    const char* name = e->_name;
    if (name == nullptr || e->_description == nullptr) return false;
    for (const Target& t : kTargets) {
        if (name[0] != t.name[0] || std::strcmp(name, t.name) != 0) continue;
        return EndsWithNoCase(e->_description->_path._data, t.path);
    }
    return false;
}

UI::Element* Child(const UI::Element* e, int32_t index) {
    if (index < 0 || index >= e->_children._size) return nullptr;
    return e->_children._data[index];
}

// The marks retail's own insertions make (AddTab's dirty walk).
void Dirty(UI::Element* e) {
    for (UI::Element* p = e; p != nullptr; p = p->_parent) p->SetNeedsSizeCalc();
}

bool IsFrame(const UI::Element* e) {
    return g_frame != nullptr && e->_description == g_frame && e->IsA(UI::ContainerElement::TypeId());
}

// `page`, child `index` of `parent`, moved into a new frame that takes its place.
int32_t Wrap(UI::Element* parent, int32_t index, UI::Element* page) {
    const UI::ElementDescription* frameDesc = Frame();
    if (frameDesc == nullptr) return 0;
    alignas(16) uint8_t scratch[sizeof(UI::Dialog)];
    UI::Dialog* const dialog = ::new (static_cast<void*>(scratch)) UI::Dialog();
    Object::InternalPtr<UI::Element> made;
    frameDesc->CreateInstance(&made, dialog);
    dialog->UI::Dialog::~Dialog();
    UI::Element* frame = made.Get();
    UI::Element* clip = frame != nullptr ? Child(frame, 0) : nullptr;
    if (clip == nullptr || !clip->IsA(UI::ClipRegionElement::TypeId()) || clip->_children._size < 1) {
        delete frame;
        return 0;
    }
    // The frame's own content (an empty group) gives its place to the page.
    UI::Element* placeholder = clip->_children._data[0];
    delete placeholder;
    clip->_children._data[0] = page;
    page->_parent = clip;
    // The frame's size follows its content; it shrinks to a page shorter than its cap (touch_scroll.h (b)).
    static_cast<UI::ContainerElement*>(frame)->_clampHeight = 1;
    frame->_parent = parent;
    parent->_children._data[index] = frame;
    // The tab shows and hides its page by the slot's hidden bit: the frame carries it now.
    frame->_flags = (frame->_flags & ~UI::kElementFlagHidden) | (page->_flags & UI::kElementFlagHidden);
    page->_flags &= ~UI::kElementFlagHidden;
    page->SetNeedsSizeCalc();
    Dirty(frame);
    return 1;
}

// The frame `frame`, child `index` of `parent`: its page back in its slot, the frame deleted.
int32_t Unwrap(UI::Element* parent, int32_t index, UI::Element* frame) {
    UI::Element* clip = Child(frame, 0);
    UI::Element* page = clip != nullptr ? Child(clip, 0) : nullptr;
    if (page == nullptr) return 0;
    clip->_children._data[0] = nullptr;   // not deleted with the frame
    page->_parent = parent;
    page->_flags = (page->_flags & ~UI::kElementFlagHidden) | (frame->_flags & UI::kElementFlagHidden);
    parent->_children._data[index] = page;
    delete frame;
    page->SetNeedsSizeCalc();
    Dirty(page);
    return 1;
}

int32_t WrapWalk(UI::Element* e, bool on) {
    int32_t changes = 0;
    for (int32_t i = 0; i < e->_children._size; ++i) {
        UI::Element* child = e->_children._data[i];
        if (child == nullptr) continue;
        if (on && IsTarget(child)) {
            // Already framed: its parent is a frame's clip region.
            const UI::Element* above = e->_parent;
            if (above != nullptr && IsFrame(above)) continue;
            changes += Wrap(e, i, child);
            continue;
        }
        if (!on && IsFrame(child)) {
            changes += Unwrap(e, i, child);
            child = e->_children._data[i];
        }
        changes += WrapWalk(child, on);
    }
    return changes;
}

// ---- (b) the lists capped so the window fits ----------------------------------

// A vertical scrolling list: a container with its vertical bar (`_children[1]`).
bool IsVerticalList(const UI::Element* e) {
    if (!e->IsA(UI::ContainerElement::TypeId())) return false;
    const UI::Element* bar = Child(e, 1);
    if (bar == nullptr || !bar->IsA(UI::ScrollElement::TypeId())) return false;
    return static_cast<const UI::ScrollElement*>(bar)->_vertical != 0;
}

constexpr int32_t kMaxLists = 32;
struct Lists {
    UI::ContainerElement* list[kMaxLists] = {};
    int32_t count = 0;
};

// The window's vertical lists (not the lists inside a list's content: they scroll with it).
void Collect(UI::Element* e, Lists& out) {
    if (e == nullptr) return;
    if (IsVerticalList(e)) {
        if (out.count < kMaxLists) out.list[out.count++] = static_cast<UI::ContainerElement*>(e);
        return;
    }
    for (int32_t i = 0; i < e->_children._size; ++i) Collect(e->_children._data[i], out);
}

// The list's height from its description (0: its content's).
int32_t RetailHeight(const UI::ContainerElement* list) {
    const UI::ElementDescription* desc = list->_description;
    if (!::Object::IsDerivedFromNullSafe(desc, UI::ContainerDescription::TypeId()))
        return list->_requestedHeight;
    return static_cast<const UI::ContainerDescription*>(desc)->_requestedHeight;
}

// The height the list would have uncapped.
int32_t NaturalHeight(const UI::ContainerElement* list) {
    const int32_t retail = RetailHeight(list);
    const UI::Element* clip = Child(list, 0);
    const int32_t content = clip != nullptr && clip->IsA(UI::ClipRegionElement::TypeId())
                                ? static_cast<const UI::ClipRegionElement*>(clip)->_innerHeight
                                : retail;
    if (retail <= 0) return content;
    if (list->_clampHeight != 0 && content < retail) return content;
    return retail;
}

// Every list of the window at `cap` (0: none, the description's height). Returns the lists changed.
int32_t ApplyCap(Lists& lists, int32_t cap) {
    int32_t changes = 0;
    for (int32_t i = 0; i < lists.count; ++i) {
        UI::ContainerElement* list = lists.list[i];
        const int32_t retail = RetailHeight(list);
        const int32_t want = cap > 0 && NaturalHeight(list) > cap ? cap : retail;
        if (list->_requestedHeight == want) continue;
        list->_requestedHeight = want;
        Dirty(list);
        ++changes;
    }
    return changes;
}

// The window's padded height (DialogInstance::ComputeLayout's ComputeSize), sized now.
int32_t PaddedHeight(UI::Element* root) {
    root->ComputeSize();
    int32_t h = root->_layoutHeight + root->_topPad + root->_bottomPad;
    if (h < root->_minHeight) h = root->_minHeight;
    return h;
}

// What the cap of each window is, so it is not searched again every frame.
struct Window {
    const UI::DialogInstance* dialog = nullptr;
    const UI::Dialog* description = nullptr;
    const UI::UserInterface* ui = nullptr;   // the UserInterface it is open in (TouchScrollForgetClosed)
    int32_t cap = 0;        // 0: the lists are retail's height
    int32_t settled = -1;   // the padded height the cap was decided at (-1: not yet)
    int32_t room = -1;      // the safe height it was decided for
};
constexpr int32_t kMaxWindows = 16;
Window g_windows[kMaxWindows];
int32_t g_nextWindow = 0;

Window& WindowOf(const UI::DialogInstance* d) {
    for (Window& w : g_windows) {
        if (w.dialog == d && w.description == d->_dialog) return w;
    }
    Window& w = g_windows[g_nextWindow];
    g_nextWindow = (g_nextWindow + 1) % kMaxWindows;
    w = Window();
    w.dialog = d;
    w.description = d->_dialog;
    w.ui = d->_ui;
    return w;
}

bool IsOpen(const UI::UserInterface* ui, const UI::DialogInstance* d) {
    for (auto* node = ui->_dialogs._head; node != nullptr; node = node->next) {
        if (node->element == d) return true;
    }
    return false;
}

// touch_fit.cpp IsWindow's test: a dialog with a description, shown, not a whole-screen picture and not
// sized to the screen; and not a child dialog (its parent places it).
bool IsTopWindow(const UI::DialogInstance* d, UI::Element*& root) {
    root = d->_root.Get();
    if (d->_dialog == nullptr || root == nullptr || d->_parent.Get() != nullptr) return false;
    if (d->_spriteSheet != nullptr && d->_spriteIndex >= 0) return false;
    if ((root->_flags & (UI::kElementFlagHidden | 0x30u)) != 0) return false;
    return true;
}

// The safe height at the player's UI scale (touch_math.h InsetLayout), 0 when unknown.
int32_t SafeHeight(const UI::DialogInstance* d) {
    const UI::UserInterface* ui = d->_ui;
    const ::Video::Device* device = ::Video::Device::_singleton;
    if (ui == nullptr || device == nullptr || ui->_uiScale <= 0.0f) return 0;
    const int32_t w = static_cast<int32_t>(static_cast<float>(device->backBufferWidth) * ui->_uiScale);
    const int32_t h = static_cast<int32_t>(static_cast<float>(device->backBufferHeight) * ui->_uiScale);
    const Rect safe = InsetLayout(w, h);
    return safe.y2 - safe.y1;
}

}  // namespace

void TouchScrollForgetClosed(const UI::UserInterface* ui) {
    for (Window& w : g_windows) {
        if (w.dialog == nullptr || w.ui != ui || IsOpen(ui, w.dialog)) continue;
        w = Window();
    }
}

int32_t TouchScrollWrap(const UI::DialogInstance* dialog, bool on) {
    UI::Element* root = dialog->_root.Get();
    if (root == nullptr) return 0;
    if (on && Frame() == nullptr) return 0;
    if (!on && g_frame == nullptr) return 0;
    return WrapWalk(root, on);
}

int32_t TouchScrollCap(const UI::DialogInstance* dialog, bool on) {
    UI::Element* root = nullptr;
    if (!IsTopWindow(dialog, root)) return 0;
    Lists lists;
    Collect(root, lists);
    if (lists.count == 0) return 0;
    Window& w = WindowOf(dialog);
    if (!on) {
        w.cap = 0;
        w.settled = -1;
        return ApplyCap(lists, 0);
    }
    const int32_t room = SafeHeight(dialog);
    if (room <= 0) return 0;
    int32_t height = PaddedHeight(root);
    if (height == w.settled && room == w.room) return 0;

    // Shrink until the window fits (two lists stacked lose twice what one cap step takes, so a few steps),
    // or grow back what the window no longer needs; a step that grows it past the room is taken back.
    // The lists carry the cap in force before it is searched from (a no-op for the window it was decided on;
    // a window opened again at a closed one's address is forgotten first, TouchScrollForgetClosed).
    int32_t changes = ApplyCap(lists, w.cap);
    if (changes > 0) height = PaddedHeight(root);
    const int32_t before = w.cap;
    const int32_t uncapped = height;
    for (int32_t step = 0; step < 4; ++step) {
        int32_t tallest = 0;
        int32_t natural = 0;
        for (int32_t i = 0; i < lists.count; ++i) {
            const int32_t h = lists.list[i]->_layoutHeight;
            const int32_t n = NaturalHeight(lists.list[i]);
            if (h > tallest) tallest = h;
            if (n > natural) natural = n;
        }
        int32_t cap = w.cap;
        if (height > room) {
            cap = ShrinkCap(w.cap, tallest, height - room, kListFloor);
        } else if (w.cap > 0 && height < room) {
            cap = GrowCap(w.cap, room - height, natural);
        }
        if (cap == w.cap) break;
        const int32_t last = w.cap;
        w.cap = cap;
        changes += ApplyCap(lists, cap);
        const int32_t next = PaddedHeight(root);
        if (next > room && height <= room) {
            // Grown past the room (two stacked lists): the last cap that fit.
            w.cap = last;
            changes += ApplyCap(lists, last);
            height = PaddedHeight(root);
            break;
        }
        height = next;
    }
    // A cap that took nothing off (the height is held by something that does not scroll) is dropped: the
    // lists keep their size and the scale-down fits the window as before.
    if (before == 0 && w.cap > 0 && height > room && height >= uncapped) {
        w.cap = 0;
        changes += ApplyCap(lists, 0);
        height = PaddedHeight(root);
    }
    w.settled = height;
    w.room = room;
    if (w.cap != before) {
        std::fprintf(stderr,
                     "[PhoneMenu] dialog %p: scrolling lists capped at %d units (was %d), %d of %d high\n",
                     static_cast<const void*>(dialog), w.cap, before, height, room);
    }
    return changes;
}

}  // namespace PhoneMenu
}  // namespace Ext
