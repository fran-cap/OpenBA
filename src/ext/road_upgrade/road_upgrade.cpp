// src/ext/road_upgrade/road_upgrade.cpp -- ext.road_upgrade's drag tool and its toolbar button (road_upgrade.h)
//
// NOT RETAIL. docs/extensions/handins/ROADUPGRADE.md. The tool is a DragTool, so the click / drag / second
// click lifecycle, the cost bar, the sounds and the right-click cancel are DragTool::OnKey's and OnCancel's
// (Player/dragtool.cpp). Its description is the template of the road it upgrades to (the stone road's), which
// is what the cost bar shows and what every order is placed from.

#include "ext/road_upgrade/road_upgrade.h"
#include "ext/road_upgrade/road_upgrade_math.h"

#include "ext/registry.h"

#include "Player/dragtool.h"
#include "Player/exe_bodies.h"          // PlayerRay_ScreenToCell_101270, PlayerTileRender_DrawOverlay_08c0
#include "Game/GameUpdateLoop.h"
#include "Game/Map.h"
#include "Game/PlayerToolManager.h"
#include "Game/Toolbar.h"
#include "Game/WorldGeometry.h"
#include "Entity/ComponentEntity.h"      // InternalPtr<ComponentEntity>::Get() needs the class complete
#include "Component/ComponentDescription.h"
#include "Component/CreateRoadDescription.h"
#include "Component/DecalDescription.h"
#include "Component/MapDescription.h"
#include "graphics/Material.h"
#include "collection/Collection.h"
#include "ui/CheckElement.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "Object/factory_registration.h"

#include <cstdint>
#include <cstdio>
#include <new>

namespace {

bool g_running = false;
// The roads group the button was last added to, and the template of the road tool it upgrades to. Both are
// resources the toolbar's items keep loaded for the game; InsertAt sets them again each time the group's
// toolbar is built.
const ::Toolbar* g_group = nullptr;
ComponentDescription* g_target = nullptr;

// The road type a toolbar item's tool lays, or -1 when the item is not a road tool.
int RoadIndexOf(const ::Toolbar* item) {
    if (item == nullptr || item->componentDescription == nullptr) {
        return -1;
    }
    const CreateRoadDescription* road = item->componentDescription->FindDescription<CreateRoadDescription>();
    if (road == nullptr) {
        return -1;
    }
    return static_cast<uint8_t>(road->_index);
}

}  // namespace

// The tool. A game class by shape (the manager owns it as an InternalPtr<Tool>), ours by content.
class RoadUpgradeTool final : public DragTool {
    DECLARE_EXE_TYPE(RoadUpgradeTool)

public:
    RoadUpgradeTool() : DragTool() {}
    ~RoadUpgradeTool() override = default;

    void Startup() override {
        _road = descriptionPtr != nullptr ? descriptionPtr->FindDescription<CreateRoadDescription>() : nullptr;
        g_running = true;
    }
    void Shutdown() override {
        g_running = false;
    }
    void Update(const System::Time* /*time*/, char /*active*/) override {}
    void Draw(Video::Context* context, char active) override;
    bool CalcPath(int startCell, int endCell) override;
    int  CalcCost(int startCell, int endCell, uint32_t* outValid,
                  int* outCostB, int* outCostA, int* outLength) override;

private:
    // Whether the tool upgrades cell (col, row) of `map`.
    bool Wanted(const Map* map, int col, int row) const {
        const MapCell& cell = map->cells.GetData()[map->size * row + col];
        return Ext::RoadUpgrade::Upgradable(cell.roadType, static_cast<uint8_t>(_road->_index),
                                            cell.terrainType, cell.blocker != nullptr);
    }
    // How many cells of the rectangle the tool upgrades.
    int Count(const Map* map, const Ext::RoadUpgrade::Rect& rect) const {
        int count = 0;
        for (int row = rect.minRow; row <= rect.maxRow; ++row) {
            for (int col = rect.minCol; col <= rect.maxCol; ++col) {
                if (Wanted(map, col, row)) {
                    ++count;
                }
            }
        }
        return count;
    }

