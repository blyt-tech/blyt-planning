/* Minimal C runtime for the Lua spike-c shared library (rv32emu, RV32IMFC ilp32f).
 *
 * Compiled with -fPIC as part of liblua54.so.  Provides everything Lua 5.4
 * needs from the C library that we are not willing to pull in via a real libc.
 *
 * Differences from spike-b's lua_runtime.c:
 *  - Compiled with -fPIC (position-independent code)
 *  - _exit/exit/abort are omitted (ECALLs belong in the cart, not the .so)
 *  - softfloat-glue symbols are provided by softfloat_stubs.c
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

/* ── raw syscalls ────────────────────────────────────────────────────────── */

static inline long syscall3(long nr, long a, long b, long c)
{
    register long t7 __asm__("a7") = nr;
    register long r0 __asm__("a0") = a;
    register long r1 __asm__("a1") = b;
    register long r2 __asm__("a2") = c;
    __asm__ volatile("ecall" : "+r"(r0) : "r"(t7), "r"(r1), "r"(r2) : "memory");
    return r0;
}

static long rv_write(int fd, const void *buf, size_t n)
{
    return syscall3(64, fd, (long)buf, (long)n);
}

/* clock_gettime via Linux RV32 syscall #403 (clock_gettime64).  Returns ns. */
struct kernel_timespec { int64_t tv_sec; int64_t tv_nsec; };
uint64_t now_ns(void)
{
    struct kernel_timespec ts = {0, 0};
    /* CLOCK_MONOTONIC = 1 */
    syscall3(403, 1, (long)&ts, 0);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* ── string ──────────────────────────────────────────────────────────────── */

void *memset(void *s, int c, size_t n)
{
    unsigned char *p = s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

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
    return (void *)0;
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

size_t strlen(const char *s)
{
    const char *e = s;
    while (*e) e++;
    return (size_t)(e - s);
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    if (!n) return 0;
    while (--n && *a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strcoll(const char *a, const char *b) { return strcmp(a, b); }

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
    return (c == '\0') ? (char *)s : (char *)0;
}

char *strrchr(const char *s, int c)
{
    const char *last = (void *)0;
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

char *strpbrk(const char *s, const char *accept)
{
    while (*s) {
        const char *a;
        for (a = accept; *a; a++) if (*a == *s) return (char *)s;
        s++;
    }
    return (char *)0;
}

char *strstr(const char *hay, const char *nee)
{
    if (!*nee) return (char *)hay;
    size_t nl = strlen(nee);
    for (; *hay; hay++) if (!strncmp(hay, nee, nl)) return (char *)hay;
    return (char *)0;
}

/* errno is referenced by Lua's strerror path and luaconf math hooks. */
int errno = 0;
char *strerror(int e)
{
    static char buf[24];
    static const char prefix[] = "errno=";
    size_t i;
    for (i = 0; prefix[i]; i++) buf[i] = prefix[i];
    /* tiny int→decimal */
    if (e < 0) { buf[i++] = '-'; e = -e; }
    char tmp[12]; int n = 0;
    if (e == 0) tmp[n++] = '0';
    while (e) { tmp[n++] = '0' + (e % 10); e /= 10; }
    while (n--) buf[i++] = tmp[n];
    buf[i] = '\0';
    return buf;
}

/* ── stdlib basics ───────────────────────────────────────────────────────── */

int  abs(int v)    { return v < 0 ? -v : v; }
long labs(long v)  { return v < 0 ? -v : v; }

int atoi(const char *s)
{
    int neg = 0, v = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return neg ? -v : v;
}
long atol(const char *s) { return atoi(s); }

long strtol(const char *s, char **end, int base)
{
    long v = 0; int neg = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2; base = 16;
    } else if (base == 0 && *s == '0') {
        s++; base = 8;
    } else if (base == 0) {
        base = 10;
    }
    while (*s) {
        int d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'z') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') d = *s - 'A' + 10;
        else break;
        if (d >= base) break;
        v = v * base + d;
        s++;
    }
    if (end) *end = (char *)s;
    return neg ? -v : v;
}
unsigned long strtoul(const char *s, char **end, int base) { return (unsigned long)strtol(s, end, base); }

/* strtof: decimal-only, tolerates +/-, ., e/E.  Adequate for Lua's
 * `lua_str2number` callers in the benchmarks (which don't write huge
 * literals). */
float strtof(const char *s, char **end)
{
    while (*s == ' ' || *s == '\t') s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    float v = 0.0f;
    while (*s >= '0' && *s <= '9') { v = v * 10.0f + (float)(*s - '0'); s++; }
    if (*s == '.') {
        s++;
        float scale = 0.1f;
        while (*s >= '0' && *s <= '9') {
            v += (float)(*s - '0') * scale;
            scale *= 0.1f;
            s++;
        }
    }
    if (*s == 'e' || *s == 'E') {
        s++;
        int eneg = 0, e = 0;
        if (*s == '-') { eneg = 1; s++; } else if (*s == '+') s++;
        while (*s >= '0' && *s <= '9') { e = e * 10 + (*s - '0'); s++; }
        float f = 1.0f;
        for (int i = 0; i < e; i++) f *= 10.0f;
        if (eneg) v /= f; else v *= f;
    }
    if (end) *end = (char *)s;
    return neg ? -v : v;
}
double strtod(const char *s, char **end) { return (double)strtof(s, end); }

/* getenv: no environment in the guest. */
char *getenv(const char *name) { (void)name; return (char *)0; }

/* qsort: simple insertion sort.  Lua only calls it from table.sort. */
void qsort(void *base, size_t n, size_t sz,
           int (*cmp)(const void *, const void *))
{
    char *a = (char *)base;
    char tmp[64];
    if (sz > sizeof tmp) { syscall3(93, 99, 0, 0); }
    for (size_t i = 1; i < n; i++) {
        size_t j = i;
        while (j > 0 && cmp(a + (j - 1) * sz, a + j * sz) > 0) {
            memcpy(tmp,             a + (j - 1) * sz, sz);
            memcpy(a + (j - 1) * sz, a + j * sz,       sz);
            memcpy(a + j * sz,       tmp,              sz);
            j--;
        }
    }
}

int rand(void) { return 0; }
void srand(unsigned int s) { (void)s; }

/* ── malloc: free-list, first-fit, no coalescing ─────────────────────────── */

#define HEAP_SIZE (8 * 1024 * 1024)
#define ALIGN     8

static unsigned char heap[HEAP_SIZE] __attribute__((aligned(ALIGN)));
static size_t        brk = 0;

typedef struct fblock {
    size_t          size;     /* block size including header */
    struct fblock  *next;
} fblock;

static fblock *freelist = (fblock *)0;

static size_t align_up(size_t n) { return (n + (ALIGN - 1)) & ~(size_t)(ALIGN - 1); }

void *malloc(size_t n)
{
    if (!n) n = 1;
    size_t need = align_up(n + sizeof(size_t));

    /* first-fit on free list */
    fblock **pp = &freelist;
    while (*pp) {
        if ((*pp)->size >= need) {
            fblock *b = *pp;
            *pp = b->next;
            return (unsigned char *)b + sizeof(size_t);
        }
        pp = &(*pp)->next;
    }
    /* bump */
    if (brk + need > HEAP_SIZE) return (void *)0;
    unsigned char *p = heap + brk;
    *(size_t *)p = need;
    brk += need;
    return p + sizeof(size_t);
}

void *calloc(size_t n, size_t sz)
{
    size_t total = n * sz;
    void *p = malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void free(void *p)
{
    if (!p) return;
    unsigned char *block = (unsigned char *)p - sizeof(size_t);
    size_t sz = *(size_t *)block;
    fblock *fb = (fblock *)block;
    fb->size = sz;
    fb->next = freelist;
    freelist = fb;
}

void *realloc(void *p, size_t n)
{
    if (!p) return malloc(n);
    if (!n) { free(p); return (void *)0; }
    unsigned char *block = (unsigned char *)p - sizeof(size_t);
    size_t old_total = *(size_t *)block;
    size_t old = old_total - sizeof(size_t);
    if (n <= old) return p;
    void *q = malloc(n);
    if (!q) return (void *)0;
    memcpy(q, p, old);
    free(p);
    return q;
}

/* ── locale stub ─────────────────────────────────────────────────────────── */

struct lconv {
    char *decimal_point;
    char *thousands_sep;
    char *grouping;
    char *int_curr_symbol;
    char *currency_symbol;
    char *mon_decimal_point;
    char *mon_thousands_sep;
    char *mon_grouping;
    char *positive_sign;
    char *negative_sign;
};

struct lconv *localeconv(void)
{
    static char dot[] = ".";
    static char empty[] = "";
    static struct lconv lc;
    lc.decimal_point     = dot;
    lc.thousands_sep     = empty;
    lc.grouping          = empty;
    lc.int_curr_symbol   = empty;
    lc.currency_symbol   = empty;
    lc.mon_decimal_point = dot;
    lc.mon_thousands_sep = empty;
    lc.mon_grouping      = empty;
    lc.positive_sign     = empty;
    lc.negative_sign     = empty;
    return &lc;
}
char *setlocale(int cat, const char *loc) { (void)cat; (void)loc; return "C"; }

/* ── signal stub ─────────────────────────────────────────────────────────── */

typedef void (*sighandler_t)(int);
sighandler_t signal(int s, sighandler_t h) { (void)s; (void)h; return (sighandler_t)0; }

/* ── time ────────────────────────────────────────────────────────────────── */

long clock(void)        { return (long)(now_ns() / 1000ULL); }
long time(long *t)      { long v = (long)(now_ns() / 1000000000ULL); if (t) *t = v; return v; }
double difftime(long a, long b) { return (double)(a - b); }
int clock_gettime(int clk, struct kernel_timespec *ts) {
    (void)clk;
    return (int)syscall3(403, 1, (long)ts, 0);
}

/* ── math ────────────────────────────────────────────────────────────────── */

float sqrtf(float x)
{
    float r;
    __asm__ volatile("fsqrt.s %0, %1" : "=f"(r) : "f"(x));
    return r;
}
double sqrt(double x)
{
    float xf = (float)x;
    return (double)sqrtf(xf);
}

float fabsf(float x)
{
    union { float f; uint32_t u; } v = { .f = x };
    v.u &= 0x7FFFFFFFu;
    return v.f;
}
double fabs(double x)
{
    union { double d; uint64_t u; } v = { .d = x };
    v.u &= (uint64_t)0x7FFFFFFFFFFFFFFFULL;
    return v.d;
}

float floorf(float x)
{
    int i = (int)x;
    if ((float)i > x) i--;
    return (float)i;
}
float ceilf(float x)
{
    int i = (int)x;
    if ((float)i < x) i++;
    return (float)i;
}
float truncf(float x) { return (float)(int)x; }
double floor(double x) { return (double)floorf((float)x); }
double ceil (double x) { return (double)ceilf ((float)x); }
double trunc(double x) { return (double)truncf((float)x); }

float fmodf(float x, float y)
{
    if (y == 0.0f) return 0.0f;
    int q = (int)(x / y);
    return x - (float)q * y;
}
double fmod(double x, double y) { return (double)fmodf((float)x, (float)y); }

float modff(float x, float *ip) { float i = truncf(x); *ip = i; return x - i; }
double modf(double x, double *ip) { double i = trunc(x); *ip = i; return x - i; }

float ldexpf(float x, int e) { while (e > 0) { x *= 2.0f; e--; } while (e < 0) { x *= 0.5f; e++; } return x; }
double ldexp(double x, int e) { return (double)ldexpf((float)x, e); }
float frexpf(float x, int *e) { *e = 0; return x; }
double frexp(double x, int *e) { *e = 0; return x; }

#define STUB1F(name) float name##f(float x) { (void)x; return 0.0f; } \
                     double name(double x)  { (void)x; return 0.0; }
#define STUB2F(name) float name##f(float a, float b) { (void)a; (void)b; return 0.0f; } \
                     double name(double a, double b)  { (void)a; (void)b; return 0.0; }
STUB1F(sin)
STUB1F(cos)
STUB1F(tan)
STUB1F(asin)
STUB1F(acos)
STUB1F(atan)
STUB1F(exp)
STUB1F(log)
STUB1F(log2)
STUB1F(log10)
STUB2F(atan2)
STUB2F(pow)
STUB2F(hypot)

/* ── snprintf / printf ───────────────────────────────────────────────────── */

typedef struct { char *buf; size_t cap; size_t pos; } pctx;

static void pput(pctx *c, char ch)
{
    if (c->buf && c->pos + 1 < c->cap) c->buf[c->pos] = ch;
    c->pos++;
}

static void pputn(pctx *c, const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++) pput(c, s[i]);
}

static void emit_uint(pctx *c, unsigned long v, int base, int upper,
                      int width, int prec, int flag_zero, int flag_left)
{
    char tmp[33];
    int n = 0;
    const char *digs = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (v == 0 && prec != 0) tmp[n++] = '0';
    while (v) { tmp[n++] = digs[v % (unsigned)base]; v /= (unsigned)base; }
    int npad = 0;
    int len = n;
    if (prec > len) { /* zero-pad to precision */
        for (int i = 0; i < prec - len; i++) tmp[n++] = '0';
        len = n;
    }
    if (width > len) npad = width - len;
    if (!flag_left && !flag_zero) for (int i = 0; i < npad; i++) pput(c, ' ');
    if (!flag_left && flag_zero && prec < 0) for (int i = 0; i < npad; i++) pput(c, '0');
    while (n--) pput(c, tmp[n]);
    if (flag_left) for (int i = 0; i < npad; i++) pput(c, ' ');
}

static void emit_int(pctx *c, long v,
                     int width, int prec, int flag_zero, int flag_left,
                     int flag_plus, int flag_space)
{
    int neg = 0;
    unsigned long u;
    if (v < 0) { neg = 1; u = (unsigned long)(-v); } else u = (unsigned long)v;

    int signw = (neg || flag_plus || flag_space) ? 1 : 0;
    int eff_width = width - signw;
    if (eff_width < 0) eff_width = 0;

    if (flag_zero && !flag_left && prec < 0) {
        if (neg) pput(c, '-');
        else if (flag_plus) pput(c, '+');
        else if (flag_space) pput(c, ' ');
        emit_uint(c, u, 10, 0, eff_width, prec, 1, 0);
        return;
    }

    char num[34];
    int n = 0;
    if (u == 0 && prec != 0) num[n++] = '0';
    while (u) { num[n++] = '0' + (u % 10); u /= 10; }
    if (prec > n) for (int i = 0; i < prec - n; i++) num[n++] = '0';
    int len = n + signw;
    int pad = width > len ? width - len : 0;
    if (!flag_left) for (int i = 0; i < pad; i++) pput(c, ' ');
    if (neg) pput(c, '-');
    else if (flag_plus) pput(c, '+');
    else if (flag_space) pput(c, ' ');
    while (n--) pput(c, num[n]);
    if (flag_left) for (int i = 0; i < pad; i++) pput(c, ' ');
}

static double pow10d(int e)
{
    double v = 1.0;
    if (e >= 0) for (int i = 0; i < e; i++) v *= 10.0;
    else        for (int i = 0; i < -e; i++) v *= 0.1;
    return v;
}

static void emit_double_fixed(pctx *c, double v, int prec, int flag_plus, int flag_space)
{
    if (prec < 0) prec = 6;
    int neg = 0;
    union { double d; uint64_t u; } cv = { .d = v };
    if (cv.u >> 63) { neg = 1; v = -v; }
    uint64_t exp_bits = (cv.u >> 52) & 0x7ffULL;
    if (exp_bits == 0x7ff) {
        if (cv.u & ((1ULL << 52) - 1ULL)) { pputn(c, "nan", 3); return; }
        if (neg) pput(c, '-'); else if (flag_plus) pput(c, '+'); else if (flag_space) pput(c, ' ');
        pputn(c, "inf", 3); return;
    }

    double scale = pow10d(prec);
    double scaled = v * scale + 0.5;
    unsigned long ipart = (unsigned long)(v);
    double remainder = (v - (double)ipart) * scale + 0.5;
    unsigned long fpart = (unsigned long)remainder;
    unsigned long pow = 1;
    for (int i = 0; i < prec; i++) pow *= 10;
    if (fpart >= pow) { fpart -= pow; ipart++; }

    if (neg) pput(c, '-'); else if (flag_plus) pput(c, '+'); else if (flag_space) pput(c, ' ');

    char ibuf[20]; int ni = 0;
    if (ipart == 0) ibuf[ni++] = '0';
    while (ipart) { ibuf[ni++] = '0' + (ipart % 10); ipart /= 10; }
    while (ni--) pput(c, ibuf[ni]);

    if (prec > 0) {
        pput(c, '.');
        char fbuf[20]; int nf = 0;
        for (int i = 0; i < prec; i++) {
            fbuf[nf++] = '0' + (int)(fpart % 10);
            fpart /= 10;
        }
        while (nf--) pput(c, fbuf[nf]);
    }
    (void)scaled;
}

static void emit_double_g(pctx *c, double v, int prec, int flag_plus, int flag_space)
{
    if (prec <= 0) prec = 6;
    pctx tmp;
    char tbuf[64];
    tmp.buf = tbuf; tmp.cap = sizeof tbuf; tmp.pos = 0;
    emit_double_fixed(&tmp, v, prec - 1, flag_plus, flag_space);
    size_t len = tmp.pos;
    if (len >= tmp.cap) len = tmp.cap - 1;
    size_t dot = len;
    for (size_t i = 0; i < len; i++) if (tbuf[i] == '.') { dot = i; break; }
    if (dot < len) {
        size_t end = len;
        while (end > dot + 1 && tbuf[end - 1] == '0') end--;
        if (end == dot + 1) end = dot;
        len = end;
    }
    pputn(c, tbuf, len);
}

int vsnprintf(char *buf, size_t cap, const char *fmt, va_list ap)
{
    pctx c = { buf, cap, 0 };
    while (*fmt) {
        if (*fmt != '%') { pput(&c, *fmt++); continue; }
        fmt++;
        int flag_left = 0, flag_plus = 0, flag_zero = 0, flag_space = 0, flag_hash = 0;
        for (;;) {
            if      (*fmt == '-') flag_left  = 1;
            else if (*fmt == '+') flag_plus  = 1;
            else if (*fmt == '0') flag_zero  = 1;
            else if (*fmt == ' ') flag_space = 1;
            else if (*fmt == '#') flag_hash  = 1;
            else break;
            fmt++;
        }
        int width = 0;
        if (*fmt == '*') { width = va_arg(ap, int); fmt++; }
        else while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        int prec = -1;
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            if (*fmt == '*') { prec = va_arg(ap, int); fmt++; }
            else while (*fmt >= '0' && *fmt <= '9') prec = prec * 10 + (*fmt++ - '0');
        }
        int len_l = 0;
        while (*fmt == 'l' || *fmt == 'h' || *fmt == 'z' || *fmt == 't' || *fmt == 'L' || *fmt == 'j') {
            if (*fmt == 'l' || *fmt == 'z' || *fmt == 't' || *fmt == 'j') len_l = 1;
            fmt++;
        }
        char conv = *fmt++;
        switch (conv) {
        case 'd': case 'i': {
            long v = len_l ? va_arg(ap, long) : va_arg(ap, int);
            emit_int(&c, v, width, prec, flag_zero, flag_left, flag_plus, flag_space);
            break;
        }
        case 'u': {
            unsigned long v = len_l ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int);
            emit_uint(&c, v, 10, 0, width, prec, flag_zero, flag_left);
            break;
        }
        case 'x': {
            unsigned long v = len_l ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int);
            if (flag_hash && v) pputn(&c, "0x", 2);
            emit_uint(&c, v, 16, 0, width, prec, flag_zero, flag_left);
            break;
        }
        case 'X': {
            unsigned long v = len_l ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int);
            if (flag_hash && v) pputn(&c, "0X", 2);
            emit_uint(&c, v, 16, 1, width, prec, flag_zero, flag_left);
            break;
        }
        case 'o': {
            unsigned long v = len_l ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int);
            emit_uint(&c, v, 8, 0, width, prec, flag_zero, flag_left);
            break;
        }
        case 'p': {
            void *p = va_arg(ap, void *);
            pputn(&c, "0x", 2);
            emit_uint(&c, (unsigned long)p, 16, 0, 0, 8, 1, 0);
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            size_t n = 0;
            while (s[n] && (prec < 0 || n < (size_t)prec)) n++;
            int pad = width > (int)n ? width - (int)n : 0;
            if (!flag_left) for (int i = 0; i < pad; i++) pput(&c, ' ');
            pputn(&c, s, n);
            if (flag_left)  for (int i = 0; i < pad; i++) pput(&c, ' ');
            break;
        }
        case 'c': {
            int ch = va_arg(ap, int);
            int pad = width > 1 ? width - 1 : 0;
            if (!flag_left) for (int i = 0; i < pad; i++) pput(&c, ' ');
            pput(&c, (char)ch);
            if (flag_left)  for (int i = 0; i < pad; i++) pput(&c, ' ');
            break;
        }
        case 'f': case 'F': {
            double v = va_arg(ap, double);
            emit_double_fixed(&c, v, prec, flag_plus, flag_space);
            break;
        }
        case 'g': case 'G':
        case 'e': case 'E': {
            double v = va_arg(ap, double);
            emit_double_g(&c, v, prec < 0 ? 6 : prec, flag_plus, flag_space);
            break;
        }
        case '%':
            pput(&c, '%');
            break;
        default:
            pput(&c, '%');
            pput(&c, conv);
            break;
        }
    }
    if (c.buf && c.cap > 0) c.buf[c.pos < c.cap ? c.pos : c.cap - 1] = '\0';
    return (int)c.pos;
}

