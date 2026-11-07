// ============================================================================
// tests/unit/ext_rsc_alignment_test.cpp -- the .rsc alignment names against the engine enum
// ============================================================================
// src/ext/resources/text_enums.cpp spells the nine `Alignment` identifiers our
// .rsc text may use with their values; UI::ElementDescription::Alignment
// (src/engine/ui/ElementDescription.h) names the same values with retail's names.
// The table keeps literal values (its object stays free of the engine header), so
// this test is what holds the two definitions equal.
// ============================================================================
#include "unit.h"
#include "ext/resources/text_enums.h"
#include "ui/ElementDescription.h"

#include <cstdint>

UNIT_TEST(ext_rsc_alignment_names_match_the_engine_enum) {
    struct Row {
        const char*                       name;
        UI::ElementDescription::Alignment value;
    };
    const Row rows[] = {
        { "TopLeft", UI::ElementDescription::TopLeft },
        { "TopCenter", UI::ElementDescription::TopCenter },
        { "TopRight", UI::ElementDescription::TopRight },
        { "MidLeft", UI::ElementDescription::MidLeft },
        { "MidCenter", UI::ElementDescription::MidCenter },
        { "MidRight", UI::ElementDescription::MidRight },
        { "BottomLeft", UI::ElementDescription::BottomLeft },
        { "BottomCenter", UI::ElementDescription::BottomCenter },
        { "BottomRight", UI::ElementDescription::BottomRight },
    };
    for (const Row& r : rows) {
        uint32_t v = 0xffffffffu;
        CHECK(Ext::RscText::LookupEnum(Ext::RscText::kAlignment, r.name, v) &&
              v == static_cast<uint32_t>(r.value));
    }
}
