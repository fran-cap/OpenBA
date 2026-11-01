// ============================================================================
// src/diag/android/signal_context_android.h -- a signal frame's registers, by ABI
// ============================================================================
// The crash handler and the boot-state watchdog walk an interrupted thread's stack the same way on
// both Android ABIs: the pc, the link register where the ABI has one, then the frame-pointer chain,
// whose records are {saved frame pointer, return address} pairs on both (arm64's x29, x86_64's rbp).
// arm64-v8a is the phones; x86_64 is the emulator's system image (docs/android/EMULATOR.md).
// ============================================================================
#pragma once

#include <ucontext.h>

#include <cstdint>

namespace SignalContext {

#if defined(__aarch64__)
inline uintptr_t Pc(const mcontext_t& mc) { return static_cast<uintptr_t>(mc.pc); }
inline uintptr_t Sp(const mcontext_t& mc) { return static_cast<uintptr_t>(mc.sp); }
inline uintptr_t Fp(const mcontext_t& mc) { return static_cast<uintptr_t>(mc.regs[29]); }
// The return address held in a register (arm64's lr, x30); 0 where the ABI keeps it on the stack.
inline uintptr_t Lr(const mcontext_t& mc) { return static_cast<uintptr_t>(mc.regs[30]); }
#elif defined(__x86_64__)
inline uintptr_t Pc(const mcontext_t& mc) { return static_cast<uintptr_t>(mc.gregs[REG_RIP]); }
inline uintptr_t Sp(const mcontext_t& mc) { return static_cast<uintptr_t>(mc.gregs[REG_RSP]); }
inline uintptr_t Fp(const mcontext_t& mc) { return static_cast<uintptr_t>(mc.gregs[REG_RBP]); }
// x86_64 has no link register: a CALL pushes the return address at [rsp].
inline uintptr_t Lr(const mcontext_t&) { return 0; }
#else
#error "src/diag/android: no signal-frame registers for this ABI (arm64-v8a and x86_64 only)"
#endif

}  // namespace SignalContext
