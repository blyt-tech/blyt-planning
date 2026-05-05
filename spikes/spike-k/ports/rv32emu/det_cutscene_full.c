/* Spike K Stage 4 — det_cutscene_full.elf — straight-through baseline. */

#include <stdint.h>
#include <stddef.h>

#include "../../cart_runtime/frame_state.h"
#include "../../cart_runtime/region_frame_state.h"
#include "../../cart_runtime/region_coroutine_save_blob.h"
#include "../../cart_runtime/digest.h"
#include "../../cart_runtime/save_state.h"

extern int   printf(const char *, ...);

#ifndef NFRAMES
#  define NFRAMES 30
#endif

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
    s.angle = (float)((s.step * 17u) % 360u);
    coroutine_blob_write(0, &s, sizeof(s));
    frame_state_t *fs = region_frame_state_get();
    fs->accum_misc += (float)s.step + s.angle * 0.001f;
}

int main(void)
{
    frame_state_t *fs = region_frame_state_get();
    {
        unsigned char *p = (unsigned char *)fs;
        for (size_t i = 0; i < sizeof(*fs); i++) p[i] = 0;
    }
    save_state_init();
    cutscene_init();

    for (uint32_t f = 0; f < NFRAMES; f++) {
        cutscene_step();
        fs->frame = f;
        frame_state_emit_digest(fs);
    }
    return 0;
}
