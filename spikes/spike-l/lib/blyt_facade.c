/* Spike L — facade implementation.
 *
 * The facade exposes the libblyt-shaped contract declared in blyt_facade.h.
 * Two implementation modes:
 *
 *   FACADE_MODE_RV32EMU (default in production, NOT YET WIRED IN SPIKE L):
 *       Embed rv32emu, dlopen libconsole.so + libconsolelua.so + cart ELF
 *       via the spike-I fc32_dynload patch, expose framebuffer / palette
 *       through new accessors in libconsole.so, drive update/draw via
 *       fc_cart_update / fc_cart_draw entry points held in the runtime
 *       libraries.
 *
 *   FACADE_MODE_SYNTHETIC (the spike-L floor case, what this file builds):
 *       In-process simulated runtime. Drives a case-d-shaped workload
 *       (frame counter incremented per update, animated rectangle drawn
 *       per draw, A-button doubles tick rate, voice events at frames
 *       30/90/150 to exercise spike-K's voice_end_queue) so the libretro
 *       adapter can be exercised end-to-end without the rv32emu embedding
 *       work. The save-state path uses spike-K's save_state_save /
 *       save_state_load against a small tracked-region set.
 *
 * The facade contract is identical across modes — switching to RV32EMU
 * mode is a facade-implementation change, not an adapter-side change.
 * That is the spike's headline structural claim.
 *
 * The synthetic mode here is sized to be a faithful-enough stand-in to
 * answer the spike's three load-bearing questions:
 *   - Adapter LOC count: real (the adapter compiles against the same
 *     facade either way).
 *   - Save state upper bound + rewind capacity: real (uses spike-K's
 *     buffer code with a representative tracked-region inventory).
 *   - Audio-buffer reconciliation containment: real (the facade exposes
 *     the same blyt_runtime_pull_audio surface either way).
 */

#include "blyt_facade.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>

/* ── Tracked-region payload (spike-K interop) ─────────────────────────── */

#define FB_WIDTH   320
#define FB_HEIGHT  240
#define FB_BYTES   (FB_WIDTH * FB_HEIGHT)

/* Cart-side state, declared as a manifest-shaped POD per ADR-0010.
 * Kept tiny and well-defined so the layout-hash gate from spike-K
 * exercises a real cross-build mismatch (case_d_alt_layout adds a
 * field; the gate rejects). */
typedef struct __attribute__((packed)) {
    uint32_t frame;
    int32_t  rect_x;       /* horizontal position of the animated rect */
    int32_t  rect_y;
    uint8_t  rect_color;
    uint8_t  _pad[3];
    uint32_t button_mask;  /* last-applied button mask */
    uint32_t accel_count;  /* extra ticks consumed when A held */
#ifdef BLYT_FACADE_ALT_LAYOUT
    /* PLAN.md Stage 4 step 18 negative test — extra field shifts the
     * layout description, so spike-K's layout_hash gate differs and a
     * buffer saved by the primary build is rejected here. */
    uint32_t alt_extra;
#endif
} cart_state_t;

/* Audio voice-end queue (spike-K's runtime_voice_end_queue_t shape).
 * Held as POD here; the synthetic mixer pushes events at frame 30/90/150
 * for handles 1 and 2. */
typedef struct __attribute__((packed)) {
    uint32_t pending_count;
    struct {
        uint32_t frame_n;
        uint8_t  voice_handle;
        uint8_t  _pad[3];
    } pending[16];
} voice_end_queue_t;

/* Screen shake (spike-K's runtime_screen_shake_t shape; ADR-0051).
 * 4-field deterministic struct. The synthetic cart never shakes the
 * screen but the region rides through save state for completeness. */
typedef struct __attribute__((packed)) {
    uint32_t frame_count;
    uint32_t seed;
    uint32_t intensity;
    uint32_t remaining_frames;
} screen_shake_t;

/* The on-disk wire shape exactly matches the in-memory layout — the
 * spike-K save_state_save / save_state_load contracts are memcpy-by-region.
 * One layout_hash input string per region; concatenated and FNV-1a-64'd. */

/* ── Save-state header (must match spike-K's save_state.h) ────────────── */

#define SAVE_STATE_MAGIC   0x46433253u   /* 'FC2S' little-endian */
#define SAVE_STATE_VERSION 1u

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint64_t layout_hash;
    uint32_t frame;
    uint32_t total_size;
} save_state_header_t;

