/* Minimal C runtime for rv32emu via direct Linux RISC-V ECALLs.
 * Provides only what CoreMark and core_portme.c require.
 */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

/* ── raw syscall ─────────────────────────────────────────────────────────── */

static inline long syscall3(long nr, long a, long b, long c)
{
    register long t7 __asm__("a7") = nr;
    register long r0 __asm__("a0") = a;
    register long r1 __asm__("a1") = b;
    register long r2 __asm__("a2") = c;
    __asm__ volatile("ecall" : "+r"(r0) : "r"(t7), "r"(r1), "r"(r2) : "memory");
    return r0;
}

/* ── write / exit ────────────────────────────────────────────────────────── */

static long rv_write(int fd, const void *buf, size_t n)
{
    return syscall3(64, fd, (long)buf, (long)n);
}

void _exit(int code)
{
    syscall3(93, code, 0, 0);
    __builtin_unreachable();
}

/* ── string helpers ──────────────────────────────────────────────────────── */

void *memset(void *s, int c, size_t n)
{
    unsigned char *p = s;
    while (n--)
        *p++ = (unsigned char)c;
    return s;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--)
        *d++ = *s++;
    return dst;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

size_t strlen(const char *s)
{
    const char *e = s;
    while (*e) e++;
    return (size_t)(e - s);
}

/* ── minimal printf (ee_printf) ──────────────────────────────────────────── */

/* Write a single character via write(1,...). Batching not needed for a
 * benchmark that prints a handful of result lines. */
static void emit(char c)
{
    rv_write(1, &c, 1);
}

static void emit_str(const char *s)
{
    rv_write(1, s, strlen(s));
}

static void emit_uint(unsigned long v, int base)
{
    char buf[32];
    int i = sizeof(buf) - 1;
    buf[i] = '\0';
    if (v == 0) { emit('0'); return; }
    const char *digits = "0123456789abcdef";
    while (v && i > 0) { buf[--i] = digits[v % base]; v /= base; }
    emit_str(buf + i);
}

static void emit_int(long v)
{
    if (v < 0) { emit('-'); v = -v; }
    emit_uint((unsigned long)v, 10);
}

/* No %f: HAS_FLOAT=0, and any double op would pull in rv32 soft-float
 * helpers (__subdf3 etc) which the Ubuntu rv64 libgcc.a doesn't provide. */
int ee_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { emit(*p); continue; }
        p++;
        /* skip width/precision/flag chars — we ignore them */
        while ((*p >= '0' && *p <= '9') || *p == '.' || *p == '-' || *p == '+'
               || *p == ' ' || *p == '#')
            p++;
        int is_long = 0;
        if (*p == 'l') { is_long = 1; p++; }
        switch (*p) {
        case 'd': case 'i':
            emit_int(is_long ? va_arg(ap, long) : va_arg(ap, int)); break;
        case 'u': emit_uint(is_long ? va_arg(ap, unsigned long)
                                    : va_arg(ap, unsigned int), 10); break;
        case 'x': case 'X':
            emit_uint(is_long ? va_arg(ap, unsigned long)
                              : va_arg(ap, unsigned int), 16); break;
        case 's': { const char *s = va_arg(ap, const char *);
                    emit_str(s ? s : "(null)"); break; }
        case 'c': emit((char)va_arg(ap, int)); break;
        case '%': emit('%'); break;
        default:  emit('%'); emit(*p); break;
        }
    }
    va_end(ap);
    return 0;
}
