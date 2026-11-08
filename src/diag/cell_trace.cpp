// src/diag/cell_trace.cpp -- map-cell diagnostics for the "red square" reports (cell_trace.h).
#include "cell_trace.h"
#include "options.h"
#include "err_line.h"
#include "Game/Map.h"
#include "Entity/ComponentEntity.h"
#include "Component/ComponentDescription.h"
#include "system/Filename.h"
#include <cstdio>
#include <string>

namespace {

// The entity's description file name ("WoodHouse", "Tree02", ...), or "-" for none.
std::string EntityName(const ComponentEntity* entity) {
    if (entity == nullptr) return "-";
    const ComponentDescription* description = entity->_description;
    if (description == nullptr) return "?";
    System::Filename path = description->GetPath();
    System::Filename name = path.GetPathFilename(true);
    const char* text = name.AsString()._data;
    return (text != nullptr && text[0] != '\0') ? text : "?";
}

int CellIndex(const Map* map, const MapCell* cell) {
    return static_cast<int>(cell - map->cells.GetData());
}

}  // namespace

namespace CellTrace {

bool Enabled() {
    return Opt().cellTrace;
}

void NoteCount(const Map* map, const MapCell* cell, uint8_t before, int delta, const char* site) {
    if (!Opt().cellTrace) return;
    const int after = before + delta;
    if (after >= 0 && after <= 2) return;
    const int index = CellIndex(map, cell);
    ErrLine("[CellTrace] count %s cell=%d (%d,%d) %u%+d -> %u terrain=%u road=%02x blocker=%s\n",
            site, index, index % map->size, index / map->size, before, delta,
            static_cast<unsigned>(static_cast<uint8_t>(after)), static_cast<unsigned>(cell->terrainType),
            cell->roadType,
            EntityName(cell->blocker.Get()).c_str());
}

void NoteResort(const Map* map, int cellIndex, uint8_t before, uint8_t after) {
    if (!Opt().cellTrace) return;
    if ((before != 2 && before != 4) || after == 2 || after == 4) return;   // buildable or a resource lost
    const MapCell* cell = &map->cells.GetData()[cellIndex];
    ErrLine("[CellTrace] resort cell=%d (%d,%d) terrain %u -> %u count=%u road=%02x\n",
            cellIndex, cellIndex % map->size, cellIndex / map->size, before, after,
            cell->footprintCount, cell->roadType);
}

void DumpCells(const Map* map, const char* stage, int version) {
    const char* dir = Opt().cellDump;
    if (dir == nullptr || dir[0] == '\0') return;
    const std::string path = std::string(dir) + "/cells_" + stage + ".txt";
    FILE* out = std::fopen(path.c_str(), "w");
    if (out == nullptr) {
        ErrLine("[CellDump] cannot write %s\n", path.c_str());
        return;
    }
    const int size = map->size;
    const int count = map->cells.GetSize();
    std::fprintf(out, "# cells stage=%s size=%d count=%d version=%d\n", stage, size, count, version);
    std::fprintf(out, "# index x y count terrain road roadState growth floor zone blocker occupant class height slope\n");
    // class / height / slope: what the classifier 0x14009e2c0 says for the cell today (the type
    // a re-sort would write), from the same terrain query it makes (0x14009f3e0).
    Map* query = const_cast<Map*>(map);
    const float half = static_cast<float>(size >> 1) - 0.5f;
    const MapCell* cells = map->cells.GetData();
    for (int i = 0; i < count; ++i) {
        const MapCell& cell = cells[i];
        float inXY[2] = {static_cast<float>(i % size) - half, static_cast<float>(i / size) - half};
        float pos[3] = {0, 0, 0};
        float tri[3] = {0, 0, 0};
        query->GetTerrainPos(pos, inXY, '\x01', tri);
        std::fprintf(out, "%d %d %d %u %u %u %u %u %d %u %s %s %d %.4f %.4f\n",
                     i, i % size, i / size, cell.footprintCount, static_cast<unsigned>(cell.terrainType),
                     cell.roadType,
                     cell.roadAlpha, cell.soilQuality, cell.floorCount, cell.zone,
                     EntityName(cell.blocker.Get()).c_str(), EntityName(cell.occupant.Get()).c_str(),
                     query->ClassifyTerrain(i), pos[2], tri[2]);
    }
    std::fclose(out);
    ErrLine("[CellDump] wrote %s (%d cells)\n", path.c_str(), count);
}

}  // namespace CellTrace
