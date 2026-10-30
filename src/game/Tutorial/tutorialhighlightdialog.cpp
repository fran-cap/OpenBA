// src/game/Tutorial/tutorialhighlightdialog.cpp
// TutorialHighlightDialog — the glow ring and bobbing arrow over the thing the
// current tutorial step wants clicked.
//
// TutorialHighlightDialog (0xF0 bytes), table 0x1401609e0, on UI::DialogInstance.
// ENDGAME B.3.8 block GOAL: a real class (Tutorial/TutorialHighlightDialog.h);
// the hand-built 48-slot table and the hand-rolled ctor in
// tutorial_dialog_vtables.cpp are gone.

// volume/Volume.h FIRST: its global-scope `struct Rectangle` is hidden by <wingdi.h>'s
// `Rectangle()` once <windows.h> has been seen.
#include "volume/Volume.h"
#include "Tutorial/TutorialHighlightDialog.h"
#include "ui/UserInterface.h"
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/String.h"
#include "system/Value.h"   // ENDGAME B.3.4b System callers: System::Time
#include "object/Internal.h"   // Object::detail::InternalMemAlloc / InternalMemFree
#include "object/VSlot.h"      // B.3.x W0: the typeName probe's slot
#include "ui/DialogInstance.h"   // ENDGAME B.3.6 UI callers (K4)
#include "ui/Element.h"                       // ENDGAME B.3.6 UI callers (K4)
#include "ui/Sprite.h"                        // ENDGAME B.3.6 UI callers (K4)
#include "ui/ImageElement.h"                  // ENDGAME B.3.6 UI callers (K4)
#include <new>
#include <cstdint>
#include "video/Device.h"   // ENDGAME B.3.9 SS2: Video::Device::_singleton

// ============================================================
// Construction / destruction (ENDGAME B.3.8 block GOAL)
// ============================================================
// 0x1400e0290 (create 0x1400278c0 runs it): DialogInstance() (the base's
// default ctor), the stamp (the compiler's), then +0xb8..+0xe8 zeroed.
TutorialHighlightDialog::TutorialHighlightDialog()
    : elapsedTicks(0), glowGroup(nullptr), glow(nullptr), arrow(nullptr),
      rotation(0), targetElement(nullptr), targetEntity(nullptr) {}

// 0x1400e01d0: DialogInstance(ui, dialog, parent, 0, 0, 0), the stamp, the
// fields zeroed, then +0xd8 = rotation, +0xe0 = *targetElement,
// +0xe8 = *targetEntity (retail passes the two handles by address; the caller
// dereferences them here). The tree's hand-rolled copy skipped the base ctor
// when `dialog` was null; retail never does.
TutorialHighlightDialog::TutorialHighlightDialog(UI::UserInterface* ui, const UI::Dialog* dialog,
                                                 Object::InternalPtr<UI::DialogInstance> parent,
                                                 UI::Element* targetElementArg, int32_t rotationArg,
                                                 ComponentEntity* targetEntityArg)
    : UI::DialogInstance(ui, dialog, parent, nullptr, nullptr, 0),
      elapsedTicks(0), glowGroup(nullptr), glow(nullptr), arrow(nullptr),
      rotation(rotationArg), targetElement(targetElementArg), targetEntity(targetEntityArg) {}

// ============================================================
// TutorialHighlightDialog::OnOpened (vtable +0x20, 0x1400e0020)
// ============================================================
// Finds "glowGroup", "glow", "arrow" by name and keeps them at +0xC0, +0xC8, +0xD0.
// The two images are kept as the elements found (retail stores the
// InternalPtr<Element> into the ImageElement slots unchecked).
void TutorialHighlightDialog::OnOpened() {
    {
        System::String nameStr("glowGroup");
        glowGroup = FindElement(nameStr).Get();
    }
    {
        System::String nameStr("glow");
        glow = static_cast<UI::ImageElement*>(FindElement(nameStr).Get());
    }
    {
        System::String nameStr("arrow");
        arrow = static_cast<UI::ImageElement*>(FindElement(nameStr).Get());
    }
}

// ============================================================
// TutorialHighlightDialog::OnUpdate (vtable +0x38, 0x1400e0000)
// ============================================================
// Retail is 11 bytes: MOV RAX,[RDX]; ADD [RCX+0xB8],RAX; RET — the frame's
// tick count added to the elapsed time the arrow's bob reads.
void TutorialHighlightDialog::OnUpdate(const System::Time& time) {
    elapsedTicks += time._ticks;
}

