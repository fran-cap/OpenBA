// src/game/Component/tradecomponent.cpp
// Clean reimplementation of TradeComponent lifecycle methods from Application-x64.exe.
//
// TradeComponent is the largest ControllerComponent variant (0xa8 bytes) with
// trade state, item arrays, and a controller sub-object at +0x88.
// Non-trivial vtable overrides:
//   Persist  (0x14005bd50) — tradecomponent_serialize.cpp
//   OnAdded  (0x14005c1d0) — add entity to trade pending list + call Setup
//   OnRemove (0x14005c190) — unregister entity from the world trade list
//   CommandMask / Command (0x14005c140 / 0x14005c000) — the work toggle
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Component/tradecomponent_funcs.h"
#include "Component/TradeDescription.h"
#include "Component/WorkComponent.h"
#include "Component/WorkPlaceComponent.h"
#include "Entity/ComponentEntity.h"   // ComponentEntity::FindComponent<T>
#include "Entity/CitizenBehavior.h"
#include "Work/TradeWork.h"
#include <cstdint>
#include "object/TypeInfo.h"
#include "Game/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/String.h"
#include <cstdio>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "system/Value.h"   // ENDGAME B.3.4b System caller batch
#include "Component/exe_bodies.h"
#include "Work/exe_bodies.h"   // WorkBase_TreeNodeFree_e320
#include "system/MemoryAllocator.h"
#include "Component/CitizenComponent.h"   // B4 S4 RT2: InternalPtr<CitizenComponent>::Get() needs the class complete

// ============================================================
// Helpers
// ============================================================
// FUN_140002cc0 Array<T>::Remove — the world entity-list erase (by value).
// FUN_140003110 — add entity to world pending list (Array SetSizeFill).
// FUN_14005c220 TradeComponent::Setup (below) — called after adding to pending list.

// ============================================================
// TradeComponent slots 13 and 14 — the work-enable pair
// ============================================================
// These are the two remaining TradeComponent-specific vtable entries (the full
// vtable is 0x14015acd0; only slots 3/6/7/13/14 are TradeComponent's own, the rest
// are Component base). Both were inert no-op stubs, which is what made the trading
// post report "work disabled" permanently and ignore the work button:
//
//   slot 13 (0x14005c140) is the QUERY the UI reads for the work toggle's state.
//     It returns 3 when the workplace is accepting and 1 when it is not — never 0.
//     An inert stub returns 0, which is neither, and reads as "disabled here".
//   slot 14 (0x14005c000) is the TOGGLE ACTION. It flips the workplace's accepting
//     flag and, on enable, allocates a TradeWork and posts it to the work
//     scheduler; on disable it cancels the posted work.
//
// They must be promoted together. Slot 13 alone would report a state that slot 14
// is not able to change; slot 14 alone would change a state nothing reads.

// FUN_140002e10 FindComponent<WorkPlaceComponent>
// FUN_140003050 FindComponent<WorkComponent> — the component the work is posted through.
// FUN_14012ec60 CitizenAI_WorkScheduler_Post — post a Work to the scheduler (stop
//   existing slot, then install).
// FUN_14012eb50 WorkComponent::CancelWork — cancel whatever work is currently posted.
// FUN_14012fc00 WorkPlaceComponent::SetAccepting — commit the new accepting state.

// ============================================================
// The Collection::Array<T>::SetSize instances of TradeComponent's arrays, kept as the
// tree's bodies over the typed elements (tradecomponent_funcs.h).
// ============================================================

// 0x14000e6b0 — the goods array of a merchant order (8-byte ExternalPtrs): free the
// destination's buffer, allocate count*8 (8-aligned), copy the source pointers in.
// Faithful 1:1 port of TradeComponent_helper_e6b0; closure = MemAlloc/MemFree only.
static void TradeGoods_Copy(TradeMerchantOrder& dst, const TradeMerchantOrder& src) {
    System::MemoryAllocator::MemFree(dst.goods._data);
    dst.goods._size = 0;   // retail: one qword 0 over +0x18..+0x1f
    dst.goods._capacity = 0;
    dst.goods._data = nullptr;
    int count = src.goods._size;
    dst.goods._size = count;
    if (count > 0) {
        dst.goods._capacity = count;
        dst.goods._data = static_cast<ComponentDescription**>(
            System::MemoryAllocator::MemAlloc((uint64_t)count * 8, 8));
    }
    int n = dst.goods._size;
    for (int64_t i = 0; i < n; i++) {
        ComponentDescription** slot = dst.goods._data + i;
        if (slot != nullptr)
            *slot = src.goods._data[i];
    }
}

