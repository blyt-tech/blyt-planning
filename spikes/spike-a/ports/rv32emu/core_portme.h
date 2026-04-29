/*
 * CoreMark port for rv32emu: RV32IMFC guest running under rv32emu.
 * Timing via Linux clock_gettime64 (syscall 403, CLOCK_MONOTONIC).
 * No libc dependency — syscalls.c provides all runtime support.
 */
#ifndef CORE_PORTME_H
#define CORE_PORTME_H

#include <stddef.h>
#include <stdint.h>

/* ── data types ─────────────────────────────────────────────────────────── */

typedef int16_t    ee_s16;
typedef uint16_t   ee_u16;
typedef int32_t    ee_s32;
typedef float      ee_f32;
typedef uint8_t    ee_u8;
typedef uint32_t   ee_u32;
typedef uintptr_t  ee_ptr_int;
typedef size_t     ee_size_t;

#define align_mem(x) (void *)(4 + (((ee_ptr_int)(x) - 1) & ~3))

/* ── timing ─────────────────────────────────────────────────────────────── */

/* Microseconds, 32-bit. Differences fit in 32 bits (max 71 minutes) and 32-bit
 * division is inlined by GCC — 64-bit would require __udivdi3 from libgcc.a. */
typedef uint32_t CORE_TICKS;
#define CORETIMETYPE CORE_TICKS

/* ── CoreMark config ─────────────────────────────────────────────────────── */

/* HAS_FLOAT=0: CoreMark uses integer arithmetic throughout (secs_ret == ee_u32).
 * The Ubuntu riscv64-linux-gnu toolchain has no rv32 multilib, so soft-double
 * helpers (__subdf3 etc) are unavailable. We emit a high-precision elapsed-ns
 * diagnostic line ourselves alongside CoreMark's integer-seconds output. */
#define HAS_FLOAT           0
#define HAS_TIME_H          0
#define USE_CLOCK           0
#define HAS_STDIO           0
#define HAS_PRINTF          0

#define SEED_METHOD         SEED_VOLATILE
#define MEM_METHOD          MEM_STATIC
#define MULTITHREAD         1
#define USE_PTHREAD         0
#define USE_FORK            0
#define USE_SOCKET          0
#define MAIN_HAS_NOARGC     0
#define MAIN_HAS_NORETURN   0

#ifndef MEM_LOCATION
#define MEM_LOCATION        "STATIC"
#endif
#ifndef COMPILER_VERSION
#define COMPILER_VERSION    "GCC " __VERSION__
#endif
#ifndef COMPILER_FLAGS
#define COMPILER_FLAGS      FLAGS_STR
#endif

typedef struct { ee_u8 portable_id; } core_portable;

extern ee_u32 default_num_contexts;

void portable_init(core_portable *p, int *argc, char *argv[]);
void portable_fini(core_portable *p);

int ee_printf(const char *fmt, ...);

#if !defined(PROFILE_RUN) && !defined(PERFORMANCE_RUN) && !defined(VALIDATION_RUN)
#if   (TOTAL_DATA_SIZE == 1200)
#define PROFILE_RUN     1
#elif (TOTAL_DATA_SIZE == 2000)
#define PERFORMANCE_RUN 1
#else
#define VALIDATION_RUN  1
#endif
#endif

#endif /* CORE_PORTME_H */
