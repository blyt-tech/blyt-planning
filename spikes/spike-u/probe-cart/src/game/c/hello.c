#include "blyt.h"
#include "cart_state.h"
#include <stdio.h>
#include <stdarg.h>

/* cart-side va_arg(double) — isolates compiler va_arg from musl's vfprintf */
__attribute__((noinline)) static uint64_t va_read_double(int n, ...) {
    va_list ap;
    va_start(ap, n);
    double d = va_arg(ap, double);
    va_end(ap);
    uint64_t b;
    __builtin_memcpy(&b, &d, 8);
    return b;
}

/* s_frame is deliberately plain static state (not a state buffer) to
 * demonstrate serialising static state in on_save_state/on_load_state. */
static int s_frame;

void blyt_cart_init(void) {
    s_frame = 0;
}

/* Stage 2 ABI witness: a double crossing a (noinline) extern "C"-style call
 * boundary must arrive in fa0 under ilp32d. Emits the raw IEEE-754 bits. */
__attribute__((noinline)) static uint64_t abi_witness_double(int slot, double v) {
    uint64_t bits;
    __builtin_memcpy(&bits, &v, 8);
    return bits + (uint64_t) (slot & 0); /* keep `slot` live, value unchanged */
}

void blyt_cart_on_new_state(void) {
    blyt_buffer_alloc_slot(S_GLOBALS, &(int32_t){-1});
    int32_t slot = -1;
    blyt_buffer_alloc_slot(S_CHARACTER, &slot);
    blyt_buffer_set_u32(S_GLOBALS, 0, S_GLOBALS_PLAYER, blyt_buffer_ref(S_CHARACTER, slot));
    blyt_buffer_set_i32(S_CHARACTER, slot, S_CHARACTER_X, 160);
    blyt_buffer_set_i32(S_CHARACTER, slot, S_CHARACTER_Y, 120);
    blyt_console_debug("init player pos: 160, 120");
    {
        uint64_t bits = abi_witness_double(0, 0.5);
        char wb[64];
        snprintf(wb, sizeof(wb), "abi witness double 0.5 -> %08x%08x",
                 (uint32_t) (bits >> 32), (uint32_t) bits);
        blyt_console_debug(wb);
        char fb[64];
        volatile double dv = 0.5, dv2 = 1.5;
        snprintf(fb, sizeof(fb), "C printf double: %g %g %f", dv, dv2, dv2);
        blyt_console_debug(fb);
        char ib[64];
        snprintf(ib, sizeof(ib), "interleaved: %d %g %d", 7, dv, 9);
        blyt_console_debug(ib);
        /* Isolate the long-double (quad soft-float) path used by printf: */
        volatile long double ld = (long double) dv; /* __extenddftf2 */
        ld = ld + (long double) 1;                  /* __addtf3 + __floatsitf */
        volatile double back = (double) ld;         /* __trunctfdf2 */
        uint64_t bb;
        __builtin_memcpy(&bb, (void *) &back, 8);
        char qb[80];
        snprintf(qb, sizeof(qb), "quad roundtrip 0.5+1 bits=%08x%08x",
                 (uint32_t) (bb >> 32), (uint32_t) bb);
        blyt_console_debug(qb);
        uint64_t vb = va_read_double(1, 0.5);
        char vbuf[80];
        snprintf(vbuf, sizeof(vbuf), "cart va_arg double 0.5 bits=%08x%08x",
                 (uint32_t) (vb >> 32), (uint32_t) vb);
        blyt_console_debug(vbuf);
    }
}

/* F-extension exerciser: hits fcvt.s.w/fcvt.w.s, fadd/fsub/fmul/fdiv.s,
 * fsgnj family (fabs/copysign/neg), flt.s, and fmv.x.w (raw bit move — the
 * NaN-box-sensitive read). Output is a stable integer derived from f32 math. */