// 0x14000ef10 — destroy `count` merchant orders: free each goods buffer, zero its
// pointer and count / capacity, then ~String on the name. Faithful 1:1 port of
// TradeComponent_sub_ef10; closure = MemFree + System::String::~String.
void TradeMerchantOrders_Destroy(TradeMerchantOrder* base, int count) {
    if (count <= 0) return;
    TradeMerchantOrder* p = base;
    for (int64_t k = count; k != 0; k--, ++p) {
        System::MemoryAllocator::MemFree(p->goods._data);
        p->goods._data = nullptr;
        p->goods._size = 0;   // retail: one qword 0 over +0x18..+0x1f
        p->goods._capacity = 0;
        p->merchantName.~String();
    }
}

// 0x14000e760's two element arms. The retained arm copy-constructs the name, copies
// the kind and clones the goods array (0x14000e6b0); the new arm builds an empty name
// and zeroes the rest.
static void TradeMerchantOrder_CopyConstruct(TradeMerchantOrder* dstElem, const TradeMerchantOrder* srcElem) {
    ::new (&dstElem->merchantName) System::String(srcElem->merchantName);   // String at +0x00
    dstElem->orderKind = srcElem->orderKind;                                // int at +0x08
    ::new (&dstElem->goods) TradeMerchantOrder::GoodsList();                // +0x10 = +0x18 = 0
    if (&dstElem->goods != &srcElem->goods)
        TradeGoods_Copy(*dstElem, *srcElem);
}
static void TradeMerchantOrder_ConstructZero(TradeMerchantOrder* dstElem) {
    ::new (&dstElem->merchantName) System::String();   // _data = null (0x180006760)
    dstElem->orderKind = 0;
    dstElem->_pad0C[0] = dstElem->_pad0C[1] = dstElem->_pad0C[2] = dstElem->_pad0C[3] = 0;
    ::new (&dstElem->goods) TradeMerchantOrder::GoodsList();   // +0x10 = +0x18 = 0
}

// The +0x30 array's row: 0x14000e760's element arms and 0x14000ef10's destroy. Its
// ~Array is retail's dtor 0x14005c3de..0x14005c3f9: ef10 over the count, MemFree,
// +0x30 = +0x38 = 0. (The resize stays the hand-written 0x14000e760 below.)
const rtcoll::ArraySpec kSpec_TradeMerchantOrder = {   // exe 0x14000e760 / 0x14000ef10
    "Array<TradeMerchantOrder>", 0x20, 8,
    &rtcoll::AssignAs<TradeMerchantOrder, &TradeMerchantOrder_CopyConstruct>,
    &rtcoll::InitAs<TradeMerchantOrder, &TradeMerchantOrder_ConstructZero>,
    &rtcoll::DestroyAs<TradeMerchantOrder, &TradeMerchantOrders_Destroy>, false,
    /*assign unwritten*/ 0, 0,
    /*init   unwritten*/ 0, 0,
    /*seed zero       */ 0, 0 };

// 0x14000e760 — resize the merchant-order array (0x20 stride). On growth past the
// capacity, allocate cap*0x20 (8-aligned; cap = 3*newCount on a +1 grow from
// non-empty), then for each retained element copy its String (+0x00) and int (+0x08)
// and clone its goods array (+0x10); new elements are zeroed. The shrink path destroys
// the dropped elements (0x14000ef10). Faithful 1:1 port of TradeComponent_impl_e760.
template <> void TradeComponent::MerchantOrderArray::SetSize(int count) {   // 0x14000e760
    int oldCount = _size;
    if (count == oldCount) return;

    TradeMerchantOrder* oldBuf = _data;
    _size = count;

    if (_capacity < count) {
        _capacity = count;
        if (count == oldCount + 1 && oldCount != 0)
            _capacity = count * 3;
        _data = static_cast<TradeMerchantOrder*>(System::MemoryAllocator::MemAlloc(
            (uint64_t)_capacity * sizeof(TradeMerchantOrder), 8));
    }

    TradeMerchantOrder* newBuf = _data;
    int startIdx = (oldBuf == newBuf) ? oldCount : 0;
    for (int64_t i = startIdx; i < count; i++) {
        TradeMerchantOrder* dstElem = newBuf + i;
        if (i < oldCount) {
            TradeMerchantOrder* srcElem = oldBuf + i;
            if (dstElem) TradeMerchantOrder_CopyConstruct(dstElem, srcElem);
        } else {
            if (dstElem) TradeMerchantOrder_ConstructZero(dstElem);
        }
    }

    if (oldBuf == newBuf) {
        if (count < oldCount)
            TradeMerchantOrders_Destroy(oldBuf + count, oldCount - count);
    } else {
        TradeMerchantOrders_Destroy(oldBuf, oldCount);
        System::MemoryAllocator::MemFree(oldBuf);
    }
}