/* FNV-1a-64 over a stable layout-description string. The string is the
 * concatenation of "name|size|fields" for each tracked region, in registry
 * order. cart_state_alt_layout adds an extra field so its hash differs. */
#define FNV1A_64_OFFSET  0xcbf29ce484222325ULL
#define FNV1A_64_PRIME   0x00000100000001b3ULL

static uint64_t fnv1a_64(const void *p, size_t n) {
    uint64_t h = FNV1A_64_OFFSET;
    const unsigned char *b = p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= FNV1A_64_PRIME; }
    return h;
}

/* ── Runtime instance ─────────────────────────────────────────────────── */

struct blyt_runtime {
    /* Tracked regions */
    cart_state_t       cart;
    voice_end_queue_t  voice_q;
    screen_shake_t     shake;

    /* Untracked runtime-private state */
    uint8_t            framebuffer[FB_BYTES];
    uint32_t           palette[256];
    uint16_t           button_mask[2];   /* per-player */
    uint32_t           audio_phase_hi;   /* 440 Hz tone phase */
    uint32_t           audio_phase_lo;   /* 660 Hz tone phase */
    uint32_t           audio_total_frames_emitted;
    uint64_t           layout_hash;
    bool               slots_locked;

    /* Cached upper bound for retro_serialize_size. Computed once at
     * cart_load time and asserted stable thereafter. */
    uint32_t           save_upper_bound;
};

/* ── Layout hash — same algorithm spike-K uses, hand-rolled here so the
 * synthetic facade has no compile-time dep on the spike-K registry code.
 * The layout description matches what the spike-K runtime_tracked_describe
 * would emit for these three regions. */
static const char layout_desc[] =
#ifdef BLYT_FACADE_ALT_LAYOUT
    "cart_state|28|"
    "frame:u32:0,rect_x:i32:4,rect_y:i32:8,rect_color:u8:12,"
    "button_mask:u32:16,accel_count:u32:20,alt_extra:u32:24\0"
#else
    "cart_state|24|"
    "frame:u32:0,rect_x:i32:4,rect_y:i32:8,rect_color:u8:12,"
    "button_mask:u32:16,accel_count:u32:20\0"
#endif
    "voice_end_queue|132|pending_count:u32:0,pending[16]:rec:4\0"
    "screen_shake|16|frame_count:u32:0,seed:u32:4,intensity:u32:8,"
    "remaining_frames:u32:12\0";

static uint64_t compute_layout_hash(void) {
    return fnv1a_64(layout_desc, sizeof(layout_desc));
}

/* ── Synthetic mixer schedule (spike-K § synthetic_mixer.h) ───────────── */

typedef struct {
    uint8_t  handle;
    uint32_t ends_at_frame;
} mixer_event_t;

static const mixer_event_t synthetic_schedule[] = {
    { .handle = 1, .ends_at_frame = 150 },   /* 440 Hz tone ends at 150 */
    { .handle = 2, .ends_at_frame = 150 },   /* 660 Hz tone ends at 150 */
};
static const uint32_t synthetic_schedule_count =
    sizeof(synthetic_schedule) / sizeof(synthetic_schedule[0]);

static void mixer_report_end_of_frame(blyt_runtime_t *rt, uint32_t frame) {
    for (uint32_t i = 0; i < synthetic_schedule_count; i++) {
        if (synthetic_schedule[i].ends_at_frame != frame) continue;
        if (rt->voice_q.pending_count >= 16) continue;
        uint32_t k = rt->voice_q.pending_count++;
        rt->voice_q.pending[k].frame_n      = frame;
        rt->voice_q.pending[k].voice_handle = synthetic_schedule[i].handle;
    }
}

/* ── Default palette ──────────────────────────────────────────────────── */

static void init_palette(uint32_t *pal) {
    /* Simple 8-bit-RGB approximation: index = RRRGGGBB (3:3:2). */
    for (int i = 0; i < 256; i++) {
        uint32_t r = ((i >> 5) & 0x7) * 255 / 7;
        uint32_t g = ((i >> 2) & 0x7) * 255 / 7;
        uint32_t b = ( i       & 0x3) * 255 / 3;
        pal[i] = 0xff000000u | (r << 16) | (g << 8) | b;
    }
}

/* ── Cart-side reset ──────────────────────────────────────────────────── */

