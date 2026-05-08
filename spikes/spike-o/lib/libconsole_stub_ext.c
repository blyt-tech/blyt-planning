/* Spike O — extended libconsole stub.
 *
 * Provides stub implementations of the three API families used by the toy
 * Rust cart (image, audio, state buffer) plus the ABI-witness function for
 * Stage 2 and a minimal per-frame digest emitter for Stage 3.
 *
 * All stubs echo their arguments to stdout so the Makefile harness can verify
 * argument values without running a full frame-state digest.  The ABI witness
 * for Stage 2 is blyt32_audio_sfx_set_volume: it prints the raw IEEE 754
 * bit pattern of the `vol` argument so the Makefile can confirm 3f000000
 * (0.5f in ilp32f hard-float ABI).
 *
 * Built as libconsole_spike_o.so (RV32IMFC, ilp32f, -fPIC, -nostdlib).
 */

#include <stdint.h>

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

/* Minimal printf-like: supports %u and %08x only. */
static void write_hex8(uint32_t v) {
    static const char hex[] = "0123456789abcdef";
    char buf[8];
    for (int i = 7; i >= 0; i--) {
        buf[i] = hex[v & 0xf];
        v >>= 4;
    }
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

/* ── Cart entry point dispatcher ─────────────────────────────────────────── */

__attribute__((weak)) extern void fc_cart_init(void);
__attribute__((weak)) extern void fc_cart_update(void);
__attribute__((weak)) extern void fc_cart_draw(void);

#define NFRAMES 30

void fc_console_main(void) {
    fc_cart_init();
    for (int i = 0; i < NFRAMES; i++) {
        fc_cart_update();
        fc_cart_draw();
    }
    write_str("OK\n");
}

/* ── Image API stubs ──────────────────────────────────────────────────────── */

uint32_t blyt32_image_load(uint32_t resource) {
    write_str("IMAGE_LOAD res=");
    write_hex8(resource);
    write_str("\n");
    return resource;   /* echo resource ID as image handle */
}

void blyt32_image_blit(uint32_t image, int32_t x, int32_t y, uint32_t flags) {
    write_str("IMAGE_BLIT img=");
    write_hex8(image);
    write_str(" x=");
    write_uint((uint32_t)x);
    write_str(" y=");
    write_uint((uint32_t)y);
    write_str(" fl=");
    write_hex8(flags);
    write_str("\n");
}

/* ── Audio API stubs ──────────────────────────────────────────────────────── */

uint32_t blyt32_audio_sfx_play(uint32_t resource) {
    write_str("SFX_PLAY res=");
    write_hex8(resource);
    write_str("\n");
    return resource;   /* echo resource ID as voice handle */
}

/*
 * ABI witness for Stage 2.  The `vol` argument is f32; under ilp32f hard-
 * float ABI it arrives in fa0 (FPR).  We copy the raw IEEE 754 bits via
 * memcpy to avoid any float-to-integer conversion that could change the bits.
 * The Makefile greps for "3f000000" (IEEE 754 for 0.5f) to confirm the ABI.
 */
void blyt32_audio_sfx_set_volume(uint32_t voice, float vol) {
    /* union pun: defined behaviour in C99 and accepted by GCC/Clang */
    union { float f; uint32_t u; } pun = { .f = vol };
    uint32_t bits = pun.u;
    write_str("SET_VOL voice=");
    write_hex8(voice);
    write_str(" vol=");
    write_hex8(bits);
    write_str("\n");
}

/* ── State buffer API stubs ──────────────────────────────────────────────── */

/* Minimal in-process state buffer: one slot, one field, one u32. */
static uint32_t state_store[256];   /* indexed by (slot * 256 + field_lo) */

uint32_t blyt32_state_get_u32(uint32_t slot, uint32_t field) {
    uint32_t idx = (slot & 0xff) * 16 + (field & 0xff);
    return state_store[idx];
}

void blyt32_state_set_u32(uint32_t slot, uint32_t field, uint32_t val) {
    uint32_t idx = (slot & 0xff) * 16 + (field & 0xff);
    state_store[idx] = val;
}

/* ── Simple per-frame digest emitter (Stage 3 gate) ─────────────────────── */

/* FNV-1a-64 over two u32 words: frame index and the current S_CTR value.
 * Output: "DIGEST <frame> <hex16>\n" matching Spike D's format.
 * The Rust cart calls this via extern "C" at each fc_cart_update. */
void frame_state_emit_digest_simple(uint32_t frame, uint32_t val) {
    /* FNV-1a-64 */
    uint64_t h = 0xcbf29ce484222325ULL;
    const uint64_t prime = 0x00000100000001b3ULL;

    /* hash frame (4 bytes, little-endian) */
    for (int i = 0; i < 4; i++) {
        h ^= (uint64_t)((frame >> (i * 8)) & 0xff);
        h *= prime;
    }
    /* hash val (4 bytes, little-endian) */
    for (int i = 0; i < 4; i++) {
        h ^= (uint64_t)((val >> (i * 8)) & 0xff);
        h *= prime;
    }

    uint32_t hi = (uint32_t)(h >> 32);
    uint32_t lo = (uint32_t)(h & 0xffffffffu);

    write_str("DIGEST ");
    write_uint(frame);
    write_str(" ");
    write_hex8(hi);
    write_hex8(lo);
    write_str("\n");
}
