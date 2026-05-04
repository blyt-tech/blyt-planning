/* setjmp.h for the Lua rv32emu port.
 *
 * Backed by GCC's __builtin_setjmp / __builtin_longjmp, which are documented
 * to need a "5 word buffer" but in practice GCC reserves space for ra/fp/sp
 * plus a couple of words — we give it 16 words to be safe across versions.
 *
 * The longjmp value is fixed at 1 by __builtin_longjmp.  Lua does not depend
 * on the specific value (errors are stored in lua_State, not via longjmp),
 * so this is fine. */

#ifndef _SETJMP_H
#define _SETJMP_H

typedef void *jmp_buf[16];
typedef void *sigjmp_buf[16];

#define setjmp(env)         __builtin_setjmp(env)
#define longjmp(env, val)   __builtin_longjmp((env), 1)
#define _setjmp(env)        __builtin_setjmp(env)
#define _longjmp(env, val)  __builtin_longjmp((env), 1)
#define sigsetjmp(env, sv)  __builtin_setjmp(env)
#define siglongjmp(env, val) __builtin_longjmp((env), 1)

#endif /* _SETJMP_H */
