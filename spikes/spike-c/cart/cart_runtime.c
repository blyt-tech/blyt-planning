/* Minimal freestanding runtime for the spike-c cart.
 * Provides syscall wrappers, _exit/exit/abort, and a tiny bump allocator
 * for the cart's l_alloc callback.  Does NOT provide Lua itself — that lives
 * in liblua54.so. */

#include <stddef.h>
#include <stdint.h>

static inline long syscall3(long nr, long a, long b, long c)
{
    register long t7 __asm__("a7") = nr;
    register long r0 __asm__("a0") = a;
    register long r1 __asm__("a1") = b;
    register long r2 __asm__("a2") = c;
    __asm__ volatile("ecall" : "+r"(r0) : "r"(t7), "r"(r1), "r"(r2) : "memory");
    return r0;
}

long write(int fd, const void *buf, size_t n)
{
    return syscall3(64, fd, (long)buf, (long)n);
}

void _exit(int code) { syscall3(93, code, 0, 0); __builtin_unreachable(); }
void exit(int code)  { _exit(code); }
void abort(void)     { _exit(1); }

/* Heap for l_alloc callback (4 MiB — Lua needs ~300KB for a minimal state) */
#define CART_HEAP_SIZE (4 * 1024 * 1024)
#define ALIGN 8
static unsigned char cart_heap[CART_HEAP_SIZE] __attribute__((aligned(ALIGN)));
static size_t cart_brk = 0;

typedef struct fblock { size_t size; struct fblock *next; } fblock;
static fblock *freelist = (fblock *)0;
static size_t align_up(size_t n) { return (n + (ALIGN-1)) & ~(size_t)(ALIGN-1); }

void *malloc(size_t n)
{
    if (!n) n = 1;
    size_t need = align_up(n + sizeof(size_t));
    fblock **pp = &freelist;
    while (*pp) {
        if ((*pp)->size >= need) {
            fblock *b = *pp; *pp = b->next;
            return (unsigned char *)b + sizeof(size_t);
        }
        pp = &(*pp)->next;
    }
    if (cart_brk + need > CART_HEAP_SIZE) return (void *)0;
    unsigned char *p = cart_heap + cart_brk;
    *(size_t *)p = need;
    cart_brk += need;
    return p + sizeof(size_t);
}

void free(void *p)
{
    if (!p) return;
    unsigned char *block = (unsigned char *)p - sizeof(size_t);
    fblock *fb = (fblock *)block;
    fb->size = *(size_t *)block;
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
    /* memcpy inline to avoid dependency */
    unsigned char *d = q;
    const unsigned char *s = p;
    for (size_t i = 0; i < old; i++) d[i] = s[i];
    free(p);
    return q;
}
