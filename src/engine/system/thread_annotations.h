#pragma once
// ============================================================================
// src/engine/system/thread_annotations.h -- clang thread-safety annotations
// ============================================================================
// NOT RETAIL. Compile-time documentation of which lock guards which data, checked by clang's
// -Wthread-safety (https://clang.llvm.org/docs/ThreadSafetyAnalysis.html). The CMake option
// BANISHED_THREAD_SAFETY (clang only) turns the analysis on as an error; every other build, GCC and
// MSVC included, sees empty macros, so an annotation never changes code, layout or behaviour.
//
// An attribute changes no type: a CAPABILITY on a retail-ABI class (System::CriticalSection,
// Platform::LockStorage) and a GUARDED_BY on a retail-ABI field leave every size and offset as they
// were (the static_asserts beside them still hold).
//
// What the analysis cannot follow, and the tree states instead of annotating:
//   - a hand-off ordered by an event or a thread start / join (System::Thread, Platform::Event);
//   - a lock taken on one thread and released on another (ext.render_pipe's Video::Device lock);
//   - a retail race kept on purpose (known_port_issues row 961): NO_THREAD_SAFETY_ANALYSIS with the
//     row named beside it.
// The annotated std::mutex wrapper for port-only code is annotated_mutex.h.
// ============================================================================

#if defined(__clang__)
#define BANISHED_THREAD_ANNOTATION(x) __attribute__((x))
#else
#define BANISHED_THREAD_ANNOTATION(x)
#endif

// A type that is a lock ("mutex") or a phantom lock-like token.
#define CAPABILITY(x) BANISHED_THREAD_ANNOTATION(capability(x))
// An RAII type whose constructor acquires and destructor releases a capability.
#define SCOPED_CAPABILITY BANISHED_THREAD_ANNOTATION(scoped_lockable)
// A field read or written only with the capability held.
#define GUARDED_BY(x) BANISHED_THREAD_ANNOTATION(guarded_by(x))
// A pointer whose pointee is read or written only with the capability held.
#define PT_GUARDED_BY(x) BANISHED_THREAD_ANNOTATION(pt_guarded_by(x))
// Lock order: this capability is taken before / after the named ones.
#define ACQUIRED_BEFORE(...) BANISHED_THREAD_ANNOTATION(acquired_before(__VA_ARGS__))
#define ACQUIRED_AFTER(...) BANISHED_THREAD_ANNOTATION(acquired_after(__VA_ARGS__))
// The caller holds the capability on entry (and still holds it on return).
#define REQUIRES(...) BANISHED_THREAD_ANNOTATION(requires_capability(__VA_ARGS__))
// The function takes / lets go of the capability (no argument on a member: `this`).
#define ACQUIRE(...) BANISHED_THREAD_ANNOTATION(acquire_capability(__VA_ARGS__))
#define RELEASE(...) BANISHED_THREAD_ANNOTATION(release_capability(__VA_ARGS__))
#define TRY_ACQUIRE(...) BANISHED_THREAD_ANNOTATION(try_acquire_capability(__VA_ARGS__))
// The caller must NOT hold the capability (the function takes it itself; not recursive).
#define EXCLUDES(...) BANISHED_THREAD_ANNOTATION(locks_excluded(__VA_ARGS__))
// The function returns a reference to the named capability.
#define RETURN_CAPABILITY(x) BANISHED_THREAD_ANNOTATION(lock_returned(x))
// Tells the analysis the capability is held here (an invariant it cannot see).
#define ASSERT_CAPABILITY(x) BANISHED_THREAD_ANNOTATION(assert_capability(x))
// The function is not analysed. Every use says why beside it.
#define NO_THREAD_SAFETY_ANALYSIS BANISHED_THREAD_ANNOTATION(no_thread_safety_analysis)
