/* Minimal string.h for the Embench rv32emu port.
 * Overrides the system string.h (found first via -I.) so the cross-compiler
 * does not pull in glibc sysroot infrastructure not present in the container.
 * Implementations are in syscalls.c (memset/memcpy/strcmp/strlen) and
 * libsys.c (memmove/memchr/memcmp/str*). */

#ifndef _STRING_H
#define _STRING_H

#include <stddef.h>

void  *memset (void *, int, size_t);
void  *memcpy (void *, const void *, size_t);
void  *memmove(void *, const void *, size_t);
void  *memchr (const void *, int, size_t);
int    memcmp (const void *, const void *, size_t);
int    strcmp (const char *, const char *);
int    strncmp(const char *, const char *, size_t);
char  *strcpy (char *, const char *);
char  *strncpy(char *, const char *, size_t);
char  *strcat (char *, const char *);
char  *strchr (const char *, int);
char  *strrchr(const char *, int);
size_t strlen (const char *);
size_t strspn (const char *, const char *);
size_t strcspn(const char *, const char *);

#endif /* _STRING_H */