// 0x14000e8d0 — resize the buy-order array (0xc stride). On growth past the capacity,
// allocate cap*0xc (4-aligned), copy the old entries (3 ints each), zero the new
// entries' third int, free the old buffer. Faithful 1:1 port of TradeComponent_sub_e8d0.
template <> void Collection::Array<TradeBuyOrder>::SetSize(int count) {   // 0x14000e8d0
    int oldCount = _size;
    if (count == oldCount) return;

    TradeBuyOrder* oldBuf = _data;
    _size = count;

    if (_capacity < count) {
        _capacity = count;
        if (count == oldCount + 1 && oldCount != 0) {
            _capacity = count * 3;
        }
        _data = static_cast<TradeBuyOrder*>(System::MemoryAllocator::MemAlloc(
            (uint64_t)_capacity * sizeof(TradeBuyOrder), 4));
    }

    int startIdx = (oldBuf == _data) ? oldCount : 0;
    for (int64_t i = startIdx; i < count; i++) {
        TradeBuyOrder* dst = _data + i;
        if (i < oldCount) {
            if (dst) {
                dst->rawMaterialFlags = oldBuf[i].rawMaterialFlags;
                dst->categoryIndex = oldBuf[i].categoryIndex;
                dst->amount = oldBuf[i].amount;
            }
        } else {
            if (dst) dst->amount = 0;
        }
    }

    if (oldBuf != _data) {
        System::MemoryAllocator::MemFree(oldBuf);
    }
}

// ============================================================
// TradeComponent::Setup (0x14005c220) — build the merchant orders and the buy orders
// from the TradeDescription
// ============================================================
// Reads the TradeDescription off +0x18 (type 0x1401c4bb8). Resizes the merchant-order
// array (+0x30) to the merchant count and names each with a random merchant name from
// 0x1400a4c60; resizes the buy orders (+0x40) to the goods-category count and copies
// each category's flags (0x18 stride -> 0xc stride). Helpers: 0x14000e760, 0x14000e8d0
// (above), 0x1400a4c60 NameDB_PickAndGenerate, import slots 0x1401514c0
// System::String::operator= and 0x140152af0 System::String::~String.
void TradeComponent::Setup() {
    if (GameUpdateLoop::_singleton == nullptr) return;

    Description* descObj = _description;
    TradeDescription* desc = Object::IsDerivedFromNullSafe(descObj, TradeDescription::TypeId())
        ? static_cast<TradeDescription*>(descObj) : nullptr;

    // Resize the merchant orders (+0x30) to the merchant count
    merchantOrders.SetSize(desc->_merchantCount);

    // Name each merchant
    int nameCount = merchantOrders._size;
    System::Random* random = GameUpdateLoop::_singleton->m_random;
    for (int i = 0; i < nameCount; i++) {
        uint32_t r = random->Rand(2u);
        System::String picked;                                 // _data = null, as the zeroed buffer
        NameDB_PickAndGenerate(&picked, random, (char)(r == 0));   // 0x1400a4c60
        merchantOrders._data[i].merchantName = picked;               // slot 0x1401514c0
    }                                                          // ~String: slot 0x140152af0

    // Report the OUTCOME, not the call. A populated +0x30 is the whole point of
    // this function — TradeUI_RefreshBuyTable indexes it — and "Setup ran" is not
    // evidence it filled anything: desc null, or a merchant count of 0, both leave the
    // array NULL and read identically from a bare entry trace.
    fprintf(stderr, "[TradeComp] Setup: desc=%p names=%d array=%p\n",
            static_cast<void*>(desc), nameCount, static_cast<void*>(merchantOrders._data));
    // ...and the names themselves. A populated array is NOT evidence the names are
    // real: while 0x1400a4c60 was unregistered, its door was null, the buffer stayed
    // zeroed, and every slot got an EMPTY string — which reads identically to a
    // healthy array from the line above.
    for (int i = 0; merchantOrders._data && i < nameCount; i++) {
        const char* nm = merchantOrders._data[i].merchantName._data;
        fprintf(stderr, "[TradeComp]   name[%d]=\"%s\"\n", i, nm ? nm : "(null)");
    }

    // Resize the buy orders (+0x40) to the goods-category count
    buyOrders.SetSize(desc->_pairCount);

    // Copy each category's flags (0x18 stride) into its buy order (0xc stride)
    int itemCount = buyOrders._size;
    TradeGoodsCategory* src = desc->_pairs;
    TradeBuyOrder* dst = buyOrders._data;
    for (int i = 0; i < itemCount; i++) {
        dst[i].rawMaterialFlags = src[i].rawMaterialFlags;
        dst[i].categoryIndex = i;
    }
}

