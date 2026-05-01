/* assert.h for the Embench rv32emu port.
 * Benchmarks compile with -DNDEBUG; assert() is a no-op in all cases.
 * beebsc.h defines assert_beebs() separately for internal use. */

#ifndef _ASSERT_H
#define _ASSERT_H

/* Unconditional no-op: we always build with NDEBUG for the benchmark run. */
#define assert(expr) ((void)0)

#endif /* _ASSERT_H */