// ===========================================================================
// The two positioning helpers behind slot 6, and slot 6 itself.
// Field map (TutorialHighlightDialog, 0xF0, base UI::DialogInstance):
//   +0x10  UI::UserInterface*        (base) — +0xDC on it is the UI scale
//   +0xB8  System::Time              accumulated dt (slot 7 adds to it)
//   +0xC0  IBP UI::Element*          "glowGroup"
//   +0xC8  IBP UI::ImageElement*     "glow"
//   +0xD0  IBP UI::ImageElement*     "arrow"
//   +0xD8  int32                     arrow rotation, degrees (0/90/180/270)
//   +0xE0  IBP UI::Element*          UI-element target   (element-path branch)
//   +0xE8  IBP entity                world-entity target (toolbar-cell branch)
// Constants read out of .rdata: 0x140172114 = 0.5f, 0x140172184 = pi,
// 0x1401721a8 = 8.0f. FUN_140131c90 is sinf (an inlined CRT sin), so the
// arrow's bob is  sinf(elapsedSeconds * pi) * 8  pixels.
// ===========================================================================

#include "Action/exe_bodies.h"      // 0x140003f60 ActCNR_FindComponent_3f60
#include "Component/exe_bodies.h"   // 0x140004680 FloorComponent_FindResData
#include "Game/exe_bodies.h"        // 0x14009f3e0 Reimpl_MapGetTerrainPos
#include "Player/exe_bodies.h"      // 0x140100380 PlayerScreenRect_Entity_0380
#include "Object/factory_registration.h"
#include "Entity/ComponentEntity.h"
#include "Component/ModelComponent.h"
#include "Component/MapComponent.h"
#include "Game/Map.h"
#include "volume/Volume.h"
#include "crt/msvc_libm.h"   // MsvcLibm::Sinf (the exe's sinf 0x140131c90)
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>

namespace {

// Volume::Rectangle::Rectangle() is RtVolume_Rectangle_Ctor; 0x140100380 — entity
// screen bounding rectangle — is PlayerScreenRect_Entity_0380 (ported in
// src/game/Player/tool_base_screenrect.cpp). param_1 is unused by the body; retail
// passes the rectangle itself there.

// sinf(elapsed * pi) * 8 — the shared bob offset both helpers apply.
float ArrowBob(const TutorialHighlightDialog* self) {
    System::Time elapsed{};
    elapsed._ticks = self->elapsedTicks;
    float t = elapsed.AsSeconds();
    return MsvcLibm::Sinf(t * 3.14159274f) * 8.0f;
}

}  // namespace

