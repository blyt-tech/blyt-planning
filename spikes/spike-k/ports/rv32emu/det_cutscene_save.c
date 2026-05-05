/* Spike K Stage 4 — det_cutscene_save.elf.
 *
 * Simulates the production `blyt32.coroutine.create{start, save,
 * restore}` pattern (ADR-0012) without a Lua VM: the cutscene's
 * persistent state (step counter and angle) lives in slot 0 of the
 * coroutine save-blob region.  Each frame, the cart reads the slot,
 * advances the cutscene by one step, writes the slot back, and folds
 * (step, angle) into `frame_state.accum_misc`.
 *
 * The byte-image round-trip property tested here is the same one a
 * real Lua coroutine save-hook would rely on — the runtime serializes
 * the slot's bytes verbatim, the cart writes its POD struct via the
 * slot interface, and the Lua-level distinction (table-flatten vs
 * fixed-shape struct) is engineering on top.  See PLAN.md § "Coroutine
 * save-hook output is treated as opaque bytes".
 *
 * The negative test (transient `coroutine.create()` across save/
 * restore must throw) requires Lua infrastructure that Stage 4
 * currently does not build.  Documented in spike-k-results.md as a
 * follow-up gated on Stage 2's Lua harness.
 */

#include <stdint.h>
#include <stddef.h>

#include "../../cart_runtime/frame_state.h"
#include "../../cart_runtime/region_frame_state.h"
#include "../../cart_runtime/region_coroutine_save_blob.h"
#include "../../cart_runtime/save_state.h"

extern int   printf(const char *, ...);
extern int   atoi(const char *);

#ifndef NFRAMES
#  define NFRAMES 30
#endif
#ifndef SAVE_FRAME_DEFAULT
#  define SAVE_FRAME_DEFAULT 7
#endif

/* The cutscene's persistent state.  Fixed POD shape; would be derived
 * from the Lua `save` callback's return-value shape in production. */
typedef struct __attribute__((packed)) {
    uint32_t step;
    float    angle;
} cutscene_state_t;

static void cutscene_init(void)
{
    coroutine_blob_activate(0);
    cutscene_state_t s = { .step = 0, .angle = 0.0f };
    coroutine_blob_write(0, &s, sizeof(s));
}

static void cutscene_step(void)
{
    cutscene_state_t s;
    coroutine_blob_read(0, &s, sizeof(s));
    s.step += 1;
    /* Toy "angle" update — same as the example in PLAN.md Stage 4. */
    s.angle = (float)((s.step * 17u) % 360u);
    coroutine_blob_write(0, &s, sizeof(s));

    frame_state_t *fs = region_frame_state_get();
    fs->accum_misc += (float)s.step + s.angle * 0.001f;
}

int main(int argc, char **argv)
{
    uint32_t save_frame = SAVE_FRAME_DEFAULT;
    if (argc >= 2) save_frame = (uint32_t)atoi(argv[1]);

    frame_state_t *fs = region_frame_state_get();
    {
        unsigned char *p = (unsigned char *)fs;
        for (size_t i = 0; i < sizeof(*fs); i++) p[i] = 0;
    }
    save_state_init();

    cutscene_init();

    for (uint32_t f = 0; f <= save_frame; f++) {
        cutscene_step();
        fs->frame = f;
    }

    static uint8_t buf[8192];
    uint32_t n = save_state_save(buf, sizeof(buf), save_frame);
    if (!n) {
        printf("PANIC det_cutscene_save: save_state_save returned 0\n");
        return 1;
    }
    save_state_emit_hex(buf, n);
    return 0;
}
