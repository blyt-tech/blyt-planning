/* Spike K — host file-IO via direct ecalls.  See save_io.h for rationale.
 *
 * Syscall numbers match rv32emu's SUPPORTED_SYSCALLS:
 *   open  = 1024 (a0=path_ptr, a1=flags, a2=mode)
 *   read  = 63   (a0=fd, a1=buf, a2=count)
 *   close = 57   (a0=fd)
 */

#include <stdint.h>
#include <stddef.h>

#include "save_io.h"

static inline long syscall3(long nr, long a, long b, long c)
{
    register long t7 __asm__("a7") = nr;
    register long r0 __asm__("a0") = a;
    register long r1 __asm__("a1") = b;
    register long r2 __asm__("a2") = c;
    __asm__ volatile("ecall" : "+r"(r0) : "r"(t7), "r"(r1), "r"(r2) : "memory");
    return r0;
}

static long sys_open(const char *path, int flags, int mode)
{
    return syscall3(1024, (long)path, flags, mode);
}
static long sys_read(int fd, void *buf, size_t n)
{
    return syscall3(63, fd, (long)buf, (long)n);
}
static long sys_close(int fd)
{
    return syscall3(57, fd, 0, 0);
}

long save_io_read_file(const char *path, void *buf, size_t cap)
{
    /* rv32emu's syscall_read uses a 4 KiB scratch (PREALLOC_SIZE) and has
     * a host-side buffer-overflow bug if asked for >4 KiB in one call when
     * the underlying file is shorter than the request.  We chunk every
     * read to 4096 bytes to stay strictly inside the safe path. */
    enum { CHUNK = 4096 };
    long fd = sys_open(path, 0 /* O_RDONLY */, 0);
    if (fd < 0) return -1;
    size_t total = 0;
    while (total < cap) {
        size_t want = cap - total;
        if (want > CHUNK) want = CHUNK;
        long n = sys_read((int)fd, (uint8_t *)buf + total, want);
        if (n < 0) { sys_close((int)fd); return -1; }
        if (n == 0) break;
        total += (size_t)n;
    }
    sys_close((int)fd);
    return (long)total;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

long save_io_parse_hex(const char *src, size_t len, uint8_t *dst, size_t cap)
{
    /* Skip leading whitespace. */
    size_t i = 0;
    while (i < len && is_space(src[i])) i++;

    /* Optional `BUFFER <frame> ` prefix.  Matched literally; if the prefix
     * looks malformed we treat the whole input as raw hex (the prefix is
     * a transport convenience, not part of the wire format). */
    if (i + 7 < len &&
        src[i+0] == 'B' && src[i+1] == 'U' && src[i+2] == 'F' &&
        src[i+3] == 'F' && src[i+4] == 'E' && src[i+5] == 'R' &&
        src[i+6] == ' ') {
        i += 7;
        /* skip frame number */
        while (i < len && src[i] >= '0' && src[i] <= '9') i++;
        /* skip the space before the hex blob */
        while (i < len && is_space(src[i])) i++;
    }

    size_t out = 0;
    while (i < len) {
        if (is_space(src[i])) { i++; continue; }
        if (i + 1 >= len) return -1;
        int hi = hex_digit(src[i]);
        int lo = hex_digit(src[i+1]);
        if (hi < 0 || lo < 0) {
            /* Allow trailing whitespace after a complete hex run. */
            if (is_space(src[i])) { i++; continue; }
            return -1;
        }
        if (out >= cap) return -1;
        dst[out++] = (uint8_t)((hi << 4) | lo);
        i += 2;
    }
    return (long)out;
}
