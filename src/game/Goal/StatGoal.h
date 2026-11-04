#pragma once
// ============================================================================
// src/game/Goal/StatGoal.h — ENDGAME B.3.8 block GOAL (B38_RULES D1-D5, D8)
//
// An achievement goal that compares a game statistic against a constant or a
// second statistic.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md section 6):
//   reflected "StatGoal", parent "Achievement::Goal", ti 0x1401c62b0
//   create 0x1400264d0 (MemAlloc(0x80, 8)), createInPlace 0x140026510
//   ctor 0x1400daa80
//   table 0x1401604c0, 11 slots (Ghidra's labels on it are wrong):
//     [0] 0x140026540 scalar deleting dtor: stamp, ~String +0x60, ~String +0x48,
//         ~Goal, MemFree on &1
//     [1] TypeName  [2] Type  [3] 0x1400daa10 GetFileVersion `MOV EAX,1`
//     [4] External::Compile (inherited)  [5] 0x1400da8b0 Serialize
//     [6] 0x140026520 Reconstruct  [7] 0x140001640 Destruct
//     [8] 0x1400da720 CheckGoalState
//     [9] Goal::GetInstanceType (inherited)  [10] 0x140026510 Construct
//   sizeof 0x80.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include "achievement/Goal.h"
#include "Object/exe_type.h"
#include "Goal/goal_exe_helpers.h"   // NamedExternalRef

class StatGoal final : public Achievement::Goal {
    DECLARE_EXE_TYPE(StatGoal)

public:
    StatGoal();                                                    // 0x1400daa80

    ~StatGoal() override;                                          // [0] 0x140026540
    int32_t           GetFileVersion() const override { return 1; }// [3] 0x1400daa10
    bool              Serialize(IO::Stream& stream) override;            // [5] 0x1400da8b0
    Object::External* Reconstruct() override;                      // [6] 0x140026520
    void              Destruct() override;                         // [7] 0x140001640
    GoalState         CheckGoalState() const override;             // [8] 0x1400da720
    virtual Object::External* Construct();                         // [10] 0x140026510

    // Not retail: the two operands CheckGoalState compares (diagnostics).
    void ComputeOperands(int& statValueA, int& comparisonValue) const;

    // 0x1400da4e0: the stat value of `statType` (0..0x1d) for the resolved
    // External `ext` (statvaluedispatch.cpp). Retail hands RCX = this to every
    // helper; the ones that read a StatGoal field are the members below.
    uint32_t StatValue(uint32_t statType, const Object::External* ext) const;

    // SumProductionWindow's (0x1400d8c50) window ends, in the 48-per-year series
    // slots: lo = ((year - statSubType) - 1) * 0x30, hi = year * 0x30 - 0x30 + frac.
    // Retail: `MOV EAX,[RDI]; SUB EAX,[RBP+0x7c]; DEC EAX; LEA R8D,[RAX+RAX*2]; SHL R8D,4`
    // and `LEA R9D,[RBX+RBX*2]; SHL R9D,4; ADD R9D,-0x30`, 32-bit wrapping arithmetic.
    // NOT RETAIL (review F-165): done in uint32_t and cast back, so a statSubType from an
    // achievement resource (or a year from a save) far enough out to wrap gives retail's
    // wrapped value instead of int32 signed overflow (undefined behaviour in C++).
    static constexpr int32_t ProductionWindowLo(int32_t year, int32_t yearsBack) {
        return static_cast<int32_t>((static_cast<uint32_t>(year) - static_cast<uint32_t>(yearsBack) - 1u) * 0x30u);
    }
    static constexpr int32_t ProductionWindowHi(int32_t year, int32_t frac) {
        return static_cast<int32_t>(static_cast<uint32_t>(year) * 0x30u - 0x30u + static_cast<uint32_t>(frac));
    }

private:
    uint32_t SumStorageAmounts(const Object::External* ext) const;        // 0x1400d9650
    uint32_t CountDistinctTracked(const Object::External* ext) const;     // 0x1400d8d70
    uint32_t SumProductionWindow() const;                                 // 0x1400d8c50
    uint32_t SumTrackedForProfession(const Object::External* ext) const;  // 0x1400d88e0
    uint32_t ReadSubTypeEntry() const;                                    // 0x1400d86a0

public:
    // H2: the serializer overloads retail calls (0x1400da8b0): Serialize(unsigned
    // int&) for +0x30 / +0x34 / +0x38 / +0x78, (int&) for +0x3c / +0x7c, (bool&)
    // for +0x40.
    uint32_t statTypeA;          // +0x30: stat type for the first operand (default 5)
    uint32_t statTypeB;          // +0x34: stat type for the second operand (default 5)
    uint32_t comparisonType;     // +0x38: 0-11: EQ/NE/LT/LE/GT/GE, odd = against statB (default 10)
    int32_t  fixedValue;         // +0x3c: constant operand when comparisonType is even (default 1)
    bool     requireOtherGoalsMet; // +0x40: result + 1 when set
    uint8_t  _pad41[7];          // +0x41: padding
    NamedExternalRef statA;      // +0x48: the first operand's External, by name (generation default -1)
    NamedExternalRef statB;      // +0x60: the second operand's External, by name (generation default -1)
    uint32_t statFlags;          // +0x78: RawMaterialDescription flag mask (default 0x20)
    int32_t  statSubType;        // +0x7c: (default 1)
};

static_assert(offsetof(StatGoal, statTypeA) == 0x30, "StatGoal::statTypeA offset");
static_assert(offsetof(StatGoal, statTypeB) == 0x34, "StatGoal::statTypeB offset");
static_assert(offsetof(StatGoal, comparisonType) == 0x38, "StatGoal::comparisonType offset");
static_assert(offsetof(StatGoal, fixedValue) == 0x3c, "StatGoal::fixedValue offset");
static_assert(offsetof(StatGoal, requireOtherGoalsMet) == 0x40, "StatGoal::requireOtherGoalsMet offset");
static_assert(offsetof(StatGoal, statA) == 0x48, "StatGoal::statA offset");
static_assert(offsetof(StatGoal, statA) + offsetof(NamedExternalRef, generation) == 0x50, "StatGoal statA generation (ctor 0x1400daad5)");
static_assert(offsetof(StatGoal, statA) + offsetof(NamedExternalRef, resolved) == 0x58, "StatGoal statA External (ctor 0x1400daadc)");
static_assert(offsetof(StatGoal, statB) == 0x60, "StatGoal::statB offset");
static_assert(offsetof(StatGoal, statB) + offsetof(NamedExternalRef, generation) == 0x68, "StatGoal statB generation");
static_assert(offsetof(StatGoal, statB) + offsetof(NamedExternalRef, resolved) == 0x70, "StatGoal statB External");
static_assert(offsetof(StatGoal, statFlags) == 0x78, "StatGoal::statFlags offset");
static_assert(offsetof(StatGoal, statSubType) == 0x7c, "StatGoal::statSubType offset");
static_assert(sizeof(StatGoal) == 0x80, "StatGoal is 0x80 (create 0x1400264d0 MemAlloc(0x80))");
static_assert(offsetof(StatGoal, _pad41) == 0x41, "StatGoal::_pad41");
static_assert(alignof(StatGoal) == 8, "StatGoal is qword-aligned");
