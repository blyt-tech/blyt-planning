/* Spike K Stage 4 — det_cutscene_load.elf.
 *
 * Reads the saved buffer (argv[1]), restores frame_state and the
 * coroutine save-blob region, then continues the cutscene from the
 * restored step.  The cart's first post-restore frame reads
 * (step=N, angle=...) from slot 0, advances to step N+1, and emits
 * the digest — same as a same-host straight-through run for
 * frame N+1+.
 */

#include <stdint.h>
#include <stddef.h>

#include "../../cart_runtime/frame_state.h"
#include "../../cart_runtime/region_frame_state.h"
#include "../../cart_runtime/region_coroutine_save_blob.h"
#include "../../cart_runtime/digest.h"
#include "../../cart_runtime/save_state.h"
#include "../../cart_runtime/save_io.h"

extern int   printf(const char *, ...);

#ifndef NFRAMES
#  define NFRAMES 30
#endif

typedef struct __attribute__((packed)) {
    uint32_t step;
    float    angle;
} cutscene_state_t;

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

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("PANIC det_cutscene_load: usage: <buffer-hex-file>\n");
        return 1;
    }
    save_state_init();

    frame_state_t *fs = region_frame_state_get();
    {
        unsigned char *p = (unsigned char *)fs;
        for (size_t i = 0; i < sizeof(*fs); i++) p[i] = 0;
    }

    static char hexbuf[16384];
    long n = save_io_read_file(argv[1], hexbuf, sizeof(hexbuf) - 1);
    if (n < 0) { printf("PANIC det_cutscene_load: cannot read buffer\n"); return 1; }
    hexbuf[n] = '\0';

    static uint8_t bin[8192];
    long m = save_io_parse_hex(hexbuf, (size_t)n, bin, sizeof(bin));
    if (m < 0) { printf("PANIC det_cutscene_load: hex parse failed\n"); return 1; }
    if (!save_state_load(bin, (uint32_t)m)) {
        printf("PANIC det_cutscene_load: save_state_load rejected buffer\n");
        return 1;
    }

    {
        cutscene_state_t s;
        coroutine_blob_read(0, &s, sizeof(s));
        union { float f; uint32_t u; } a = { .f = s.angle };
        printf("CUTSCENE step=%u angle=%08x\n", (unsigned)s.step, (unsigned)a.u);
    }

    uint32_t resume_frame = fs->frame + 1;
    for (uint32_t f = resume_frame; f < NFRAMES; f++) {
        cutscene_step();
        fs->frame = f;
        frame_state_emit_digest(fs);
    }
    return 0;
}
