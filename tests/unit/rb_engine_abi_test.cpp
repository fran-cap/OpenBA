// ============================================================================
// tests/unit/rb_engine_abi_test.cpp — ENDGAME B.3.8 block RB, the Engine loops
//
// B38_RULES D1 / RULE B8-1 on the four loop bases LOOP derives its five exe
// loops from (GameUpdateLoop : UpdateLoop, MenuDialogLoopT : DialogLoopT, the
// splash : DialogLoaderLoopT, two : LoaderLoop). Each real ctor/dtor shares
// ONE body (`InitFields` / `ReleaseFields`). ENDGAME B.3.9 ENG W2: the doors
// and `X::DoorBody` are gone; PART B pins the bases' own concrete slot 0.
//
// PART A — STAMP ORDER through test subclasses: every shared body runs under
//   ITS class's compiler table (base -> derived on the way in, derived -> base
//   on the way out), in retail's order. For the two Dialog loops the
//   `_dialogFilename` String copy (a member initialiser) is already in place
//   when InitFields runs, and ~String runs AFTER ReleaseFields and BEFORE the
//   base's teardown — retail's ~DialogLoopT order (UI, String, base).
// PART B — each base's destructor pair (B3X W1: retail slot 0, the scalar
//   deleting destructor, is `virtual ~T()`): `p->~T()` is retail's flags 0,
//   `delete p` flags 1 (the free proven through a recording operator delete),
//   and `delete[]` of a `new T[3]` replaces the retired MSVC VECTOR arm.
//
// NOT PROVEN HERE: the bodies (src/engine/engine/loops.cpp does not link
// here). They are RECORDING MARKERS below, as are the loops' virtuals (so
// that GCC emits the four vtables in this translation unit; nothing else in
// the unit binary defines them — engine_loop_abi_test.cpp measures slots with
// pointers to members and links no body).
// ============================================================================
#include "unit.h"
#include "abi.h"   // RT_ASM_LABEL
#include "dtor_check.h"

#include "engine/UpdateLoop.h"
#include "system_abi_stubs.h"   // recording Thread InitFields / ReleaseFields

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <new>
#include <type_traits>

extern const void* const g_rbUlVtGroup[]  RT_ASM_LABEL("_ZTVN6Engine10UpdateLoopE");
extern const void* const g_rbLlVtGroup[]  RT_ASM_LABEL("_ZTVN6Engine10LoaderLoopE");
extern const void* const g_rbDlVtGroup[]  RT_ASM_LABEL("_ZTVN6Engine11DialogLoopTE");
extern const void* const g_rbDllVtGroup[] RT_ASM_LABEL("_ZTVN6Engine17DialogLoaderLoopTE");

namespace {

const void* UlVt()  { return &g_rbUlVtGroup[2]; }
const void* LlVt()  { return &g_rbLlVtGroup[2]; }
const void* DlVt()  { return &g_rbDlVtGroup[2]; }
const void* DllVt() { return &g_rbDllVtGroup[2]; }
const void* VptrOf(const void* obj) { return *static_cast<const void* const*>(obj); }

// An ordered log of (event, vptr) pairs.
enum Ev { kUlInit = 1, kLlInit, kDlInit, kDllInit, kUlRel, kDlRel, kDllRel };
struct Entry { int ev; const void* vptr; const char* filename; const void* self; };
Entry g_log[16];
int   g_n = 0;
void Log(int ev, const void* self, const char* fn = nullptr) {
    if (g_n < 16) g_log[g_n++] = Entry{ev, VptrOf(self), fn, self};
}
void Clear() { g_n = 0; }

int64_t g_delaySeen = 0;
void*   g_nextSeen  = nullptr;

}  // namespace

// A System::String is its refcounted char* (String.h).
static const char* Raw(const System::String& s) {
    return *reinterpret_cast<const char* const*>(&s);
}

