/*
 * CoreMark port for rv32emu.
 * Timing: Linux clock_gettime64 (syscall 403, CLOCK_MONOTONIC).
 *
 * All elapsed-time math is 32-bit microseconds. Differences between two
 * timestamps fit comfortably (4.29e9 us ≈ 71 minutes) and 64-bit division
 * helpers (__udivdi3) are not in the rv64 libgcc.a we link against.
 */
#include "coremark.h"
#include "core_portme.h"
#include <stdint.h>

/* ── clock_gettime64 via ECALL ───────────────────────────────────────────── */

#define CLOCK_MONOTONIC 1
#define SYS_clock_gettime64 403

struct timespec64 {
    int64_t tv_sec;
    int64_t tv_nsec;
};

static inline long syscall2(long nr, long a, long b)
{
    register long t7 __asm__("a7") = nr;
    register long r0 __asm__("a0") = a;
    register long r1 __asm__("a1") = b;
    __asm__ volatile("ecall" : "+r"(r0) : "r"(t7), "r"(r1) : "memory");
    return r0;
}

/* Read the current time as a (sec, nsec) pair, keeping only the low 32 bits
 * of each component. Wraparound is fine: differences are computed modulo 2^32
 * and any sane benchmark elapsed time fits in 32 bits. */
static void read_now(uint32_t *sec, uint32_t *nsec)
{
    struct timespec64 ts;
    syscall2(SYS_clock_gettime64, CLOCK_MONOTONIC, (long)&ts);
    *sec  = (uint32_t)ts.tv_sec;
    *nsec = (uint32_t)ts.tv_nsec;
}

/* ── volatile seeds (barebones-style) ────────────────────────────────────── */

#if VALIDATION_RUN
volatile ee_s32 seed1_volatile = 0x3415;
volatile ee_s32 seed2_volatile = 0x3415;
volatile ee_s32 seed3_volatile = 0x66;
#endif
#if PERFORMANCE_RUN
volatile ee_s32 seed1_volatile = 0x0;
volatile ee_s32 seed2_volatile = 0x0;
volatile ee_s32 seed3_volatile = 0x66;
#endif
#if PROFILE_RUN
volatile ee_s32 seed1_volatile = 0x8;
volatile ee_s32 seed2_volatile = 0x8;
volatile ee_s32 seed3_volatile = 0x8;
#endif
volatile ee_s32 seed4_volatile = ITERATIONS;
volatile ee_s32 seed5_volatile = 0;

/* ── timing interface ────────────────────────────────────────────────────── */

static uint32_t start_sec, start_nsec, stop_sec, stop_nsec;

void start_time(void) { read_now(&start_sec, &start_nsec); }
void stop_time(void)  { read_now(&stop_sec,  &stop_nsec);  }

/* CORE_TICKS is uint32_t microseconds. */
CORE_TICKS get_time(void)
{
    uint32_t dsec  = stop_sec  - start_sec;
    int32_t  dnsec = (int32_t)(stop_nsec - start_nsec);
    /* Borrow if nsec went negative across a second boundary. */
    if (dnsec < 0) { dsec -= 1; dnsec += 1000000000; }
    return dsec * 1000000U + (uint32_t)dnsec / 1000U;
}

/* secs_ret is double (HAS_FLOAT=1); the conversion goes through softfloat
 * via __floatunsidf and __divdf3 in softfloat-glue.c. */
secs_ret time_in_secs(CORE_TICKS ticks_us)
{
    return (secs_ret)ticks_us / 1000000.0;
}

/* ── boilerplate ─────────────────────────────────────────────────────────── */

ee_u32 default_num_contexts = 1;

void portable_init(core_portable *p, int *argc, char *argv[])
{
    (void)argc; (void)argv;
    if (sizeof(ee_ptr_int) != sizeof(ee_u8 *))
        ee_printf("ERROR! ee_ptr_int size mismatch\n");
    if (sizeof(ee_u32) != 4)
        ee_printf("ERROR! ee_u32 is not 32 bits\n");
    p->portable_id = 1;
}

/* Microsecond diagnostic alongside CoreMark's float-formatted seconds. */
void portable_fini(core_portable *p)
{
    ee_printf("Elapsed (us)     : %u\n", get_time());
    p->portable_id = 0;
}