int snprintf(char *buf, size_t cap, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(buf, cap, fmt, ap);
    va_end(ap);
    return n;
}

int sprintf(char *buf, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(buf, 0x7fffffff, fmt, ap);
    va_end(ap);
    return n;
}

int vprintf(const char *fmt, va_list ap)
{
    char buf[1024];
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    if (n > (int)sizeof buf - 1) n = (int)sizeof buf - 1;
    rv_write(1, buf, (size_t)n);
    return n;
}

int printf(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    int n = vprintf(fmt, ap);
    va_end(ap);
    return n;
}

int puts(const char *s)
{
    size_t n = strlen(s);
    rv_write(1, s, n);
    rv_write(1, "\n", 1);
    return 0;
}

/* ── stdio FILE surface ──────────────────────────────────────────────────── */

struct __FILE { int fd; };
static struct __FILE _stdout_obj = { 1 };
static struct __FILE _stderr_obj = { 2 };
static struct __FILE _stdin_obj  = { 0 };
struct __FILE *const stdout = &_stdout_obj;
struct __FILE *const stderr = &_stderr_obj;
struct __FILE *const stdin  = &_stdin_obj;

int fputc(int c, struct __FILE *f) { unsigned char ch = (unsigned char)c; rv_write(f ? f->fd : 1, &ch, 1); return c; }
int putc (int c, struct __FILE *f) { return fputc(c, f); }
int putchar(int c)                 { return fputc(c, stdout); }

