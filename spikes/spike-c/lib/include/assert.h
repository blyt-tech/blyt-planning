/* assert.h for the Lua rv32emu port.
 * Lua compiles with NDEBUG; assert() is a no-op. */

#ifndef _ASSERT_H
#define _ASSERT_H

#define assert(expr) ((void)0)
#define static_assert _Static_assert

#endif /* _ASSERT_H */