// ---------------------------------------------------------------------------
// 0x1400DFAC0 — the UI-ELEMENT branch (+0xE8 == 0, target element at +0xE0).
// Grows the glow ring to the element's rect + 13px on every side, parks the
// glowGroup with a four-sided padding measured against the scaled screen, then
// puts the arrow on the corner selected by the rotation field.
// ---------------------------------------------------------------------------
// Row 89 (B7 RES): 0x1400dfac0 tests nothing -- the target element (+0xe0), glow
// (+0xc8), the Video::Device singleton, the UserInterface (+0x10), glowGroup (+0xc0)
// and arrow (+0xd0) are each CheckPtr'd and read straight; the tree's null tests are gone.
void TutorialHighlightDialog::PlaceOnElement() {
    UI::Element* elem = targetElement;

    // Element layout rect: +0x68 left, +0x6C top, +0x70 right, +0x74 bottom.
    int left   = elem->_layoutLeft;
    int top    = elem->_layoutTop;
    int width  = elem->_layoutRight - left;
    int height = elem->_layoutBottom - top;

    glow->SetWidthHeight(width + 0x1A, height + 0x1A);

    int padLeft = left - 13;
    int padTop  = top  - 13;

    // Screen size from the display singleton (+4 width, +8 height), scaled by
    // the UserInterface's +0xDC factor.
    // Video::Device is behind a DOUBLE deref of the 0x140151a80 exref
    //   MOV RAX,[0x140151a80] ; MOV RCX,[RAX] ; MOVD XMM0,[RCX+4]
    // — one deref short lands on the holder and reads garbage dimensions, which
    // is exactly what mis-sizes and mis-places the glow ring.
    const ::Video::Device* device = ::Video::Device::_singleton;   // 0x1400dfb74-0x1400dfb85
    int screenW = device->backBufferWidth;
    int screenH = device->backBufferHeight;
    float scale = _ui->_uiScale;                                     // 0x1400dfb7e / 0x1400dfb8a

    glowGroup->SetPadding(padLeft,
                          (static_cast<int>(static_cast<float>(screenW) * scale) - padLeft) - width - 0x1A,
                          padTop,
                          (static_cast<int>(static_cast<float>(screenH) * scale) - padTop) - height - 0x1A);
    glowGroup->SetNeedsSizeCalc();

    // Arrow: corner + sign pair chosen by the rotation.
    UI::ImageElement* arrowElem = arrow;

    UI::Sprite sprite = arrowElem->GetSprite();
    int arrowW = sprite.Width();

    int rot = rotation;
    int x, y, signX, signY;
    if (rot == 90) {
        x = left - arrowW;                                    signX = 1;
        y = elem->_layoutBottom;                              signY = -1;
    } else if (rot == 180) {
        x = elem->_layoutRight;                               signX = -1;
        y = elem->_layoutBottom;                              signY = -1;
    } else if (rot == 270) {
        x = elem->_layoutRight;                               signX = -1;
        y = top - arrowW;                                     signY = 1;
    } else {
        x = left - arrowW;                                    signX = 1;
        y = top - arrowW;                                     signY = 1;
    }

    float bob = ArrowBob(this);
    arrowElem->SetPadding(static_cast<int>(static_cast<float>(signX) * bob + static_cast<float>(x)), 0,
                          static_cast<int>(static_cast<float>(signY) * bob + static_cast<float>(y)), 0);
    arrowElem->SetRotation(static_cast<float>(-rot));
}