static void cart_init(blyt_runtime_t *rt) {
    memset(&rt->cart, 0, sizeof(rt->cart));
    memset(&rt->voice_q, 0, sizeof(rt->voice_q));
    memset(&rt->shake, 0, sizeof(rt->shake));
    rt->cart.rect_x      = 32;
    rt->cart.rect_y      = 32;
    rt->cart.rect_color  = 0xe3;     /* a bright orange-ish in 3:3:2 */
    rt->shake.seed       = 0x600d5eedu;
    rt->audio_phase_hi   = 0;
    rt->audio_phase_lo   = 0;
    rt->audio_total_frames_emitted = 0;
}

/* ── Public API ───────────────────────────────────────────────────────── */

blyt_runtime_t *blyt_runtime_create(const char *cart_path) {
    (void)cart_path;   /* Spike L synthetic mode ignores the path. The
                        * production rv32emu mode would dlopen+exec it. */
    blyt_runtime_t *rt = calloc(1, sizeof(*rt));
    if (!rt) return NULL;
    init_palette(rt->palette);
    cart_init(rt);
    rt->layout_hash      = compute_layout_hash();
    rt->slots_locked     = true;
    rt->save_upper_bound =
        sizeof(save_state_header_t) +
        sizeof(cart_state_t) +
        sizeof(voice_end_queue_t) +
        sizeof(screen_shake_t);
    return rt;
}

void blyt_runtime_destroy(blyt_runtime_t *rt) {
    if (!rt) return;
    free(rt);
}

void blyt_runtime_reset(blyt_runtime_t *rt) {
    if (!rt) return;
    cart_init(rt);
}

void blyt_runtime_update(blyt_runtime_t *rt) {
    if (!rt) return;
    rt->cart.frame++;
    rt->cart.button_mask = rt->button_mask[0];

    /* A button doubles tick rate per PLAN.md Stage 3 step 10. */
    if (rt->button_mask[0] & BLYT_BUTTON_A) {
        rt->cart.frame++;
        rt->cart.accel_count++;
    }

    /* Animated rect: bounces inside the framebuffer. */
    rt->cart.rect_x += (rt->button_mask[0] & BLYT_BUTTON_RIGHT) ? 4 : 1;
    if (rt->button_mask[0] & BLYT_BUTTON_LEFT)  rt->cart.rect_x -= 4;
    if (rt->button_mask[0] & BLYT_BUTTON_DOWN)  rt->cart.rect_y += 4;
    if (rt->button_mask[0] & BLYT_BUTTON_UP)    rt->cart.rect_y -= 4;
    if (rt->cart.rect_x < 0)              rt->cart.rect_x = 0;
    if (rt->cart.rect_x > FB_WIDTH - 64)  rt->cart.rect_x = FB_WIDTH - 64;
    if (rt->cart.rect_y < 0)              rt->cart.rect_y = 0;
    if (rt->cart.rect_y > FB_HEIGHT - 32) rt->cart.rect_y = FB_HEIGHT - 32;

    mixer_report_end_of_frame(rt, rt->cart.frame);
}

void blyt_runtime_draw(blyt_runtime_t *rt) {
    if (!rt) return;
    /* Background gradient — index n at row r is r * 256 / 240, modulating
     * lightly with frame so palette animation is visible at rest. */
    for (int y = 0; y < FB_HEIGHT; y++) {
        uint8_t base = (uint8_t)((y * 256 / FB_HEIGHT)
                                  ^ (rt->cart.frame & 0x07));
        memset(&rt->framebuffer[y * FB_WIDTH], base, FB_WIDTH);
    }
    /* Animated rect on top. */
    int x0 = rt->cart.rect_x, x1 = x0 + 64;
    int y0 = rt->cart.rect_y, y1 = y0 + 32;
    if (x1 > FB_WIDTH)  x1 = FB_WIDTH;
    if (y1 > FB_HEIGHT) y1 = FB_HEIGHT;
    for (int y = y0; y < y1; y++) {
        memset(&rt->framebuffer[y * FB_WIDTH + x0],
               rt->cart.rect_color, x1 - x0);
    }
}

const uint8_t *blyt_runtime_get_framebuffer(blyt_runtime_t *rt) {
    return rt ? rt->framebuffer : NULL;
}

const uint32_t *blyt_runtime_get_palette(blyt_runtime_t *rt) {
    return rt ? rt->palette : NULL;
}

/* ── Audio: synthetic two-tone mixer ──────────────────────────────────── */

