/* Minimal stdlib.h for the Embench rv32emu port.
 * malloc/free/rand/srand are declared but never called directly — benchmarks
 * use beebsc's _beebs variants.  abort and atoi are provided by libsys.c. */

#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>

void  abort(void);
int   atoi(const char *);
int   abs(int);
long  labs(long);

/* Declared for header compatibility; not implemented — use beebsc variants. */
void *malloc (size_t size);
void *calloc (size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void  free   (void *ptr);
int   rand   (void);
void  srand  (unsigned int seed);

#endif /* _STDLIB_H */