// ---------------------------------------------------------------------------
// 0x1400DFDA0 — the WORLD-ENTITY branch (+0xE8 holds an entity IBP).
// The glow ring is meaningless on a world object, so the group is hidden and
// only the arrow is placed, over the centre of the entity's screen rect. When
// the entity has no projectable rect (off screen / no model) the arrow hides
// too — that is the tutorial's "the thing I am pointing at is not visible".
// ---------------------------------------------------------------------------
namespace {

// The FindComponent<T> instances the screen-rect path uses: 0x140003f60 ->
// ModelComponent, 0x140004680 -> MapComponent.
// 0x14009f3e0 Reimpl_MapGetTerrainPos — grid cell -> world position (real terrain z).

// The diagnostic's reprint-on-change filter (main thread only: OnUpdateLayout).
const ComponentEntity* s_diagLastEntity = nullptr;
int      s_diagLastRect[4] = { -12345, 0, 0, 0 };

void TutHLDiag(const ComponentEntity* entity, bool ok, const Rectangle& rect) {

    ModelComponent* model = entity->FindComponent<ModelComponent>();   // 0x140003f60
    MapComponent* map = entity->FindComponent<MapComponent>();         // 0x140004680

    int16_t placed = -1;
    float xf[7] = {0,0,0,0,0,0,0};
    float box[6] = {0,0,0,0,0,0};
    if (model) {
        placed = model->meshIndex;
        const Math::Transform& t = model->transform;
        xf[0] = t.rotation.x; xf[1] = t.rotation.y; xf[2] = t.rotation.z; xf[3] = t.rotation.w;
        xf[4] = t.translation.x; xf[5] = t.translation.y; xf[6] = t.translation.z;
        const ::Box& b = model->meshInstance._localBounds;
        box[0] = b.min.x; box[1] = b.min.y; box[2] = b.min.z;
        box[3] = b.max.x; box[4] = b.max.y; box[5] = b.max.z;
    }

    int rx = 0, ry = 0, rw = 0, rh = 0;
    if (map) {
        rx = map->tileX0;
        ry = map->tileY0;
        rw = map->tileX1;
        rh = map->tileY1;
    }

    // Footprint centre through the same grid->world conversion the outline uses.
    int   g = 0;
    float centreWorld[3] = {0,0,0};
    if (GameUpdateLoop::_singleton != nullptr) {
        Map* grid = GameUpdateLoop::_singleton->m_game;
        if (grid) {
            g = grid->size;
            float bias = static_cast<float>(g >> 1) - 0.5f;
            float in[3] = { static_cast<float>(rx + (rw >> 1)) - bias,
                            static_cast<float>(ry + (rh >> 1)) - bias, 0.0f };
            grid->GetTerrainPos(centreWorld, in, '\0', nullptr);   // 0x14009f3e0
        }
    }

    // Only reprint when something actually changes.
    // The reprint-on-change filter lives at namespace scope (s_diagLast*).
    const ComponentEntity*& lastEntity = s_diagLastEntity;
    int* lastRect = s_diagLastRect;
    if (entity == lastEntity && rx == lastRect[0] && ry == lastRect[1] &&
        rw == lastRect[2] && rh == lastRect[3]) return;
    lastEntity = entity;
    lastRect[0] = rx; lastRect[1] = ry; lastRect[2] = rw; lastRect[3] = rh;

    // Type names so we know we are looking at the components we think we are,
    // and the MeshInstance's OWN transform (model +0x28, +0x84) — that is what
    // the renderer actually draws from, so it is the oracle for what +0x148
    // should have held.
    auto typeName = [](const Object::Base* obj) -> const char* {
        if (!obj) return "(null)";
        const char* n = ::Object::GetTypeNameById(obj->Type());   // slot 2
        return n ? n : "(unknown id)";
    };
    // The entity's whole component roster (+0x30 array / +0x38 count). Print
    // EVERY entry with its identifying payload: FindComponent-by-type returns
    // the FIRST match, so if a type appears twice the lookups and the renderer
    // can be looking at different objects.
    {
        int n = entity->_components._size;
        Object::InternalPtr<Component>* arr = entity->_components._data;
        std::fprintf(stderr, "[TutHLDiag] entity=%p roster count=%d\n",
                     static_cast<const void*>(entity), n);
        for (int i = 0; i < n && arr; ++i) {
            Component* c = arr[i].Get();
            const char* nm = typeName(c);
            char extra[128]; extra[0] = 0;
            if (c && std::strcmp(nm, "MapComponent") == 0) {
                const MapComponent* mc = static_cast<const MapComponent*>(c);
                std::snprintf(extra, sizeof(extra), " cell=(%u,%u)..(%u,%u)",
                    static_cast<uint16_t>(mc->tileX0), static_cast<uint16_t>(mc->tileY0),
                    static_cast<uint16_t>(mc->tileX1), static_cast<uint16_t>(mc->tileY1));
            } else if (c && std::strcmp(nm, "ModelComponent") == 0) {
                const ModelComponent* mo = static_cast<const ModelComponent*>(c);
                std::snprintf(extra, sizeof(extra), " pos=(%.2f,%.2f,%.2f) variant=%d modelId=%d",
                    mo->transform.translation.x, mo->transform.translation.y,
                    mo->transform.translation.z,
                    (int)mo->meshIndex, mo->modelId);
            }
            std::fprintf(stderr, "[TutHLDiag]   [%2d] %p %s%s\n",
                         i, static_cast<void*>(c), nm, extra);
        }
        std::fflush(stderr);
    }
    float mi[7] = {0,0,0,0,0,0,0};
    if (model) {
        const Math::Transform& t = model->meshInstance._transform._current;
        mi[0] = t.rotation.x; mi[1] = t.rotation.y; mi[2] = t.rotation.z; mi[3] = t.rotation.w;
        mi[4] = t.translation.x; mi[5] = t.translation.y; mi[6] = t.translation.z;
    }
    std::fprintf(stderr,
        "[TutHLDiag] names: entity=%s model=%s map=%s  meshInstXform pos=(%.2f,%.2f,%.2f)"
        " quat=(%.3f,%.3f,%.3f,%.3f)\n",
        typeName(entity), typeName(model), typeName(map),
        mi[4], mi[5], mi[6], mi[0], mi[1], mi[2], mi[3]);


    std::fprintf(stderr,
        "[TutHLDiag] entity=%p model=%p placed(+0x166)=%d map=%p\n"
        "            modelXform pos=(%.2f,%.2f,%.2f) quat=(%.3f,%.3f,%.3f,%.3f)\n"
        "            modelBox min=(%.2f,%.2f,%.2f) max=(%.2f,%.2f,%.2f)\n"
        "            footprint cell=(%d,%d) %dx%d  grid=%d  centreWorld=(%.2f,%.2f,%.2f)\n"
        "            branch=%s screenRect ok=%d (%.1f,%.1f)-(%.1f,%.1f)\n",
        static_cast<const void*>(entity), static_cast<void*>(model), (int)placed,
        static_cast<void*>(map),
        xf[4], xf[5], xf[6], xf[0], xf[1], xf[2], xf[3],
        box[0], box[1], box[2], box[3], box[4], box[5],
        rx, ry, rw, rh, g, centreWorld[0], centreWorld[1], centreWorld[2],
        (placed >= 0) ? "MODEL-BOX" : "FOOTPRINT-FALLBACK",
        (int)ok, rect.min.x, rect.min.y, rect.max.x, rect.max.y);
    std::fflush(stderr);
}

}  // namespace

