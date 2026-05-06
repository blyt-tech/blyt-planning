/* Spike N — native cart migration-aware load-side driver.
 *
 * Reads a save buffer (with optional "BUFFER <frame> " prefix) from
 * argv[1], restores state via save_state_load_migrate() (which invokes
 * the migration walk if the layout_hash differs), emits the post-
 * migration BUFFER line, then runs the cart from frame S+1 to NFRAMES-1
 * emitting DIGEST lines.
 *
 * The post-migration BUFFER line lets the harness check that cross-host
 * migration produces byte-identical buffers (the headline cross-host
 * gate for schema-changing edits).
 */

#include <stdint.h>
#include <stddef.h>

#include "../../cart_runtime/save_state.h"
#include "../../cart_runtime/save_state_n.h"
#include "../../cart_runtime/save_io.h"
#include "../../cart_runtime/region_frame_state.h"

extern int  printf(const char *, ...);
extern void native_cart_init(void);
extern void native_cart_update(int frame);
extern void native_cart_emit_digest(void);

#ifndef NFRAMES
#  define NFRAMES 30
#endif
#ifndef SAVE_BUF_CAP
#  define SAVE_BUF_CAP 32768
#endif
#ifndef HEX_BUF_CAP
#  define HEX_BUF_CAP 131072
#endif

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("PANIC n_native_load: usage: <buffer-hex-file>\n");
        return 1;
    }

    save_state_init();
    native_cart_init();

    static char hexbuf[HEX_BUF_CAP];
    long n = save_io_read_file(argv[1], hexbuf, sizeof(hexbuf) - 1);
    if (n < 0) { printf("PANIC n_native_load: cannot read buffer\n"); return 1; }
    hexbuf[n] = '\0';

    static uint8_t bin[SAVE_BUF_CAP];
    long m = save_io_parse_hex(hexbuf, (size_t)n, bin, sizeof(bin));
    if (m < 0) { printf("PANIC n_native_load: hex parse failed\n"); return 1; }

    if (!save_state_load_migrate(bin, (uint32_t)m)) {
        printf("PANIC n_native_load: save_state_load_migrate rejected buffer\n");
        return 1;
    }

    /* Emit the post-migration buffer so the harness can check cross-host
     * buffer-bytes equality (the stronger migration gate). */
    {
        static uint8_t post_buf[SAVE_BUF_CAP];
        uint32_t post_n = save_state_save(post_buf, sizeof(post_buf),
                                           save_state_get_frame(bin));
        if (post_n) save_state_emit_hex(post_buf, post_n);
    }

    /* Continuation: run from save_frame+1 to NFRAMES-1. */
    int start_frame = (int)region_frame_state_get()->frame;
    for (int f = start_frame; f < NFRAMES; f++) {
        native_cart_update(f);
        native_cart_emit_digest();
        region_frame_state_get()->frame = (uint32_t)(f + 1);
    }

    return 0;
}