// ===========================================================================
// Markers: the shared bodies and the loops' own virtuals.
// ===========================================================================
namespace Engine {
void UpdateLoop::InitFields()    { Log(kUlInit, this); _loadBit = 5; _loadProgress = 0; }
void UpdateLoop::ReleaseFields() { Log(kUlRel, this); }
void LoaderLoop::InitFields(UpdateLoop* nextLoop, int64_t delayTicks) {
    Log(kLlInit, this);
    g_nextSeen = nextLoop; g_delaySeen = delayTicks;
    _nextLoop = nextLoop; _delayTicks = delayTicks; _autoAdvance = 1;
}
void DialogLoopT::InitFields()       { Log(kDlInit, this, Raw(_dialogFilename)); }
void DialogLoopT::ReleaseFields()    { Log(kDlRel, this, Raw(_dialogFilename)); }
void DialogLoaderLoopT::InitFields() { Log(kDllInit, this, Raw(_dialogFilename)); }
void DialogLoaderLoopT::ReleaseFields() { Log(kDllRel, this, Raw(_dialogFilename)); }

void UpdateLoop::DrawFailed() {}
void UpdateLoop::SaveRestore(bool, Save::SavedState*) {}
void UpdateLoop::OnVideoError() {}
void UpdateLoop::OnVideoRestart() {}
const char* UpdateLoop::GetResourceFilename() { return nullptr; }
bool UpdateLoop::AllowsModChanges() { return false; }

void LoaderLoop::Startup() {}
void LoaderLoop::Shutdown() {}
bool LoaderLoop::Update(const System::Time&) { return false; }
void LoaderLoop::Draw(float) {}
void LoaderLoop::Load() {}
void LoaderLoop::OnVideoError() {}

void DialogLoopT::Startup() {}
void DialogLoopT::Shutdown() {}
bool DialogLoopT::Update(const System::Time&) { return false; }
void DialogLoopT::Draw(float) {}
void DialogLoopT::Load() {}

void DialogLoaderLoopT::Startup() {}
void DialogLoaderLoopT::Shutdown() {}
bool DialogLoaderLoopT::Update(const System::Time&) { return false; }
void DialogLoaderLoopT::Draw(float) {}
void DialogLoaderLoopT::Load() {}

int g_rbLoaderRunCalls = 0;
void Loader::Run() { ++g_rbLoaderRunCalls; }
}  // namespace Engine

