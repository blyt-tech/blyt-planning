/* Spike K — runtime-side registry of tracked regions.
 *
 * Each tracked region declares its name, byte size, a save callback that
 * writes the region's bytes into a save buffer (canonicalizing f32 fields
 * along the way), a load callback that reads the bytes back into the
 * region's storage, and a `describe` callback that emits a stable text
 * description of the region's field layout into a string sink.  The
 * `describe` outputs are concatenated and FNV-1a-64'd to produce the
 * `layout_hash` carried in every save buffer header.
 *
 * Stages add new region kinds:
 *   Stage 1 — frame_state (POD; reused from Spike D, the floor case).
 *   Stage 2 — cart_state_t per cart (POD; declared by per-cart layout).
 *   Stage 3 — runtime_voice_end_queue_t (ADR-0106).
 *   Stage 4 — coroutine_save_blob_t array (ADR-0012, simplified to POD).
 *   Stage 5 — runtime_screen_shake_t (ADR-0051).
 *
 * The body emit order is fixed and matches PLAN.md.  Adding a new region
 * kind is *not* compatible with old buffers — the layout_hash gate makes
 * this an explicit reject rather than a silent corruption.
 */

#ifndef CART_RUNTIME_RUNTIME_TRACKED_H
#define CART_RUNTIME_RUNTIME_TRACKED_H

#include <stdint.h>
#include <stddef.h>

/* String sink used by `describe` callbacks.  The buffer is sized at cart
 * startup; if the cart adds enough regions to exceed it, save_state_init()
 * traps via abort() — better than silently truncating the layout hash
 * input.  4 KiB is plenty for the spike's region inventory. */
typedef struct {
    char    *buf;
    size_t   cap;
    size_t   pos;
    int      overflowed;    /* set if any append exceeded cap */
} string_sink_t;

void  string_sink_putc (string_sink_t *s, char c);
void  string_sink_puts (string_sink_t *s, const char *str);
void  string_sink_putu (string_sink_t *s, unsigned long v);

/* The per-region callback set.  Each region is identified by a stable
 * name string used in the layout description and in the body block tag.
 * Sizes are bytes; the runtime allocates no per-region storage — each
 * region's bytes live in cart-runtime BSS and the callbacks copy in/out. */
typedef struct {
    const char *name;
    uint32_t    size;                                       /* bytes */
    void      (*describe)(string_sink_t *out);              /* layout hash input */
    void      (*save)    (uint8_t *dst);                    /* canonicalize + memcpy */
    int       (*load)    (const uint8_t *src, uint32_t n);  /* bounds-check + memcpy */
} runtime_tracked_region_t;

/* The global region registry — populated at cart link time.  Order is
 * meaningful: it determines body emit order in the save buffer. */
extern const runtime_tracked_region_t *runtime_tracked_regions[];
extern const uint32_t                  runtime_tracked_region_count;

/* Walk every region's describe() and concatenate the results into `out`.
 * The output is the input to FNV-1a-64 for layout_hash. */
void runtime_tracked_describe(string_sink_t *out);

/* Sum of `size` across every region.  Used to pre-size save buffers and
 * to bounds-check loads. */
uint32_t runtime_tracked_total_size(void);

#endif /* CART_RUNTIME_RUNTIME_TRACKED_H */