    const CreateRoadDescription* _road = nullptr;   // the road the tool upgrades to (Startup)
};

// The drag's figures for the cost bar: the number of roads the rectangle would upgrade (shown as one figure,
// and the multiplier of the per-cell cost text, as the road tool's length is). 0 = fine; 3 with the "invalid"
// path type when there is nothing to upgrade, the road tool's own answer for a road it cannot lay.
int RoadUpgradeTool::CalcCost(int startCell, int endCell, uint32_t* outValid,
                              int* outCostB, int* outCostA, int* outLength) {
    *outCostA = 0;
    *outCostB = 0;
    *outValid = 2;
    *outLength = 1;
    const GameUpdateLoop* game = GameUpdateLoop::_singleton;
    if (startCell < 0 || endCell < 0 || _road == nullptr || game == nullptr || game->m_game == nullptr) {
        return 3;
    }
    // A tutorial step that restricts the player names one tool and one place: not this tool's.
    if (dialogPtr != nullptr && dialogPtr->tutorialRestricted) {
        return 3;
    }
    const Map* map = game->m_game;
    const int count = Count(map, Ext::RoadUpgrade::RectOf(startCell, endCell, map->size));
    if (count == 0) {
        return 3;
    }
    *outCostA = count;
    *outCostB = count;
    *outLength = count;
    *outValid = 1;
    return 0;
}

// The release of the drag: one road order per lower road in the rectangle, placed as RoadTool::CalcPath
// (0x140104d30) places its run: the road's decal material recorded for its road type, Map::PlaceRoadCell per
// cell (the BuildRoad work, the pending set, the queued mark), then the road mesh built again.
bool RoadUpgradeTool::CalcPath(int startCell, int endCell) {
    GameUpdateLoop* game = GameUpdateLoop::_singleton;
    if (startCell < 0 || endCell < 0 || _road == nullptr || game == nullptr) {
        return false;
    }
    Map* map = game->m_game;
    WorldGeometry* terrain = game->m_worldGeometry;
    if (map == nullptr || terrain == nullptr) {
        return false;
    }
    if (dialogPtr != nullptr && dialogPtr->tutorialRestricted) {
        return false;
    }
    const DecalDescription* decal = descriptionPtr->FindDescription<DecalDescription>();
    const MapDescription* mapDesc = descriptionPtr->FindDescription<MapDescription>();
    if (decal == nullptr || mapDesc == nullptr) {
        return false;
    }
    const Ext::RoadUpgrade::Rect rect = Ext::RoadUpgrade::RectOf(startCell, endCell, map->size);
    if (Count(map, rect) == 0) {
        return false;
    }
    const uint8_t roadType = static_cast<uint8_t>(_road->_index);
    terrain->StoreBatchMaterial(static_cast<int>(roadType), decal->_materials[0]);
    int placed = 0;
    for (int row = rect.minRow; row <= rect.maxRow; ++row) {
        for (int col = rect.minCol; col <= rect.maxCol; ++col) {
            if (!Wanted(map, col, row)) {
                continue;
            }
            map->PlaceRoadCell(static_cast<uint32_t>(map->size * row + col), roadType,
                               static_cast<uint64_t>(endCell), mapDesc->_cellType, _road->_created);
            ++placed;
        }
    }
    terrain->RebuildRoadMesh(map);
    std::fprintf(stderr, "[RoadUpgrade] %d road cells ordered upgraded in %d x %d\n", placed, rect.Width(),
                 rect.Height());
    return true;
}

