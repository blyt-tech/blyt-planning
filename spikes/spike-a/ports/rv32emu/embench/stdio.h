/* Minimal stdio.h for the Embench rv32emu port.
 * Only printf and puts are ever referenced by the benchmarks (and only inside
 * guarded debug blocks that are never enabled). Implementations in libsys.c. */

#ifndef _STDIO_H
#define _STDIO_H

int printf(const char *fmt, ...);
int puts(const char *s);

#endif /* _STDIO_H */