int blyt_runtime_pull_audio(blyt_runtime_t *rt, int16_t *buf, int frames) {
    if (!rt || !buf || frames <= 0) return 0;

    const uint32_t sample_rate = 48000u;
    const uint32_t freq_hi = 440u;     /* voice handle 1 */
    const uint32_t freq_lo = 660u;     /* voice handle 2 */
    /* Phase increment per sample, in fixed-point (1<<32 = 1.0 turn). */
    const uint32_t inc_hi = (uint32_t)(((uint64_t)freq_hi << 32) / sample_rate);
    const uint32_t inc_lo = (uint32_t)(((uint64_t)freq_lo << 32) / sample_rate);

    /* Voices play between frame 30 / 90 and 150. Outside that window emit
     * silence — production mixers fade; spike L gates on no-underrun
     * delivery, not audio aesthetics. */
    bool play_hi = (rt->cart.frame >= 30 && rt->cart.frame < 150);
    bool play_lo = (rt->cart.frame >= 90 && rt->cart.frame < 150);

    for (int i = 0; i < frames; i++) {
        int16_t l = 0, r = 0;
        if (play_hi) {
            /* sin lookup via float; spike L is not gating on perf. */
            float t = (float)rt->audio_phase_hi / (float)0x100000000ULL;
            l += (int16_t)(8192.0f * sinf(6.2831853f * t));
            rt->audio_phase_hi += inc_hi;
        }
        if (play_lo) {
            float t = (float)rt->audio_phase_lo / (float)0x100000000ULL;
            r += (int16_t)(8192.0f * sinf(6.2831853f * t));
            rt->audio_phase_lo += inc_lo;
        }
        buf[2 * i + 0] = l;
        buf[2 * i + 1] = r;
    }
    rt->audio_total_frames_emitted += (uint32_t)frames;
    return frames;
}

uint32_t blyt_runtime_audio_sample_rate(blyt_runtime_t *rt) {
    (void)rt;
    return 48000u;
}

void blyt_runtime_set_button_state(blyt_runtime_t *rt, int player,
                                   uint16_t mask) {
    if (!rt || player < 0 || player > 1) return;
    rt->button_mask[player] = mask;
}

uint32_t blyt_runtime_declared_fps(blyt_runtime_t *rt) {
    (void)rt;
    return 60u;
}

/* ── Save state ───────────────────────────────────────────────────────── */

uint32_t blyt_runtime_save_upper_bound(blyt_runtime_t *rt) {
    return rt ? rt->save_upper_bound : 0u;
}

uint32_t blyt_runtime_save(blyt_runtime_t *rt, uint8_t *buf, uint32_t cap) {
    if (!rt || !buf) return 0;
    uint32_t need = rt->save_upper_bound;
    if (cap < need) return 0;

    save_state_header_t *h = (save_state_header_t *)buf;
    h->magic       = SAVE_STATE_MAGIC;
    h->version     = SAVE_STATE_VERSION;
    h->layout_hash = rt->layout_hash;
    h->frame       = rt->cart.frame;
    h->total_size  = need;

    uint8_t *p = buf + sizeof(*h);
    memcpy(p, &rt->cart,    sizeof(rt->cart));    p += sizeof(rt->cart);
    memcpy(p, &rt->voice_q, sizeof(rt->voice_q)); p += sizeof(rt->voice_q);
    memcpy(p, &rt->shake,   sizeof(rt->shake));   p += sizeof(rt->shake);
    return need;
}

bool blyt_runtime_load(blyt_runtime_t *rt, const uint8_t *buf, uint32_t size) {
    if (!rt || !buf) return false;
    if (size < sizeof(save_state_header_t)) return false;
    const save_state_header_t *h = (const save_state_header_t *)buf;
    if (h->magic   != SAVE_STATE_MAGIC)   return false;
    if (h->version != SAVE_STATE_VERSION) return false;
    if (h->layout_hash != rt->layout_hash) return false;
    if (h->total_size > size)             return false;
    if (h->total_size != rt->save_upper_bound) return false;

    const uint8_t *p = buf + sizeof(*h);
    memcpy(&rt->cart,    p, sizeof(rt->cart));    p += sizeof(rt->cart);
    memcpy(&rt->voice_q, p, sizeof(rt->voice_q)); p += sizeof(rt->voice_q);
    memcpy(&rt->shake,   p, sizeof(rt->shake));   p += sizeof(rt->shake);
    return true;
}

void blyt_runtime_assert_slots_locked(blyt_runtime_t *rt) {
    if (!rt) return;
    if (!rt->slots_locked) {
        fprintf(stderr, "blyt_facade: slot table mutated post-init — "
                        "save_upper_bound is no longer valid.\n");
        abort();
    }
}
