/*
 * Banished reimplementation, third_party/wmadec: the build configuration FFmpeg's
 * `configure` would generate, reduced to what the vendored WMA v2 decoder reads.
 * Portable C only: no assembly, no inline asm, no SIMD codelets, no threads layer
 * (one-time table init goes through shim/libavutil/thread.h instead).
 * Not an FFmpeg file; see EXTRACT.md.
 */
#ifndef WMADEC_SHIM_CONFIG_H
#define WMADEC_SHIM_CONFIG_H

#define ARCH_AARCH64 0
#define ARCH_ALPHA 0
#define ARCH_ARM 0
#define ARCH_LOONGARCH 0
#define ARCH_LOONGARCH64 0
#define ARCH_MIPS 0
#define ARCH_MIPS64 0
#define ARCH_PPC 0
#define ARCH_RISCV 0
#define ARCH_X86 0
#define ARCH_X86_32 0
#define ARCH_X86_64 0

#define HAVE_X86ASM 0
#define HAVE_INLINE_ASM 0
#define HAVE_MMX 0
#define HAVE_NEON 0
#define HAVE_ALTIVEC 0
#define HAVE_BIGENDIAN 0
#define HAVE_FAST_UNALIGNED 0
#define HAVE_FAST_64BIT 1
#define HAVE_FAST_CLZ 1
#define HAVE_THREADS 0
#define HAVE_PTHREADS 0
#define HAVE_W32THREADS 0
#define HAVE_OS2THREADS 0
#define HAVE_SIMD_ALIGN_16 0
#define HAVE_SIMD_ALIGN_32 0
#define HAVE_SIMD_ALIGN_64 0
#define HAVE_CBRT 1
#define HAVE_CBRTF 1
#define HAVE_COPYSIGN 1
#define HAVE_COSF 1
#define HAVE_ERF 1
#define HAVE_EXP2 1
#define HAVE_EXP2F 1
#define HAVE_EXPF 1
#define HAVE_HYPOT 1
#define HAVE_ISFINITE 1
#define HAVE_ISINF 1
#define HAVE_ISNAN 1
#define HAVE_LDEXPF 1
#define HAVE_LLRINT 1
#define HAVE_LLRINTF 1
#define HAVE_LOG2 1
#define HAVE_LOG2F 1
#define HAVE_LOG10F 1
#define HAVE_LRINT 1
#define HAVE_LRINTF 1
#define HAVE_POWF 1
#define HAVE_RINT 1
#define HAVE_ROUND 1
#define HAVE_ROUNDF 1
#define HAVE_SINF 1
#define HAVE_TRUNC 1
#define HAVE_TRUNCF 1

#define CONFIG_SMALL 1
#define CONFIG_HARDCODED_TABLES 0
#define CONFIG_MEMORY_POISONING 0
#define CONFIG_FTRAPV 0
/* The checked bitstream reader, as upstream `configure` enables it by default
 * (--disable-safe-bitstream-reader turns it off). get_bits.h makes
 * UNCHECKED_BITSTREAM_READER = !CONFIG_SAFE_BITSTREAM_READER, and wmadec relies
 * on the check: ff_wma_run_level_decode reads until EOB or num_coefs, so a packet
 * that never reaches EOB walked past its 64 bytes of padding without it
 * (review F-031). */
#define CONFIG_SAFE_BITSTREAM_READER 1

#define ASSERT_LEVEL 0

#endif /* WMADEC_SHIM_CONFIG_H */