// ===== ENDGAME B.3.8 MI-B — TradeComponent as a REAL class =====
// (TradeComponent.h). Before: COMPONENT_INERT_FACTORY expanded by hand (48-slot
// table, slots 1/3/6/13/14/16/17 patched per create, Inert_ScalarDtor in slot 0, a
// CtorInit writing only +0x2c = -1, and Dispatcher<TradeCtrlTag> stamped into raw
// bytes at +0x88 with its back-pointer at +0xa0). After: the compiler's 18-slot
// vtable, a real ctor/dtor chain, the controller a member object at retail's +0x88
// (B38_RULES D1, D2, D6).
#include "Object/factory_registration.h"

// 0x14005c430. Component() is the CALL 0x1401188b0; the stamps are the compiler's;
// +0x28 = 0; +0x2c = -1 (the "no merchant selected" sentinel: tradeui's `>= 0` test
// and comboEnabled key off it); +0x30..+0x58 = 0; +0x60 = 0 (qword), +0x68 = 0
// (dword); +0x71 = 0; +0x78 = +0x80 = 0; the member at +0x88 with its back-pointer
// (+0xa0); then `_flags |= 3` (0x83). +0x6c..+0x70 and +0x72..+0x77 are NOT written
// (0x14005c495 `MOV [RAX+0x8],EDX` then 0x14005c498 `MOV byte [RBX+0x71],DL`; B6 row 27).
TradeComponent::TradeComponent()
    : tradeState(0), tradeId(-1),
      merchantOrders(), buyOrders(), manifest(),
      stock(),
      tradeFlag(0), merchantTimer{0}, merchant(nullptr),
      _controller(this) {
    OrFlagsInCtor(0x3);
}

// 0x14005c340: the body is empty. ~_controller is the +0x88 restamp (0x14005c376), then
// the members in reverse declaration order, retail's release order:
//   +0x60 ~stock (below): the root's two subtrees through 0x14000e320, then the root;
//         +0x60 = 0 (qword), +0x68 = 0 (dword)                     0x14005c380..0x14005c3b7
//   +0x50 ~manifest: MemFree, +0x50 = +0x58 = 0 (qwords)           0x14005c3ba..0x14005c3c8
//   +0x40 ~buyOrders: MemFree, +0x40 = +0x48 = 0                    0x14005c3cc..0x14005c3da
//   +0x30 ~merchantOrders: 0x14000ef10 over the count (+0x38), MemFree, +0x30 = +0x38 = 0
//                                                                   0x14005c3de..0x14005c3f9
// then the restamps and ~Internal (the compiler chain).
TradeComponent::~TradeComponent() = default;

TradeStockTree::~TradeStockTree() {
    if (TradeResourceNode* r = root) {
        WorkBase_TreeNodeFree(&root, r->left);
        WorkBase_TreeNodeFree(&root, r->right);
        Object::detail::InternalMemFree(r);
    }
    root = nullptr;
    count = 0;
}

// create 0x14000db60: MemAlloc(0xa8, 8), then the ctor.
::Object::Base* TradeComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(TradeComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) TradeComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* TradeComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) TradeComponent());
}

// [3] 0x14005bd50 — TradeComponent::Persist lives in tradecomponent_serialize.cpp.
// ============================================================
// TradeComponent::OnAdded (vtable slot [6]; Ghidra "Draw", 0x14005c1d0)
// ============================================================
// Adds the owning entity to the world trade list, then calls Setup.
//
// Original disassembly:
//   MOV RAX,[RCX+0x10]          ; entity ptr from owner
//   MOV RBX,RCX                 ; save self
//   LEA RDX,[RSP+0x20]          ; stack InternalBasePtr
//   MOV [RSP+0x20],RAX          ; store entity ptr
//   MOV RAX,[DAT_1401c3dd8]     ; game state singleton
//   MOV RCX,[RAX+0xf0]          ; world manager
//   MOV EDX,[RCX+0x38]          ; current count
//   ADD EDX,1                   ; newCount = count + 1
//   ADD RCX,0x30                ; trade pending list
//   LEA R8,[RSP+0x20]           ; entity ptr
//   CALL FUN_140003110           ; add to pending list
//   MOV RCX,RBX                 ; self
//   CALL FUN_14005c220           ; trade setup

