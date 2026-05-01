/* Embench board support for rv32emu.
 * Timing via Linux clock_gettime64 (syscall 403, CLOCK_MONOTONIC) — the same
 * approach used by the CoreMark port so we know rv32emu handles it correctly.
 *
 * start_trigger() / stop_trigger() record wall-clock timestamps.
 * Results are printed by stop_trigger() so that every benchmark binary
 * is self-reporting: `rv32emu embench-foo.elf` prints one result line.
 */

#include "support.h"
#include <stdint.h>

/* Provided by syscalls.c (same compilation unit via board.c → boardsupport.c
 * textual inclusion, then linked with syscalls.c). */
int ee_printf(const char *fmt, ...);

/* ── clock_gettime64 ECALL ─────────────────────────────────────────────────── */

#define CLOCK_MONOTONIC    1
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

static void read_now(uint32_t *sec, uint32_t *nsec)
{
    struct timespec64 ts;
    syscall2(SYS_clock_gettime64, CLOCK_MONOTONIC, (long)&ts);
    *sec  = (uint32_t)ts.tv_sec;
    *nsec = (uint32_t)ts.tv_nsec;
}

/* ── timing state ───────────────────────────────────────────────────────────── */

static uint32_t t_start_sec, t_start_nsec;
static uint32_t t_stop_sec,  t_stop_nsec;

/* ── board interface ────────────────────────────────────────────────────────── */

void initialise_board(void) { /* nothing to initialise in the emulator */ }

void __attribute__((noinline)) start_trigger(void)
{
    read_now(&t_start_sec, &t_start_nsec);
}

void __attribute__((noinline)) stop_trigger(void)
{
    read_now(&t_stop_sec, &t_stop_nsec);

    uint32_t dsec  = t_stop_sec  - t_start_sec;
    int32_t  dnsec = (int32_t)(t_stop_nsec - t_start_nsec);
    if (dnsec < 0) { dsec -= 1; dnsec += 1000000000; }

    uint32_t us = dsec * 1000000U + (uint32_t)dnsec / 1000U;
    uint32_t ms_int  = us / 1000U;
    uint32_t ms_frac = (us % 1000U);

    /* Print a single result line; BENCHMARK_NAME is set by the Makefile. */
    ee_printf("Embench : %-20s : %u.%03u ms\n",
              BENCHMARK_NAME, ms_int, ms_frac);
}
