/* Spike P — libconsole stub.
 *
 * Extends Spike O's stub with:
 *   - Save/restore: if /tmp/spike-p/save-state.bin exists at startup, enters
 *     load mode (restore state → fc_cart_on_load → loop).  Otherwise normal
 *     mode (fc_cart_init → loop).
 *   - blyt_test_save_now(frame): writes state to /tmp/spike-p/save-state.bin
 *     if that directory is writable; no-op otherwise.
 *   - blyt_emit_digest(frame, hi, lo): emits "DIGEST <frame> <hi><lo>\n".
 *   - fc_console_main loops until the cart calls SYS_exit (ecall 93).
 *
 * Save file format:
 *   [0..3]    uint32 frame      — frame at which save was taken
 *   [4..1027] uint32[256] state — full state_store snapshot
 * Total: 1028 bytes.
 */

#include <stdint.h>
#include <stddef.h>

/* ── ECALL helpers ────────────────────────────────────────────────────────── */

static unsigned strlen_s(const char *s) {
    unsigned n = 0;
    while (s[n]) n++;
    return n;
}

static void write_str(const char *s) {
    unsigned len = strlen_s(s);
    register long a0 __asm__("a0") = 1;
    register const char *a1 __asm__("a1") = s;
    register unsigned a2 __asm__("a2") = len;
    register long a7 __asm__("a7") = 64;
    __asm__ volatile ("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7) : "memory");
}

static void write_hex8(uint32_t v) {
    static const char hex[] = "0123456789abcdef";
    char buf[8];
    for (int i = 7; i >= 0; i--) { buf[i] = hex[v & 0xf]; v >>= 4; }
    register long a0 __asm__("a0") = 1;
    register const char *a1 __asm__("a1") = buf;
    register unsigned a2 __asm__("a2") = 8;
    register long a7 __asm__("a7") = 64;
    __asm__ volatile ("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7) : "memory");
}

static void write_uint(uint32_t v) {
    char buf[10];
    int i = 9;
    buf[i] = 0;
    if (v == 0) { buf[--i] = '0'; }
    else { while (v) { buf[--i] = '0' + (v % 10); v /= 10; } }
    write_str(buf + i);
}

/* ── Process support ─────────────────────────────────────────────────────── */

void _exit(int code) {
    register long a0 __asm__("a0") = code;
    register long a7 __asm__("a7") = 93;
    __asm__ volatile ("ecall" : "+r"(a0) : "r"(a7) : "memory");
    __builtin_unreachable();
}
void exit(int code) { _exit(code); }
void abort(void)    { _exit(1); }

/* ── File I/O via direct ECALLs ──────────────────────────────────────────── */
/* rv32emu syscall numbers: open=1024, read=63, write=64, close=57          */

static inline long sys_open(const char *path, int flags, int mode) {
    register long a0 __asm__("a0") = (long)path;
    register long a1 __asm__("a1") = flags;
    register long a2 __asm__("a2") = mode;
    register long a7 __asm__("a7") = 1024;
    __asm__ volatile ("ecall" : "+r"(a0) : "r"(a7), "r"(a1), "r"(a2) : "memory");
    return a0;
}
static inline long sys_read(int fd, void *buf, size_t n) {
    register long a0 __asm__("a0") = fd;
    register long a1 __asm__("a1") = (long)buf;
    register long a2 __asm__("a2") = (long)n;
    register long a7 __asm__("a7") = 63;
    __asm__ volatile ("ecall" : "+r"(a0) : "r"(a7), "r"(a1), "r"(a2) : "memory");
    return a0;
}
static inline long sys_write(int fd, const void *buf, size_t n) {
    register long a0 __asm__("a0") = fd;
    register long a1 __asm__("a1") = (long)buf;
    register long a2 __asm__("a2") = (long)n;
    register long a7 __asm__("a7") = 64;
    __asm__ volatile ("ecall" : "+r"(a0) : "r"(a7), "r"(a1), "r"(a2) : "memory");
    return a0;
}
static inline long sys_close(int fd) {
    register long a0 __asm__("a0") = fd;
    register long a7 __asm__("a7") = 57;
    __asm__ volatile ("ecall" : "+r"(a0) : "r"(a7) : "memory");
    return a0;
}

/* ── State buffer ──────────────────────────────────────────────────────────
 * Indexed by (slot & 0xff) * 16 + (field & 0xff).
 * 256 slots × 16 fields = 4096 u32s; the save file saves all 256 words for
 * slot 0 (the main buffer used by the alloc cart). */
#define STATE_SLOTS 256
static uint32_t state_store[STATE_SLOTS];

uint32_t blyt32_state_get_u32(uint32_t slot, uint32_t field) {
    uint32_t idx = (slot & 0x0f) * 16 + (field & 0x0f);
    if (idx >= STATE_SLOTS) return 0;
    return state_store[idx];
}

void blyt32_state_set_u32(uint32_t slot, uint32_t field, uint32_t val) {
    uint32_t idx = (slot & 0x0f) * 16 + (field & 0x0f);
    if (idx < STATE_SLOTS) state_store[idx] = val;
}

/* ── Save file path ─────────────────────────────────────────────────────── */

#define SAVE_FILE "/tmp/spike-p/save-state.bin"

/* Save file layout: [frame u32][state_store[0..255]] = 4 + 256*4 = 1028 bytes */
typedef struct {
    uint32_t frame;
    uint32_t state[STATE_SLOTS];
} save_file_t;

static int read_save(save_file_t *out) {
    long fd = sys_open(SAVE_FILE, 0 /* O_RDONLY */, 0);
    if (fd < 0) return 0;
    /* Read in chunks of up to 512 bytes (rv32emu 4 KiB syscall buffer safe) */
    size_t total = 0;
    uint8_t *dst = (uint8_t *)out;
    size_t cap = sizeof(*out);
    while (total < cap) {
        size_t want = cap - total;
        if (want > 512) want = 512;
        long n = sys_read((int)fd, dst + total, want);
        if (n <= 0) break;
        total += (size_t)n;
    }
    sys_close((int)fd);
    return total == sizeof(*out);
}

static int write_save(uint32_t frame) {
    /* O_WRONLY|O_CREAT|O_TRUNC = 1|64|512 = 577 on Linux */
    long fd = sys_open(SAVE_FILE, 577, 0666);
    if (fd < 0) return 0;
    save_file_t s;
    s.frame = frame;
    for (int i = 0; i < STATE_SLOTS; i++) s.state[i] = state_store[i];
    uint8_t *src = (uint8_t *)&s;
    size_t total = 0, cap = sizeof(s);
    while (total < cap) {
        size_t want = cap - total;
        if (want > 512) want = 512;
        long n = sys_write((int)fd, src + total, want);
        if (n <= 0) break;
        total += (size_t)n;
    }
    sys_close((int)fd);
    return total == cap;
}

/* ── Heap init weak default ──────────────────────────────────────────────── *
 * The Rust blyt32 allocator.rs provides a strong __blyt_heap_init when the
 * alloc crate is linked (heap carts).  For carts built without alloc (e.g.
 * atomics), the Rust symbol is dead-stripped; this weak no-op ensures crt0.S
 * can always call the symbol without a linker error. */
__attribute__((weak)) void __blyt_heap_init(void *start, void *end) {
    (void)start; (void)end;
}

/* ── Cart entry point declarations ──────────────────────────────────────── */

__attribute__((weak)) extern void fc_cart_init(void);
__attribute__((weak)) extern void fc_cart_update(void);
__attribute__((weak)) extern void fc_cart_draw(void);
__attribute__((weak)) extern void fc_cart_on_load(void);

/* ── Main loop ────────────────────────────────────────────────────────────── */

void fc_console_main(void) {
    save_file_t saved;
    int load_mode = read_save(&saved);

    if (load_mode) {
        /* Restore state buffer, then fire on_load (not init). */
        for (int i = 0; i < STATE_SLOTS; i++)
            state_store[i] = saved.state[i];
        if (fc_cart_on_load) fc_cart_on_load();
    } else {
        if (fc_cart_init) fc_cart_init();
    }

    /* Loop until the cart terminates via SYS_exit(0). */
    for (;;) {
        if (fc_cart_update) fc_cart_update();
        if (fc_cart_draw)   fc_cart_draw();
    }
}

/* ── Image API stubs ──────────────────────────────────────────────────────── */

uint32_t blyt32_image_load(uint32_t resource) { return resource; }
void blyt32_image_blit(uint32_t image, int32_t x, int32_t y, uint32_t flags) {
    (void)image; (void)x; (void)y; (void)flags;
}

/* ── Audio API stubs ──────────────────────────────────────────────────────── */

uint32_t blyt32_audio_sfx_play(uint32_t resource) { return resource; }
void blyt32_audio_sfx_set_volume(uint32_t voice, float vol) {
    (void)voice; (void)vol;
}

/* ── Dev / instrumentation APIs ──────────────────────────────────────────── */

void blyt_test_save_now(uint32_t frame) {
    write_save(frame);
}

void blyt_emit_digest(uint32_t frame, uint32_t hi, uint32_t lo) {
    write_str("DIGEST ");
    write_uint(frame);
    write_str(" ");
    write_hex8(hi);
    write_hex8(lo);
    write_str("\n");
}