int fputs(const char *s, struct __FILE *f)
{
    rv_write(f ? f->fd : 1, s, strlen(s));
    return 0;
}

int fflush(struct __FILE *f) { (void)f; return 0; }

size_t fwrite(const void *p, size_t sz, size_t n, struct __FILE *f)
{
    size_t total = sz * n;
    rv_write(f ? f->fd : 1, p, total);
    return n;
}

size_t fread (void *p, size_t sz, size_t n, struct __FILE *f) { (void)p; (void)sz; (void)n; (void)f; return 0; }
int    feof   (struct __FILE *f) { (void)f; return 1; }
int    ferror (struct __FILE *f) { (void)f; return 0; }
void   clearerr(struct __FILE *f) { (void)f; }
int    fgetc  (struct __FILE *f) { (void)f; return -1; }
int    getc   (struct __FILE *f) { return fgetc(f); }
int    getchar(void)             { return fgetc(stdin); }
char  *fgets  (char *s, int n, struct __FILE *f) { (void)s; (void)n; (void)f; return (char *)0; }
int    fseek  (struct __FILE *f, long o, int w) { (void)f; (void)o; (void)w; return -1; }
long   ftell  (struct __FILE *f) { (void)f; return -1; }
int    setvbuf(struct __FILE *f, char *b, int m, size_t s) { (void)f; (void)b; (void)m; (void)s; return 0; }
int    ungetc (int c, struct __FILE *f) { (void)c; (void)f; return -1; }
struct __FILE *fopen   (const char *p, const char *m) { (void)p; (void)m; return (struct __FILE *)0; }
struct __FILE *freopen (const char *p, const char *m, struct __FILE *f) { (void)p; (void)m; (void)f; return (struct __FILE *)0; }
int    fclose  (struct __FILE *f) { (void)f; return 0; }
struct __FILE *tmpfile(void) { return (struct __FILE *)0; }
char  *tmpnam (char *s) { (void)s; return (char *)0; }
int    remove (const char *p) { (void)p; return -1; }
int    rename (const char *o, const char *n) { (void)o; (void)n; return -1; }

