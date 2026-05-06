/* Spike N — native cart save-side driver.
 *
 * Runs the native cart (no Lua VM) for SAVE_FRAME frames, then saves
 * state and emits "BUFFER <frame> <hex>" to stdout.  Used by edits
 * n1–n6 PRE variants.
 *
 * The native cart is implemented in workloads/det_native_cart.c and
 * exposes three symbols:
 *   void native_cart_init(void)          — registers region, sets up state
 *   void native_cart_update(int frame)   — advances state by one frame
 *   void native_cart_emit_digest(void)   — calls frame_state_emit_digest
 */

#include <stdint.h>
#include <stddef.h>

#include "../../cart_runtime/save_state.h"
#include "../../cart_runtime/region_frame_state.h"

extern int  printf(const char *, ...);
extern int  atoi(const char *);
extern void native_cart_init(void);
extern void native_cart_update(int frame);
extern void native_cart_emit_digest(void);

#ifndef SAVE_FRAME_DEFAULT
#  define SAVE_FRAME_DEFAULT 15
#endif
#ifndef SAVE_BUF_CAP
#  define SAVE_BUF_CAP 32768
#endif

int main(int argc, char **argv)
{
    int save_frame = SAVE_FRAME_DEFAULT;
    if (argc >= 2) save_frame = atoi(argv[1]);

    save_state_init();
    native_cart_init();

    for (int f = 0; f <= save_frame; f++) {
        native_cart_update(f);
        native_cart_emit_digest();
        region_frame_state_get()->frame = (uint32_t)(f + 1);
    }

    static uint8_t buf[SAVE_BUF_CAP];
    uint32_t n = save_state_save(buf, sizeof(buf), (uint32_t)save_frame);
    if (!n) {
        printf("PANIC n_native_save: save_state_save returned 0\n");
        return 1;
    }
    save_state_emit_hex(buf, n);
    return 0;
}
