#pragma once
// ============================================================================
// src/game/Dialog/ProfessionUI.h — ENDGAME B.3.8 block PNL (B38_RULES D1-D5)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6): reflected "ProfessionUI", parent
//   "UI::DialogController", table 0x14015d0f0, 11 slots, sizeof 0x78.
//   create 0x140013400: MemAlloc(0x78, 8), ??0DialogController, stamp, fields.
//   [0] 0x140013450 scalar deleting dtor (~ProfessionUI + MemFree on &1)
//   [1] TypeName  [2] Type  (DECLARE_EXE_TYPE)
//   [5] 0x140076ca0  OnOpened
//   [8] 0x1400765a0  OnUpdate
//   [9] 0x140076c10  OnElementEvent
//   every other slot is UI::DialogController's (inherited, not declared).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "ui/DialogController.h"
#include "Object/exe_type.h"
#include "collection/Collection.h"

class Profession;   // Game/Profession.h
namespace UI { class NumberEditElement; class NumberElement; }

class ProfessionUI final : public UI::DialogController {
    DECLARE_EXE_TYPE(ProfessionUI)

public:
    ProfessionUI() = default;                            // 0x140013400 (ctor inlined in the factory)

    // 0x140013450 minus the free: ~ProfessionUI 0x1400774e0 frees the Set's nodes,
    // then the three Arrays (+0x50, +0x40, +0x30: member order, reversed), then
    // the compiler chains ~DialogController (and so ~Internal: RemoveObject).
    ~ProfessionUI() override;                                     // [0] 0x140013450
    void OnOpened() override;   // [5] 0x140076ca0
    void OnUpdate(const System::Time& time) override;   // [8] 0x1400765a0
    void OnElementEvent(Object::InternalPtr<UI::Element>* element, UI::EventId a, int32_t b,
                        int32_t c, int32_t d, int32_t e, void* userData) override;   // [9] 0x140076c10

    // Retail's own Collection::Set node (0x30 bytes): a profession -> its row index
    // (OnOpened 0x140076ca0 inserts, OnUpdate 0x1400765a0 looks up; the AVL
    // rebalance is 0x140013480, the rotations 0x14000e450 / 0x14000e580).
    struct ProfessionIndexNode {
        Profession*          key;      // +0x00 ExternalPtr<Profession>
        int32_t              index;    // +0x08 the row
        int32_t              _pad0C;   // +0x0C
        ProfessionIndexNode* parent;   // +0x10
        ProfessionIndexNode* left;     // +0x18
        ProfessionIndexNode* right;    // +0x20
        int16_t              balance;  // +0x28 right height - left height
        int16_t              height;   // +0x2A
        int32_t              _pad2C;   // +0x2C
    };

private:
    // Non-virtual members (retail passes the controller in RCX).
    void HandleSpinner(int profIndex);                             // 0x1400763c0
    void HandleButton(int profIndex);                              // 0x140076a30

public:
    // ---- fields: retail's three Collection::Arrays and one Set, released by
    // ~ProfessionUI 0x1400774e0; the zero initialisers are the create path's
    // stores (0x140013400).
    Collection::Array<int32_t> counts;                  // +0x30 citizens per profession row
    Collection::Array<UI::NumberEditElement*> spinners; // +0x40 InternalPtr<NumberEditElement>
    Collection::Array<UI::NumberElement*> numbers;      // +0x50 InternalPtr<NumberElement>
    ProfessionIndexNode* professionIndexRoot = nullptr; // +0x60 Set root
    int32_t   professionIndexCount = 0;    // +0x68 (u32 write)
    uint8_t _pad6C[4] = {};                // +0x6C: gap
    int32_t   gotoIndex = 0;               // +0x70 the citizen HandleButton cycles through (u32 write)
    uint8_t _pad74[4] = {};                // +0x74: gap
};

static_assert(sizeof(ProfessionUI) == 0x78, "ProfessionUI is 0x78 (create 0x140013400 MemAlloc(0x78))");
static_assert(alignof(ProfessionUI) == 8, "qword-aligned (the vptr)");
static_assert(offsetof(ProfessionUI, counts) == 0x30, "counts @ 0x30");
static_assert(offsetof(ProfessionUI, spinners) == 0x40, "spinners @ 0x40");
static_assert(offsetof(ProfessionUI, numbers) == 0x50, "numbers @ 0x50");
static_assert(offsetof(ProfessionUI, professionIndexRoot) == 0x60, "professionIndexRoot @ 0x60");
static_assert(offsetof(ProfessionUI, professionIndexCount) == 0x68, "professionIndexCount @ 0x68");
static_assert(offsetof(ProfessionUI, gotoIndex) == 0x70, "gotoIndex @ 0x70");
static_assert(sizeof(ProfessionUI::ProfessionIndexNode) == 0x30, "the Set node is 0x30 (MemAlloc(0x30, 8))");
static_assert(offsetof(ProfessionUI::ProfessionIndexNode, key) == 0x0, "ProfessionIndexNode::key");
static_assert(offsetof(ProfessionUI::ProfessionIndexNode, _pad0C) == 0xC, "ProfessionIndexNode::_pad0C");
static_assert(offsetof(ProfessionUI::ProfessionIndexNode, _pad2C) == 0x2C, "ProfessionIndexNode::_pad2C");
static_assert(offsetof(ProfessionUI::ProfessionIndexNode, index) == 0x08, "node +0x08");
static_assert(offsetof(ProfessionUI::ProfessionIndexNode, parent) == 0x10, "node +0x10");
static_assert(offsetof(ProfessionUI::ProfessionIndexNode, left) == 0x18, "node +0x18");
static_assert(offsetof(ProfessionUI::ProfessionIndexNode, right) == 0x20, "node +0x20");
static_assert(offsetof(ProfessionUI::ProfessionIndexNode, balance) == 0x28, "node +0x28");
static_assert(offsetof(ProfessionUI::ProfessionIndexNode, height) == 0x2A, "node +0x2a");