// The preview: the rectangle under the drag (the cell under the mouse before it starts), the roads it would
// upgrade as green road tiles and the rest of it as faint plain tiles.
void RoadUpgradeTool::Draw(Video::Context* context, char active) {
    const GameUpdateLoop* game = GameUpdateLoop::_singleton;
    if (active != '\0' || _road == nullptr || game == nullptr || game->m_game == nullptr) {
        return;
    }
    const Map* map = game->m_game;
    int first = startCell;
    int last = endCell;
    if (dragState == DragState::Idle) {
        first = PlayerRay_ScreenToCell_101270(nullptr);
        last = first;
    }
    if (first < 0) {
        return;
    }
    if (last < 0) {
        last = first;
    }
    const Ext::RoadUpgrade::Rect rect = Ext::RoadUpgrade::RectOf(first, last, map->size);
    Collection::Array<ToolTileRecord> tiles;
    for (int row = rect.minRow; row <= rect.maxRow; ++row) {
        for (int col = rect.minCol; col <= rect.maxCol; ++col) {
            const bool wanted = Wanted(map, col, row);
            ToolTileRecord tile;
            tile.col = col;
            tile.row = row;
            tile.variant = wanted ? 1 : 0;                    // 1: the road tool's tile
            tile.color = wanted ? 0xff00ff00 : 0x60ffffff;    // ARGB
            tile.angle = 0;
            tiles.SetSize(tiles._size + 1, tile);
        }
    }
    Collection::Array<ToolSegmentRecord> none;
    PlayerTileRender_DrawOverlay_08c0(nullptr, context, &tiles, &none);
}

::Object::Base* RoadUpgradeTool::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(RoadUpgradeTool), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) RoadUpgradeTool());
}
::Object::Base* RoadUpgradeTool::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) RoadUpgradeTool());
}

DEFINE_EXE_TYPE(RoadUpgradeTool, "DragTool")

namespace Ext {
namespace RoadUpgrade {

namespace {

// Installs the tool as the player's current tool: what PlayerToolManager::SelectTool does
// (Dialog/toolbardialog.cpp) without the registry lookup, as ext.terrain_edit's StartBrush.
void StartTool(PlayerToolManager* manager) {
    manager->activeToolGroup = nullptr;   // the group's own buttons release themselves
    if (manager->currentTool != nullptr) {
        manager->currentTool->Shutdown();
        delete manager->currentTool.Get();
        manager->currentTool = nullptr;
    }
    Tool* tool = static_cast<Tool*>(RoadUpgradeTool::CreateNewObject());
    if (tool == nullptr) {
        return;
    }
    tool->dialogPtr      = manager;
    tool->descriptionPtr = g_target;
    tool->toolSetting    = nullptr;
    manager->currentTool = tool;
    tool->Startup();
}

}  // namespace

int InsertAt(const ::Toolbar* group, const ::Toolbar* const* items, int n) {
    if (!Ext().road_upgrade || group == nullptr || n <= 0 || n > 64) {
        return -1;
    }
    int roadIndex[64];
    for (int i = 0; i < n; ++i) {
        roadIndex[i] = RoadIndexOf(items[i]);
    }
    const int target = PickTarget(roadIndex, n);
    if (target < 0) {
        return -1;
    }
    g_group = group;
    g_target = items[target]->componentDescription;
    return target + 1;
}

bool OwnsGroup(const ::Toolbar* group) {
    return group != nullptr && group == g_group && g_target != nullptr;
}

ComponentDescription* Target() {
    return g_target;
}

void Click(UI::CheckElement* button) {
    const GameUpdateLoop* game = GameUpdateLoop::_singleton;
    if (button == nullptr || game == nullptr || game->m_toolManager == nullptr) {
        return;
    }
    PlayerToolManager* manager = game->m_toolManager;
    if (button->_checked == 0) {
        manager->SelectTool(nullptr);   // released: the game's own tool, as a group button's release
        return;
    }
    if (g_target == nullptr || manager->tutorialRestricted) {
        button->_checked = 0;           // a tutorial step that restricts the player keeps its own tool
        return;
    }
    StartTool(manager);
}

void SyncButton(UI::CheckElement* button) {
    if (button != nullptr && button->_checked != 0 && !g_running) {
        button->_checked = 0;
    }
}

bool Running() {
    return g_running;
}

}  // namespace RoadUpgrade
}  // namespace Ext