// Row 89 (B7 RES): 0x1400dfda0 tests nothing but the screen-rect result
// (0x1400dfdfa); glowGroup (+0xc0) and arrow (+0xd0) are read straight.
void TutorialHighlightDialog::PlaceOnEntity() {
    glowGroup->SetFlagsOnOff(1, 0);   // hide

    // Volume::Rectangle rect; ok = EntityScreenRect(rect, &entityIBP, &rect)
    alignas(16) struct Rectangle rect;
    RtVolume_Rectangle_Ctor(&rect);
    ComponentEntity* entityIBP = targetEntity;
    bool ok = PlayerScreenRect_EntityRect_0380(&rect, &entityIBP, &rect) != 0;   // 0x140100380

    // [diag] BANISHED_TUTHL_DIAG — where the arrow thinks the target is.
    //
    // EntityScreenRect (0x140100380) has TWO sources: the ModelComponent's own
    // world Transform + Box (taken when modelComp+0x166 >= 0), else a FALLBACK
    // that rebuilds four corners from the MapComponent footprint rect. The
    // ground outline (Impl_b930) is built from that same footprint rect, so a
    // wrong rect misplaces BOTH markers identically while the model itself
    // still draws from its Transform — exactly the symptom. Print both sources
    // side by side; whichever disagrees with the drawn building is the culprit.
    // Rate-limited by CHANGE (the entity + rect tuple), never by a fixed budget.
    TutHLDiag(entityIBP, ok, rect);

    UI::ImageElement* arrowElem = arrow;
    if (!ok) {
        arrowElem->SetFlagsOnOff(1, 0);   // hide
        return;
    }
    arrowElem->SetFlagsOnOff(0, 1);                  // show

    UI::Sprite sprite = arrowElem->GetSprite();
    int arrowW = sprite.Width();
    sprite = arrowElem->GetSprite();
    int arrowH = sprite.Height();

    // Rectangle is four floats: minX, minY, maxX, maxY.
    float centreX = (rect.min.x + rect.max.x) * 0.5f;
    float centreY = (rect.min.y + rect.max.y) * 0.5f;

    float bob = ArrowBob(this);
    // Retail multiplies the horizontal bob by 0.0 — the arrow bobs vertically
    // only. Kept literal rather than "simplified" away: the term is in the
    // shipped code and dropping it would hide a future edit to that constant.
    arrowElem->SetPadding(static_cast<int>(static_cast<float>(static_cast<int>(centreX) - (arrowW >> 1)) + bob * 0.0f), 0,
                          static_cast<int>(static_cast<float>(static_cast<int>(centreY) - arrowH) + bob), 0);
    arrowElem->SetRotation(static_cast<float>(-rotation));
}

// TutorialHighlightDialog::OnUpdateLayout (0x1400DFFE0) — vtable slot 6.
// Retail is the dispatch plus an inlined copy of the 0x1400DFDA0 body.
void TutorialHighlightDialog::OnUpdateLayout() {
    if (targetEntity == nullptr)
        PlaceOnElement();
    else
        PlaceOnEntity();
}

// create 0x1400278c0: MemAlloc(0xf0, 8), then the default ctor 0x1400e0290.
::Object::Base* TutorialHighlightDialog::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(TutorialHighlightDialog), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(::new (p) TutorialHighlightDialog());
}
// Retail's createInPlace 0x140027900 does not build a dialog in place (B8-13);
// the tree's in-place entry did, and that is kept.
::Object::Base* TutorialHighlightDialog::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(::new (storage) TutorialHighlightDialog());
}

// DEFINE_EXE_TYPE(TutorialHighlightDialog, "UI::DialogInstance") stays where
// its REGISTER_TYPE was — tutorial_dialog_vtables.cpp (B38_RULES B8-7).
