// ============================================================================
// src/diag/diag_threads.h -- which thread is which (NOT RETAIL; block LOADSTAMP)
// ============================================================================
// A tiny portable roster of the threads the game creates, keyed by OS thread id:
// the main thread, the engine loader (Engine::Loader), the persistence worker, the
// audio stream, the pathfinder, the path-worker pool, the render thread and the
// cull helpers. Two readers:
//   * the [LoadTime] stamps (load_time.h) name the thread they print on;
//   * the sampling profiler (src/diag/win/sampler.cpp, BANISHED_SAMPLE) samples
//     every registered thread, not only the main one.
//
// A thread registers ITSELF (Register reads the calling thread's id), once: each
// call after the first on the same thread is one thread_local test. Every
// System::Thread registers from System::Thread::ThreadFunc just before its first
// Run (the object is fully constructed there, so its dynamic type names the role);
// the cull helpers register from HelperPool::ThreadMain; the main thread from
// load_time.cpp's static initialiser. Registration never fails the caller: past
// kMaxThreads entries it is dropped.
//
// Lock-free: an append-only array of {tid, role, busy}; the tid is published last
// (release), a reader skips a slot whose tid is still 0. No allocation, no
// constructor (constant-initialised), so it is safe before main. A thread id the OS
// hands out again gets a NEW slot (review F-293): the sampler reads the roster with
// a cursor that only moves forward, so an in-place update behind it was never seen
// and its role / busy pair could be read half-written.
//
// The busy bit lives in the slot, not in the thread's object (review S-034): the
// sampler used to keep a pointer to System::Thread::_runFlags and read it after the
// object could have been freed. A thread sets its own bit with SetBusy.
// ============================================================================
#pragma once

#include <cstdint>

namespace DiagThreads {

// Role letters: one byte, used as the sampler's per-sample thread field.
constexpr char kMain       = 'M';   // the main (sim + draw) thread
constexpr char kLoader     = 'L';   // Engine::Loader (LoaderLoop::_loader): UpdateLoop::Load jobs
constexpr char kPersist    = 'S';   // IO::PersistenceWorker: save / load file I/O
constexpr char kAudio      = 'A';   // Audio::Stream
constexpr char kPathfinder = 'F';   // PathfinderThread
constexpr char kPathWorker = 'P';   // ext.path_workers pool thread
constexpr char kRender     = 'R';   // ext.render_pipe render thread
constexpr char kCull       = 'C';   // ext.gather_threads cull helper
constexpr char kParallel   = 'J';   // System::Parallel fork-join worker (block WGPAR)
constexpr char kOther      = 'T';   // a System::Thread of another type

// Every registration takes a slot (a reused thread id too); past the last one a
// registration is dropped.
constexpr int kMaxThreads = 1024;

// Register the calling thread under `role` (idempotent per thread).
void Register(char role);
// The same, the role read from a System::Thread subclass's type name
// (typeid(*thread).name(), mangled or not).
void RegisterNamed(const char* typeName);
// Mark the calling thread mid-job (true) or idle (false): the sampler samples a busy
// thread for the whole job, waits included. A no-op on an unregistered thread.
void SetBusy(bool busy);

// The role registered for `tid` (its newest owner), or 0.
char RoleOf(uint32_t tid);
// "main", "loader", "persist", "audio", "pathfinder", "path", "render", "cull", "parallel",
// "thread".
const char* RoleName(char role);

// Snapshot for the sampler: entries [0, Count()) in registration order; a slot whose
// tid reads 0 is still being written (skip it).
int Count();
uint32_t TidAt(int i);
char RoleAt(int i);
bool BusyAt(int i);   // the slot's busy bit (SetBusy); false for a thread that never set it

// For tests/unit only: register `tid` under `role` from the calling thread without
// marking it registered, and the calling thread's slot (-1 when it has none).
namespace Detail {
int RegisterAs(uint32_t tid, char role);
int CurrentSlot();
}  // namespace Detail

}  // namespace DiagThreads
