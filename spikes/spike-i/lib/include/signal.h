/* signal.h stub for the Lua rv32emu port — Lua only references signal in
 * the standalone interpreter (lua.c), which we do not build. */

#ifndef _SIGNAL_H
#define _SIGNAL_H

typedef int sig_atomic_t;
typedef void (*sighandler_t)(int);
#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)
#define SIGINT  2

sighandler_t signal(int signum, sighandler_t handler);

#endif /* _SIGNAL_H */