int fprintf(struct __FILE *f, const char *fmt, ...)
{
    char buf[1024];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof buf - 1) n = (int)sizeof buf - 1;
    rv_write(f ? f->fd : 1, buf, (size_t)n);
    return n;
}
int vfprintf(struct __FILE *f, const char *fmt, va_list ap)
{
    char buf[1024];
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    if (n > (int)sizeof buf - 1) n = (int)sizeof buf - 1;
    rv_write(f ? f->fd : 1, buf, (size_t)n);
    return n;
}
int sscanf(const char *s, const char *fmt, ...) { (void)s; (void)fmt; return 0; }

/* libgcc 64-bit divmod helpers.  The riscv64-linux-gnu toolchain only
 * ships rv64 multilib, so we cannot link libgcc into an rv32 .so.  Lua
 * emits a small number of 64-bit / 32-bit divides; supply them here. */

static unsigned long long udivmoddi4(unsigned long long n, unsigned long long d,
                                     unsigned long long *rem)
{
    if (d == 0) { if (rem) *rem = 0; return 0; }
    if (d > n) { if (rem) *rem = n; return 0; }
    int shift = 0;
    while ((d << 1) <= n && ((d << 1) >> 1) == d) { d <<= 1; shift++; }
    unsigned long long q = 0;
    while (shift >= 0) {
        if (n >= d) { n -= d; q |= (1ULL << shift); }
        d >>= 1; shift--;
    }
    if (rem) *rem = n;
    return q;
}

unsigned long long __udivdi3(unsigned long long a, unsigned long long b)
{
    return udivmoddi4(a, b, 0);
}
unsigned long long __umoddi3(unsigned long long a, unsigned long long b)
{
    unsigned long long r; udivmoddi4(a, b, &r); return r;
}
long long __divdi3(long long a, long long b)
{
    int neg = (a < 0) ^ (b < 0);
    unsigned long long ua = a < 0 ? -(unsigned long long)a : (unsigned long long)a;
    unsigned long long ub = b < 0 ? -(unsigned long long)b : (unsigned long long)b;
    unsigned long long q = udivmoddi4(ua, ub, 0);
    return neg ? -(long long)q : (long long)q;
}
long long __moddi3(long long a, long long b)
{
    int neg = a < 0;
    unsigned long long ua = a < 0 ? -(unsigned long long)a : (unsigned long long)a;
    unsigned long long ub = b < 0 ? -(unsigned long long)b : (unsigned long long)b;
    unsigned long long r;
    udivmoddi4(ua, ub, &r);
    return neg ? -(long long)r : (long long)r;
}
