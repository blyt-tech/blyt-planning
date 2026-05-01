/* Additional C library functions for the Embench rv32emu port.
 * syscalls.c (shared with CoreMark) provides: memset, memcpy, strcmp, strlen,
 * _exit, and ee_printf.  This file provides everything else Embench needs. */

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include "ctype.h"  /* inline isdigit, isspace, etc. */

/* ── string functions ────────────────────────────────────────────────────── */

void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    if (d < s || d >= s + n) {
        while (n--) *d++ = *s++;
    } else {
        d += n; s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = s;
    while (n--) {
        if (*p == (unsigned char)c) return (void *)p;
        p++;
    }
    return NULL;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *p = a, *q = b;
    while (n--) {
        if (*p != *q) return (int)*p - (int)*q;
        p++; q++;
    }
    return 0;
}

char *strcpy(char *dst, const char *src)
{
    char *d = dst;
    while ((*d++ = *src++));
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n)
{
    char *d = dst;
    while (n && (*d++ = *src++)) n--;
    while (n-- > 1) *d++ = '\0';
    return dst;
}

int strncmp(const char *a, const char *b, size_t n)
{
    if (!n) return 0;
    while (--n && *a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

char *strcat(char *dst, const char *src)
{
    char *d = dst;
    while (*d) d++;
    while ((*d++ = *src++));
    return dst;
}

char *strchr(const char *s, int c)
{
    for (; *s; s++) if (*s == (char)c) return (char *)s;
    return (c == '\0') ? (char *)s : NULL;
}

char *strrchr(const char *s, int c)
{
    const char *last = NULL;
    for (; *s; s++) if (*s == (char)c) last = s;
    return (c == '\0') ? (char *)s : (char *)last;
}

size_t strspn(const char *s, const char *accept)
{
    size_t n = 0;
    while (s[n]) {
        const char *a;
        for (a = accept; *a && *a != s[n]; a++);
        if (!*a) break;
        n++;
    }
    return n;
}

size_t strcspn(const char *s, const char *reject)
{
    size_t n = 0;
    while (s[n]) {
        const char *r;
        for (r = reject; *r && *r != s[n]; r++);
        if (*r) break;
        n++;
    }
    return n;
}

/* ── integer math ────────────────────────────────────────────────────────── */

int abs(int v)    { return v < 0 ? -v : v; }
long labs(long v) { return v < 0 ? -v : v; }

/* ── stdlib basics ───────────────────────────────────────────────────────── */

void _exit(int code);
void abort(void) { _exit(1); }

int atoi(const char *s)
{
    int neg = 0, v = 0;
    while (isspace((unsigned char)*s)) s++;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    while (isdigit((unsigned char)*s)) v = v * 10 + (*s++ - '0');
    return neg ? -v : v;
}

/* ── math ────────────────────────────────────────────────────────────────── */

/* Integer square root via Newton-Raphson.
 * wikisort calls sqrt(long) and truncates to long — the input is always a
 * non-negative integer, so we work entirely in integer space.
 * double <-> long conversion emits __fixdfsi / __floatsidf via SoftFloat. */
double sqrt(double x)
{
    if (x <= 0.0) return 0.0;
    long n = (long)x;
    if (n <= 1) return (double)n;
    long r = n >> 1;
    for (;;) {
        long r2 = (r + n / r) >> 1;
        if (r2 >= r) break;
        r = r2;
    }
    return (double)r;
}

/* Clear the IEEE 754 sign bit directly — no floating-point comparison emitted,
 * so no softfloat helpers needed for these trivial cases. */
double fabs(double x)
{
    union { double d; uint64_t u; } v;
    v.d = x;
    v.u &= (uint64_t)0x7FFFFFFFFFFFFFFF;
    return v.d;
}
float fabsf(float x)
{
    union { float f; uint32_t u; } v;
    v.f = x;
    v.u &= 0x7FFFFFFFu;
    return v.f;
}

/* ── stdio ───────────────────────────────────────────────────────────────── */

/* printf: all benchmark calls are inside #ifdef DEBUG / #ifdef TEST guards
 * that are never enabled.  This stub satisfies the linker. */
int printf(const char *fmt, ...)
{
    (void)fmt;
    va_list ap;
    va_start(ap, fmt);
    va_end(ap);
    return 0;
}

/* puts: similarly guarded in the benchmarks; stub is sufficient. */
int puts(const char *s) { (void)s; return 0; }