static int32_t f_probe(int n) {
    float a = (float)n * 1.5f;           /* fcvt.s.w, fmul.s  */
    float b = a / 3.0f - 0.25f;          /* fdiv.s, fsub.s    */
    float c = __builtin_fabsf(b);        /* fsgnjx.s          */
    float d = (c > 1.0f) ? c : -c;       /* flt.s, fsgnjn.s   */
    float e = __builtin_copysignf(d, a); /* fsgnj.s           */
    int32_t bits;
    __builtin_memcpy(&bits, &e, 4);      /* fmv.x.w / store   */
    int32_t trunc = (int32_t)d;          /* fcvt.w.s          */
    return bits ^ (int32_t)(trunc * 2654435761u);
}

/* D-extension exerciser: fcvt.d.w/fcvt.w.d, fadd/fsub/fmul/fdiv.d, fsgnj.d
 * family, flt.d, fld/fsd (incl. compressed c.fld/c.fsd), and fcvt.s.d. */
static int32_t d_probe(int n) {
    double a = (double)n * 1.5;          /* fcvt.d.w, fmul.d  */
    double b = a / 3.0 - 0.25;           /* fdiv.d, fsub.d    */
    double c = __builtin_fabs(b);        /* fsgnjx.d          */
    double d = (c > 1.0) ? c : -c;       /* flt.d, fsgnjn.d   */
    double e = __builtin_copysign(d, a); /* fsgnj.d           */
    int64_t bits;
    __builtin_memcpy(&bits, &e, 8);      /* fsd               */
    int32_t trunc = (int32_t)d;          /* fcvt.w.d          */
    float f = (float)e;                  /* fcvt.s.d          */
    int32_t fbits;
    __builtin_memcpy(&fbits, &f, 4);
    return (int32_t)bits ^ (int32_t)(bits >> 32) ^
           (int32_t)(trunc * 2654435761u) ^ fbits;
}

void blyt_cart_update(void) {
    s_frame++;
    if (s_frame % 10 == 0) {
        blyt_entity_ref_t player = blyt_buffer_get_u32(S_GLOBALS, 0, S_GLOBALS_PLAYER);
        if (blyt_buffer_ref_valid(S_CHARACTER, player)) {
            int32_t slot = blyt_buffer_ref_slot(player);
            int32_t x = (blyt_buffer_get_i32(S_CHARACTER, slot, S_CHARACTER_X) + 1) % 320;
            int32_t y = (blyt_buffer_get_i32(S_CHARACTER, slot, S_CHARACTER_Y) + 1) % 240;
            blyt_buffer_set_i32(S_CHARACTER, slot, S_CHARACTER_X, x);
            blyt_buffer_set_i32(S_CHARACTER, slot, S_CHARACTER_Y, y);
            char buf[96];
            snprintf(buf, sizeof(buf),
                     "update frame %d pos: %d, %d fprobe: %d dprobe: %d",
                     s_frame, x, y, f_probe(s_frame), d_probe(s_frame));
            blyt_console_debug(buf);
        }
    }
}

void blyt_cart_draw(void) {
    if (s_frame % 10 == 0) {
        int32_t slot = blyt_buffer_ref_slot(blyt_buffer_get_u32(S_GLOBALS, 0, S_GLOBALS_PLAYER));
        int32_t x = blyt_buffer_get_i32(S_CHARACTER, slot, S_CHARACTER_X);
        int32_t y = blyt_buffer_get_i32(S_CHARACTER, slot, S_CHARACTER_Y);
        char buf[64];
        snprintf(buf, sizeof(buf), "draw frame %d player pos: %d, %d", s_frame, x, y);
        blyt_console_debug(buf);
    }
}

void blyt_cart_on_save_state(void) {
    blyt_buffer_set_i32(S_GLOBALS, 0, S_GLOBALS_FRAME, s_frame);
}

void blyt_cart_on_load_state(blyt_load_info_t info) {
    (void)info;
    s_frame = blyt_buffer_get_i32(S_GLOBALS, 0, S_GLOBALS_FRAME);
}
