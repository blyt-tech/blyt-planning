/* Spike H — Stage 4: cgroups v2 cpu.max throttle smoke-test.
 *
 * A pure CPU loop with no syscalls (apart from clock_gettime at the start
 * and end via musl).  Run twice — once outside any cgroup, once inside a
 * cgroup with cpu.max "500 5000" (10% CPU).  The throttled run should take
 * roughly 10× longer; that ratio validates the kernel mechanism is active.
 *
 * Exit prints a single SUMMARY line:
 *   SUMMARY busy_loop iters=N elapsed_us=T
 * which the harness compares between the two runs.
 */

#include <stdint.h>
#include <stdio.h>
#include <time.h>

#ifndef BUSY_LOOP_ITERS
#  define BUSY_LOOP_ITERS 500000000ULL
#endif

static uint64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

int main(void)
{
    uint64_t t0 = now_us();
    /* `volatile` so the compiler can't elide the loop. */
    volatile uint64_t n = 0;
    while (n < BUSY_LOOP_ITERS) n++;
    uint64_t t1 = now_us();
    printf("SUMMARY busy_loop iters=%llu elapsed_us=%llu\n",
           (unsigned long long)BUSY_LOOP_ITERS,
           (unsigned long long)(t1 - t0));
    return 0;
}
