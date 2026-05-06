/* Spike L — minimum-viable libblyt facade.
 *
 * The libretro adapter (host/retro_core.c) talks to the runtime through
 * exactly these entry points. The set is the *lower bound* on libblyt's
 * production API: the facade exists to surface what the libretro adapter
 * actually needs from the runtime, before the production libblyt API is
 * frozen. Per ADR-0033, this is the boundary the libretro core composes
 * against.
 *
 * Facade implementation responsibilities (lib/blyt_facade.c):
 *   - Embed rv32emu and load the cart ELF + libconsole.so + libconsolelua.so
 *     via spike-I's fc32_dynload patch.
 *   - Expose framebuffer + palette pointers from libconsole's runtime state.
 *   - Drive blyt_runtime_update / _draw by invoking fc_cart_update /
 *     fc_cart_draw inside rv32emu (one tick = one update + one draw).
 *   - Wrap spike-K's save_state_save / save_state_load against the cart's
 *     tracked-region registry.
 *   - Drive spike-K's synthetic_mixer to generate audio samples.
 *
 * Spike-L scope: the facade is the only place the rv32emu host interface
 * lives. The adapter never touches rv32emu directly — keeping the
 * libretro-vs-frontend reasoning isolated to the adapter and the
 * runtime-internal embedding isolated to the facade.
 */

#ifndef BLYT_FACADE_H
#define BLYT_FACADE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct blyt_runtime blyt_runtime_t;

/* ── Lifecycle ─────────────────────────────────────────────────────────── */

/* Create a runtime from a cart ELF path. Returns NULL on failure.
 * Spike-L embeds rv32emu and loads the cart via fc32_dynload; cart_path
 * is the absolute path to the RV32 cart ELF (e.g. /spike-i/cases/case_d/cart_d).
 * The runtime libraries libconsole.so / libconsolelua.so are resolved
 * relative to the libpath baked into the facade at build time. */
blyt_runtime_t *blyt_runtime_create(const char *cart_path);

/* Destroy a runtime. Idempotent on NULL. */
void            blyt_runtime_destroy(blyt_runtime_t *rt);

/* Reset to fresh-init state without re-loading the cart ELF. Calls the
 * cart's init function under the runtime contract (ADR-0010). Used by
 * retro_unserialize before save_state_load — the spike-K contract is
 * "init, then load". */
void            blyt_runtime_reset(blyt_runtime_t *rt);

/* ── Per-tick interface (1/60 s tick under ADR-0037) ───────────────────── */

/* Run one cart update tick. Drives fc_cart_update inside the embedded
 * rv32emu. The cart's frame counter, RNG, and tracked regions advance
 * by exactly one frame.  */
void            blyt_runtime_update(blyt_runtime_t *rt);

/* Render one frame into the runtime's internal paletted framebuffer.
 * After this call returns, blyt_runtime_get_framebuffer() returns a
 * pointer to the just-rendered 320×240 8bpp image. Drives fc_cart_draw. */
void            blyt_runtime_draw(blyt_runtime_t *rt);

/* ── Video output ──────────────────────────────────────────────────────── */

/* Returns a pointer to the runtime's 320×240 paletted framebuffer.
 * The pointer is stable for the runtime's lifetime; contents are
 * valid only between blyt_runtime_draw and the next blyt_runtime_update. */
const uint8_t  *blyt_runtime_get_framebuffer(blyt_runtime_t *rt);

/* Returns a pointer to the 256-entry XRGB8888 palette. Stable for the
 * runtime's lifetime; contents may change every frame (palette
 * cycling, fades). The adapter reads this every frame after draw. */
const uint32_t *blyt_runtime_get_palette(blyt_runtime_t *rt);

/* ── Audio output ──────────────────────────────────────────────────────── */

/* Pull up to `frames` stereo samples (2 × int16) from the runtime's mixer
 * into `buf`. Returns the actual number of stereo frames written.
 * The adapter calls this once per retro_run with frames =
 * sample_rate / declared_fps. The spike's synthetic mixer always returns
 * exactly the requested count; production mixers may return fewer if
 * underrunning. */
int             blyt_runtime_pull_audio(blyt_runtime_t *rt,
                                        int16_t *buf, int frames);

/* The runtime's declared audio sample rate in Hz. Spike L: always 48000.
 * Reported through retro_get_system_av_info. */
uint32_t        blyt_runtime_audio_sample_rate(blyt_runtime_t *rt);

/* ── Input ─────────────────────────────────────────────────────────────── */

/* Set the current button-state bitmask for the given player. Mask layout
 * follows BLYT_BUTTON_* below (ADR-0017). The adapter calls this once
 * per catch-up tick before blyt_runtime_update; the runtime sees the
 * same bitmask for every tick that belongs to the same render frame. */
void            blyt_runtime_set_button_state(blyt_runtime_t *rt,
                                              int player, uint16_t mask);

#define BLYT_BUTTON_UP       (1u << 0)
#define BLYT_BUTTON_DOWN     (1u << 1)
#define BLYT_BUTTON_LEFT     (1u << 2)
#define BLYT_BUTTON_RIGHT    (1u << 3)
#define BLYT_BUTTON_A        (1u << 4)
#define BLYT_BUTTON_B        (1u << 5)
#define BLYT_BUTTON_X        (1u << 6)
#define BLYT_BUTTON_Y        (1u << 7)
#define BLYT_BUTTON_L        (1u << 8)
#define BLYT_BUTTON_R        (1u << 9)
#define BLYT_BUTTON_START    (1u << 10)
#define BLYT_BUTTON_SELECT   (1u << 11)

/* ── Timing metadata ───────────────────────────────────────────────────── */

/* Cart-declared frames per second (60 in spike-L; ADR-0047 may broaden
 * to 30 / 50). The adapter passes this to retro_get_system_av_info. */
uint32_t        blyt_runtime_declared_fps(blyt_runtime_t *rt);

/* ── Save state (direct passthroughs to spike-K) ──────────────────────── */

/* Compute the upper bound on save-state size, in bytes. Stable for the
 * lifetime of the loaded cart (per the libretro contract — see plan
 * §retro_serialize_size). The adapter caches this once at retro_load_game. */
uint32_t        blyt_runtime_save_upper_bound(blyt_runtime_t *rt);

/* Serialize the runtime's tracked regions into `buf`. Returns the number
 * of bytes written (≤ blyt_runtime_save_upper_bound), or 0 on failure
 * (cap too small, runtime not initialized). Calls into spike-K's
 * save_state_save against the runtime-side tracked-region registry. */
uint32_t        blyt_runtime_save(blyt_runtime_t *rt,
                                  uint8_t *buf, uint32_t cap);

/* Deserialize a previously-written buffer back into the runtime.
 * Returns true on success; false on header mismatch, layout-hash mismatch
 * (cross-build buffer), truncated buffer. The adapter pairs this with
 * blyt_runtime_reset to satisfy the spike-K "init then load" contract. */
bool            blyt_runtime_load(blyt_runtime_t *rt,
                                  const uint8_t *buf, uint32_t size);

/* Spike-L assertion: the cart's coroutine-slot table is immutable post-
 * init, so save_upper_bound is stable for the cart's lifetime. The
 * facade aborts if a cart attempts to grow its slot table after this
 * point. Documented in PLAN.md §risk note "retro_serialize size
 * mismatch under coroutine-blob expansion". */
void            blyt_runtime_assert_slots_locked(blyt_runtime_t *rt);

#ifdef __cplusplus
}
#endif

#endif /* BLYT_FACADE_H */
