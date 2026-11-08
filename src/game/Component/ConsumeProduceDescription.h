#pragma once
// ============================================================================
// src/game/Component/ConsumeProduceDescription.h — ENDGAME B.3.8 K1 (B38_RULES.md D1-D5, §2.2)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "ConsumeProduceDescription", parent "Description", ti 0x1401c7388
//   create 0x140032360: MemAlloc(0x58, 8), `??0External@Object@@`, the
//   Description stamp, the leaf stamp, the field defaults. createInPlace 0x1400323a0.
//   table 0x140162b30, 10 slots:
//     [0] 0x1400323e0 scalar deleting dtor   [1] 0x140032350 TypeName   [2] 0x140032340 Type
//     [3] 0x140119200 GetFileVersion = 12   [4] External::Compile (import, inherited)
//     [5] 0x1401190e0 Serialize (`MOV AL,1`)
//     [6] 0x1400323b0 Reconstruct   [7] 0x140001640 Destruct (shared)
//     [8] 0x1400323d0 GetComponentType: ConsumeProduceComponent's TypeId
//     [9] 0x1400323a0 Construct (folded with createInPlace)
//   sizeof 0x58.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Description.h"
#include "Object/exe_type.h"
#include "system/String.h"

class ComponentDescription;             // Component/ComponentDescription.h
namespace UI { class StringTable; }     // ui/StringTable.h

// One accepted input of a recipe (0x10 stride; Serialize 0x1400326f0:
// ExternalPtr<ComponentDescription>, int).
struct ConsumeProduceInput {
    ComponentDescription* material;     // +0x00 the input good's template
    int32_t               count;        // +0x08
    int32_t               _pad0c;       // +0x0c
};
static_assert(sizeof(ConsumeProduceInput) == 0x10, "ConsumeProduceInput stride 0x10");
static_assert(offsetof(ConsumeProduceInput, material) == 0x0, "ConsumeProduceInput::material");
static_assert(offsetof(ConsumeProduceInput, count) == 0x8, "ConsumeProduceInput::count");
static_assert(offsetof(ConsumeProduceInput, _pad0c) == 0xC, "ConsumeProduceInput::_pad0c");

// One recipe (0x28 stride; Serialize 0x1400326f0: the input array, the product's
// ExternalPtr<ComponentDescription>, the uint enum, the name String).
struct ConsumeProduceRecipe {
    ConsumeProduceInput*  inputs;       // +0x00 Array data
    int32_t               inputCount;   // +0x08
    int32_t               inputCap;     // +0x0c
    ComponentDescription* product;      // +0x10 the produced good's template
    uint32_t              materialType; // +0x18 index into the town's material table
    uint32_t              _pad1c;       // +0x1c
    System::String        name;         // +0x20 key into the description's string table
};
static_assert(sizeof(ConsumeProduceRecipe) == 0x28, "ConsumeProduceRecipe stride 0x28");
static_assert(offsetof(ConsumeProduceRecipe, inputs) == 0x0, "ConsumeProduceRecipe::inputs");
static_assert(offsetof(ConsumeProduceRecipe, inputCount) == 0x8, "ConsumeProduceRecipe::inputCount");
static_assert(offsetof(ConsumeProduceRecipe, inputCap) == 0xC, "ConsumeProduceRecipe::inputCap");
static_assert(offsetof(ConsumeProduceRecipe, product) == 0x10, "ConsumeProduceRecipe::product");
static_assert(offsetof(ConsumeProduceRecipe, materialType) == 0x18, "ConsumeProduceRecipe::materialType");
static_assert(offsetof(ConsumeProduceRecipe, _pad1c) == 0x1C, "ConsumeProduceRecipe::_pad1c");
static_assert(offsetof(ConsumeProduceRecipe, name) == 0x20, "ConsumeProduceRecipe::name");

class ConsumeProduceDescription final : public Description {
    DECLARE_EXE_TYPE(ConsumeProduceDescription)

public:
    ConsumeProduceDescription();

    ~ConsumeProduceDescription() override;                                 // [0] 0x1400323e0
    int32_t           GetFileVersion() const override { return 12; }    // [3] 0x140119200
    bool              Serialize(IO::Stream& stream) override;                    // [5] 0x1401190e0
    Object::External* Reconstruct() override;                              // [6] 0x1400323b0
    void              Destruct() override;                                 // [7] 0x140001640
    Object::Type*     GetComponentType(Object::Type* ret) const override;  // [8] 0x1400323d0
    virtual Object::External* Construct();                                 // [9] 0x1400323a0

    // ---- fields (PUBLIC: the components read them at raw offsets today) ----
    UI::StringTable* _stringTable;              // +0x20 ExternalPtr<UI::StringTable> (0x14000bf40): recipe names
    ConsumeProduceRecipe* _recipes;             // +0x28 recipe array data (0x28 stride)
    int32_t _recipeCount;                       // +0x30
    int32_t _recipeCap;                         // +0x34
    uint32_t _messageType;                         // +0x38 Object::Type (the tool's type id; serialized as a name)
    int32_t _messageId;                               // +0x3c
    int32_t _workRequired;                               // +0x40
    float _workTime;                                 // +0x44
    int32_t _maximumProduction;                               // +0x48
    int32_t _defaultRecipe;                     // +0x4c the recipe Startup 0x140118ef0 selects
    uint32_t _toolType;                              // +0x50 enum (serialized through the uint door)
    uint8_t _pad54[4];                          // +0x54 never written by the ctor
};

static_assert(sizeof(ConsumeProduceDescription) == 0x58, "ConsumeProduceDescription is 0x58 (create 0x140032360 MemAlloc(0x58))");
static_assert(offsetof(ConsumeProduceDescription, _pad54) == 0x54, "ConsumeProduceDescription::_pad54");
static_assert(alignof(ConsumeProduceDescription) == 8, "ConsumeProduceDescription alignment");
static_assert(offsetof(ConsumeProduceDescription, _stringTable) == 0x20, "ConsumeProduceDescription::_stringTable @ +0x20");
static_assert(offsetof(ConsumeProduceDescription, _recipes) == 0x28, "ConsumeProduceDescription::_recipes @ +0x28");
static_assert(offsetof(ConsumeProduceDescription, _recipeCount) == 0x30, "ConsumeProduceDescription::_recipeCount @ +0x30");
static_assert(offsetof(ConsumeProduceDescription, _recipeCap) == 0x34, "ConsumeProduceDescription::_recipeCap @ +0x34");
static_assert(offsetof(ConsumeProduceDescription, _messageType) == 0x38, "ConsumeProduceDescription::_messageType @ +0x38");
static_assert(offsetof(ConsumeProduceDescription, _messageId) == 0x3c, "ConsumeProduceDescription::_messageId @ +0x3c");
static_assert(offsetof(ConsumeProduceDescription, _workRequired) == 0x40, "ConsumeProduceDescription::_workRequired @ +0x40");
static_assert(offsetof(ConsumeProduceDescription, _workTime) == 0x44, "ConsumeProduceDescription::_workTime @ +0x44");
static_assert(offsetof(ConsumeProduceDescription, _maximumProduction) == 0x48, "ConsumeProduceDescription::_maximumProduction @ +0x48");
static_assert(offsetof(ConsumeProduceDescription, _defaultRecipe) == 0x4c, "ConsumeProduceDescription::_defaultRecipe @ +0x4c");
static_assert(offsetof(ConsumeProduceDescription, _toolType) == 0x50, "ConsumeProduceDescription::_toolType @ +0x50");
