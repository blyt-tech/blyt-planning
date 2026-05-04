/* stdlib.h stand-in for the Lua rv32emu port.
 * Allocator routes through our internal free-list malloc (lua_runtime.c). */

#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 0x7fffffff

void  *malloc (size_t);
void  *calloc (size_t, size_t);
void  *realloc(void *, size_t);
void   free   (void *);
void   abort  (void);
void   exit   (int) __attribute__((noreturn));
int    atoi   (const char *);
long   atol   (const char *);
long   strtol (const char *, char **, int);
unsigned long strtoul(const char *, char **, int);
float  strtof (const char *, char **);
double strtod (const char *, char **);
char  *getenv (const char *);
int    rand   (void);
void   srand  (unsigned int);
int    abs    (int);
long   labs   (long);
void   qsort  (void *, size_t, size_t, int (*)(const void *, const void *));

#endif /* _STDLIB_H */