void TradeComponent::OnAdded() {
    if (GameUpdateLoop::_singleton == nullptr)
        return;

    ComponentEntity* entityPtr = _entity.Get();

    // The town manager (GameUpdateLoop::m_citizenList).
    CitizenBehavior* worldMgr = GameUpdateLoop::_singleton->m_citizenList;

    // Add to the trading-post list at +0x30
    int currentCount = worldMgr->tradePosts._size;
    worldMgr->tradePosts
        .SetSizeFill(currentCount + 1, &entityPtr);

    Setup();   // 0x14005c220
}
// ============================================================
// TradeComponent::OnRemove (vtable slot [7]; Ghidra "OnCreate", 0x14005c190)
// ============================================================
// Unregisters the owning entity from the world trade entity list.
//
// Original disassembly:
//   MOV RAX,[RCX+0x10]          ; entity ptr from owner
//   LEA RDX,[RSP+0x08]          ; stack InternalBasePtr
//   MOV [RSP+0x08],RAX          ; store entity ptr
//   MOV RAX,[DAT_1401c3dd8]     ; game state singleton
//   MOV RCX,[RAX+0xf0]          ; world manager
//   ADD RCX,0x30                ; trade entity list
//   CALL FUN_140002cc0           ; unregister entity

void TradeComponent::OnRemove() {
    if (GameUpdateLoop::_singleton == nullptr)
        return;

    ComponentEntity* entityPtr = _entity.Get();

    // The town manager (GameUpdateLoop::m_citizenList).
    CitizenBehavior* worldMgr = GameUpdateLoop::_singleton->m_citizenList;

    // Unregister the entity from the trading-post list at +0x30
    worldMgr->tradePosts.Remove(entityPtr);   // 0x140002cc0
}
// [13] 0x14005c140 / [14] 0x14005c000 — the work-enable pair; they go together (a
// live query over a stub toggle reports a state nothing can change). Neither
// reads its command id.
// TradeComponent::CommandMask (vtable index 13, 0x14005c140) — work-state query.
//   return -(WPC[0x50] != 0) & 2 | 1   =>  3 when accepting, 1 when not.
// The null test is the tree's.
uint32_t TradeComponent::CommandMask(int /*command*/) {
    WorkPlaceComponent* wpc = _entity->FindComponent<WorkPlaceComponent>();   // 0x140002e10
    if (!wpc) return 1;

    return wpc->isAccepting ? 3 : 1;
}
// TradeComponent::Command (vtable index 14, 0x14005c000) — work-state toggle.
// Reads the workplace's CURRENT accepting flag (+0x50) and inverts it:
//   currently OFF -> allocate a TradeWork, post it, commit accepting = 1
//   currently ON  -> cancel the posted work,          commit accepting = 0
// TradeComponent+0x70 caches the new state for the panel. The null test is the tree's.
void TradeComponent::Command(int /*command*/) {
    WorkComponent* sched = _entity->FindComponent<WorkComponent>();          // 0x140003050

    WorkPlaceComponent* wpc = _entity->FindComponent<WorkPlaceComponent>();  // 0x140002e10

    if (!wpc) return;

    // enable == "the workplace is NOT currently accepting", i.e. this is a toggle.
    bool enable = !wpc->isAccepting;
    workEnabled = enable;

    char newState;
    if (enable) {
        TradeWork* work = nullptr;
        // MemAlloc(0x58, 8) + the from-entity TradeWork ctor 0x1400f6ea0, as retail.
        // ENDGAME B.3.8 MI-B: the REAL TradeWork (Work/TradeWork.h) — its default ctor
        // runs TradeWork_ResolveFuncs (the reason the hand build had to call it) and
        // zeroes +0x48/+0x50, the compiler stamps its table, and Work::InitFromEntity is
        // Work_Ctor25's body (0x1400f72e0). No more read-back of 0x140161a10.
        void* mem = System::MemoryAllocator::MemAlloc(0x58, 8);
        if (mem) {
            ComponentEntity* entityIBP = _entity.Get();
            work = new (mem) TradeWork();
            work->InitFromEntity(&entityIBP);
        }
        Object::InternalPtr<Work> workIBP(work);
        sched->PostWork(1, '\0', &workIBP);   // 0x14012ec60
        newState = '\x01';
    } else {
        sched->CancelWork();   // 0x14012eb50
        newState = '\0';
    }

    wpc->SetAccepting(newState != '\0');   // 0x14012fc00

    fprintf(stderr, "[TradeComp] Slot14 toggle: wpc=%p was=%d now=%d\n",
            static_cast<void*>(wpc), !enable, (int)newState);
    fflush(stderr);
}

DEFINE_EXE_TYPE(TradeComponent, "ControllerComponent")