namespace {

// ---- test subclasses: stand-ins for LOOP's five exe loops -----------------
struct RbGameLoop final : Engine::UpdateLoop {
    void Startup() override {}
    void Shutdown() override {}
    bool Update(const System::Time&) override { return true; }
    void Draw(float) override {}
    void Load() override {}
};
struct RbLoaderLoop final : Engine::LoaderLoop {
    RbLoaderLoop(Engine::UpdateLoop* next, const System::Time& delay)
        : Engine::LoaderLoop(next, delay) {}
};
struct RbMenuLoop final : Engine::DialogLoopT {
    explicit RbMenuLoop(const System::String& f) : Engine::DialogLoopT(f) {}
    Object::InternalPtr<UI::DialogInstance>* CreateLoopDialog(
            Object::InternalPtr<UI::DialogInstance>* ret) override { return ret; }
};
struct RbSplashLoop final : Engine::DialogLoaderLoopT {
    RbSplashLoop(const System::String& f, Engine::UpdateLoop* next, const System::Time& d)
        : Engine::DialogLoaderLoopT(f, next, d) {}
    Object::InternalPtr<UI::DialogInstance>* CreateLoopDialog(
            Object::InternalPtr<UI::DialogInstance>* ret) override { return ret; }
};

System::Time TimeOf(int64_t ticks) {
    System::Time t;
    t._ticks = ticks;   // one int64 at +0
    return t;
}

// ===========================================================================
// PART A
// ===========================================================================
UNIT_TEST(rb_updateloop_ctor_dtor_run_shared_bodies_in_order) {
    Clear();
    alignas(8) unsigned char buf[sizeof(RbGameLoop)];
    RbGameLoop* l = ::new (static_cast<void*>(buf)) RbGameLoop;
    CHECK_EQ(g_n, 1);
    CHECK_EQ(g_log[0].ev, (int)kUlInit);
    CHECK(g_log[0].vptr == UlVt());
    CHECK(VptrOf(l) != UlVt());
    CHECK_EQ(l->GetLoadBit(), 5);
    l->~RbGameLoop();
    CHECK_EQ(g_n, 2);
    CHECK_EQ(g_log[1].ev, (int)kUlRel);
    CHECK(g_log[1].vptr == UlVt());
}

UNIT_TEST(rb_loaderloop_ctor_dtor_stamp_order) {
    Clear();
    int nextToken = 0;
    const int64_t delay = 12345;
    alignas(8) unsigned char buf[sizeof(RbLoaderLoop)];
    RbLoaderLoop* l = ::new (static_cast<void*>(buf)) RbLoaderLoop(
        reinterpret_cast<Engine::UpdateLoop*>(&nextToken), TimeOf(delay));
    CHECK_EQ(g_n, 2);
    CHECK_EQ(g_log[0].ev, (int)kUlInit);  CHECK(g_log[0].vptr == UlVt());
    CHECK_EQ(g_log[1].ev, (int)kLlInit);  CHECK(g_log[1].vptr == LlVt());
    CHECK(g_nextSeen == static_cast<void*>(&nextToken));
    CHECK_EQ(g_delaySeen, delay);          // the Time& argument's ticks
    CHECK(VptrOf(l) != LlVt());
    l->~RbLoaderLoop();
    CHECK_EQ(g_n, 3);
    CHECK_EQ(g_log[2].ev, (int)kUlRel);   CHECK(g_log[2].vptr == UlVt());
}

UNIT_TEST(rb_dialogloopt_ctor_dtor_order_is_retails) {
    Clear();
    System::String name("Dialog/MainMenu.rsc");
    alignas(8) unsigned char buf[sizeof(RbMenuLoop)];
    RbMenuLoop* l = ::new (static_cast<void*>(buf)) RbMenuLoop(name);
    CHECK_EQ(g_n, 2);
    CHECK_EQ(g_log[0].ev, (int)kUlInit);  CHECK(g_log[0].vptr == UlVt());
    CHECK_EQ(g_log[1].ev, (int)kDlInit);  CHECK(g_log[1].vptr == DlVt());
    // The String copy (member initialiser) precedes the shared body.
    CHECK(g_log[1].filename == Raw(name));   // copy shares the buffer (refcount)
    l->~RbMenuLoop();
    CHECK_EQ(g_n, 4);
    CHECK_EQ(g_log[2].ev, (int)kDlRel);   CHECK(g_log[2].vptr == DlVt());
    CHECK(g_log[2].filename == Raw(name));   // ~String not yet run
    CHECK_EQ(g_log[3].ev, (int)kUlRel);   CHECK(g_log[3].vptr == UlVt());
}

UNIT_TEST(rb_dialogloaderloopt_ctor_dtor_order_is_retails) {
    Clear();
    System::String name("Dialog/Splash.rsc");
    const int64_t delay = 7;
    alignas(8) unsigned char buf[sizeof(RbSplashLoop)];
    RbSplashLoop* l = ::new (static_cast<void*>(buf)) RbSplashLoop(name, nullptr, TimeOf(delay));
    CHECK_EQ(g_n, 3);
    CHECK_EQ(g_log[0].ev, (int)kUlInit);  CHECK(g_log[0].vptr == UlVt());
    CHECK_EQ(g_log[1].ev, (int)kLlInit);  CHECK(g_log[1].vptr == LlVt());
    CHECK_EQ(g_log[2].ev, (int)kDllInit); CHECK(g_log[2].vptr == DllVt());
    CHECK(g_log[2].filename == Raw(name));
    l->~RbSplashLoop();
    CHECK_EQ(g_n, 5);
    CHECK_EQ(g_log[3].ev, (int)kDllRel);  CHECK(g_log[3].vptr == DllVt());
    CHECK_EQ(g_log[4].ev, (int)kUlRel);   CHECK(g_log[4].vptr == UlVt());
}

// ===========================================================================
// PART B -- ENDGAME B.3.9 ENG W2: the doors are gone; retail slot 0 is each
// base's own destructor (B3X W1: D1 + D0; the retired RT_DELDTOR rows' VECTOR
// arms have no Itanium counterpart, `delete[]` replaces them).
// ===========================================================================
UNIT_TEST(b39_eng_loaderloop_is_concrete_and_its_ctor_public) {
    Clear();
    const int64_t delay = 3;
    void* mem = std::malloc(sizeof(Engine::LoaderLoop));
    Engine::LoaderLoop* l = ::new (mem) Engine::LoaderLoop(nullptr, TimeOf(delay));
    CHECK_EQ(g_n, 2);
    CHECK_EQ(g_log[0].ev, (int)kUlInit);  CHECK(g_log[0].vptr == UlVt());
    CHECK_EQ(g_log[1].ev, (int)kLlInit);  CHECK(g_log[1].vptr == LlVt());
    CHECK(VptrOf(l) == LlVt());           // the COMPILER's table, not a minted one
    CHECK_EQ(g_delaySeen, delay);
    // Retail flags 0: destroy, no free.
    l->~LoaderLoop();
    CHECK_EQ(g_n, 3);
    CHECK_EQ(g_log[2].ev, (int)kUlRel);   CHECK(g_log[2].vptr == UlVt());
    // Retail flags 1: destroy and free — `delete` through the ROOT pointer
    // reaches LoaderLoop's D0, whose operator delete is UpdateLoop's
    // class-scope one (the host MemFree is std::free, matching std::malloc).
    static_assert(dtor_check::HasClassDelete<Engine::LoaderLoop>::value, "UpdateLoop::operator delete");
    l = ::new (mem) Engine::LoaderLoop(nullptr, TimeOf(delay));
    Clear();
    delete static_cast<Engine::UpdateLoop*>(l);
    CHECK_EQ(g_n, 1);
    CHECK_EQ(g_log[0].ev, (int)kUlRel);
    // The free itself, proven: a LoaderLoop whose class operator delete records.
    using Rec = dtor_check::Recording<Engine::LoaderLoop>;
    Engine::UpdateLoop* r = dtor_check::NewOnMalloc<Rec>(nullptr, TimeOf(delay));
    Clear(); dtor_check::ResetFrees();
    delete r;
    CHECK_EQ(g_n, 1);
    CHECK_EQ(g_log[0].ev, (int)kUlRel);
    CHECK_EQ(dtor_check::g_frees, 1);
    CHECK(dtor_check::g_lastFree == static_cast<const void*>(r));
}

UNIT_TEST(b39_eng_loaderloop_deletingdtor_vector_arm) {
    // B3X W1: retail's slot-0 VECTOR arm (0x18004d4f0; flags 3: int32 count at this-8,
    // destroy DOWNWARD, free this-8) has no Itanium counterpart. Its
    // replacement is `delete[]` of a `new LoaderLoop[3]` (the public
    // (next, delay) ctor, one braced initialiser per element): the three
    // 0x48-byte destructors run downward.
    const int64_t delay = 1;
    Engine::LoaderLoop* a = new Engine::LoaderLoop[3]{
        {nullptr, TimeOf(delay)}, {nullptr, TimeOf(delay)}, {nullptr, TimeOf(delay)}};
    const void* e[3] = {&a[0], &a[1], &a[2]};
    CHECK(static_cast<const unsigned char*>(e[1]) - static_cast<const unsigned char*>(e[0]) == 0x48);
    Clear();
    delete[] a;
    CHECK_EQ(g_n, 3);
    for (int i = 0; i < 3; ++i) {
        CHECK_EQ(g_log[i].ev, (int)kUlRel);
        CHECK(g_log[i].self == e[2 - i]);
    }
}

UNIT_TEST(b39_eng_loop_bases_deletingdtor_runs_the_whole_chain) {
    System::String name("Dialog/MainMenu.rsc");
    alignas(8) unsigned char buf[sizeof(RbMenuLoop)];
    RbMenuLoop* m = ::new (static_cast<void*>(buf)) RbMenuLoop(name);
    Clear();
    // The BASE's destructor (qualified: RbMenuLoop has its own), retail flags 0.
#if defined(__clang__) && !defined(_MSC_VER)
    // Clang with the Itanium ABI traps in abstract-base D1; test the base D2 body.
    extern void ClangDialogBaseDtor(Engine::DialogLoopT*) RT_ASM_LABEL("_ZN6Engine11DialogLoopTD2Ev");
    ClangDialogBaseDtor(m);
#else
    m->Engine::DialogLoopT::~DialogLoopT();
#endif
    CHECK_EQ(g_n, 2);
    CHECK_EQ(g_log[0].ev, (int)kDlRel);   CHECK(g_log[0].vptr == DlVt());
    CHECK_EQ(g_log[1].ev, (int)kUlRel);   CHECK(g_log[1].vptr == UlVt());

    const int64_t delay = 9;
    alignas(8) unsigned char buf2[sizeof(RbSplashLoop)];
    RbSplashLoop* s = ::new (static_cast<void*>(buf2)) RbSplashLoop(name, nullptr, TimeOf(delay));
    Clear();
#if defined(__clang__) && !defined(_MSC_VER)
    // Clang with the Itanium ABI traps in abstract-base D1; test the base D2 body.
    extern void ClangLoaderBaseDtor(Engine::DialogLoaderLoopT*) RT_ASM_LABEL("_ZN6Engine17DialogLoaderLoopTD2Ev");
    ClangLoaderBaseDtor(s);
#else
    s->Engine::DialogLoaderLoopT::~DialogLoaderLoopT();
#endif
    CHECK_EQ(g_n, 2);
    CHECK_EQ(g_log[0].ev, (int)kDllRel);  CHECK(g_log[0].vptr == DllVt());
    CHECK_EQ(g_log[1].ev, (int)kUlRel);   CHECK(g_log[1].vptr == UlVt());

    alignas(8) unsigned char buf3[sizeof(RbGameLoop)];
    RbGameLoop* g = ::new (static_cast<void*>(buf3)) RbGameLoop;
    Clear();
#if defined(__clang__) && !defined(_MSC_VER)
    // Clang with the Itanium ABI traps in abstract-base D1; test the base D2 body.
    extern void ClangUpdateBaseDtor(Engine::UpdateLoop*) RT_ASM_LABEL("_ZN6Engine10UpdateLoopD2Ev");
    ClangUpdateBaseDtor(g);
#else
    g->Engine::UpdateLoop::~UpdateLoop();
#endif
    CHECK_EQ(g_n, 1);
    CHECK_EQ(g_log[0].ev, (int)kUlRel);
}

UNIT_TEST(rb_loop_bases_shape) {
    CHECK(std::is_abstract<Engine::UpdateLoop>::value);          // pure 1,2,3,4,6 (retail)
    CHECK(!std::is_abstract<Engine::LoaderLoop>::value);         // concrete, as in retail
    CHECK(std::is_abstract<Engine::DialogLoopT>::value);         // pure 12 (retail)
    CHECK(std::is_abstract<Engine::DialogLoaderLoopT>::value);
    // B3X W1: public virtual destructors (retail slot 0 = D1 + D0).
    CHECK(std::is_destructible<Engine::LoaderLoop>::value);
    CHECK(std::has_virtual_destructor<Engine::LoaderLoop>::value);
    CHECK(std::has_virtual_destructor<Engine::DialogLoopT>::value);
    CHECK(std::has_virtual_destructor<Engine::UpdateLoop>::value);
    CHECK(std::has_virtual_destructor<Engine::DialogLoaderLoopT>::value);

    CHECK_EQ(sizeof(RbGameLoop), (size_t)0x28);     // a subclass adds nothing by itself
    CHECK_EQ(sizeof(RbSplashLoop), (size_t)0x188);
}

// ENDGAME B.3.9 ENG W2: Engine::Loader's REAL ctor (retail 0x1800dc0f0):
// System::Thread(0x40000) runs once under THREAD's table, then _loop = null;
// the object wears the compiler's Loader table (slot 0 Run). ~Loader is the
// base chain only (retail 0x18004d330).
UNIT_TEST(b39_eng_loader_real_ctor_on_thread_0x40000) {
    g_systemStub_InitFields    = {};
    g_systemStub_ReleaseFields = {};
    alignas(8) unsigned char buf[sizeof(Engine::Loader)];
    std::memset(buf, 0xCD, sizeof(buf));
    Engine::Loader* l = ::new (static_cast<void*>(buf)) Engine::Loader();
    CHECK_EQ(g_systemStub_InitFields.calls, 1);
    CHECK_EQ(g_systemStub_InitFields.stackSize, 0x40000u);
    CHECK(g_systemStub_InitFields.vptrAtCall != VptrOf(l));
    CHECK(l->_loop == nullptr);
    using SlotFn = void (*)(void*);
    Engine::g_rbLoaderRunCalls = 0;
    reinterpret_cast<SlotFn>((*reinterpret_cast<void* const* const*>(l))[0])(l);
    CHECK_EQ(Engine::g_rbLoaderRunCalls, 1);
    l->~Loader();
    CHECK_EQ(g_systemStub_ReleaseFields.calls, 1);
    CHECK(g_systemStub_ReleaseFields.vptrAtCall == g_systemStub_InitFields.vptrAtCall);
}

}  // namespace
