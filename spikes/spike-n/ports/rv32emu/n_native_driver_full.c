/* Spike N — native cart straight-through driver.
 *
 * Reference run: no save, no load.  Runs the native cart from frame 0
 * to NFRAMES-1; the harness extracts same-host suffixes for comparison
 * against the load continuations.
 */

#include <stdint.h>
#include <stddef.h>

#include "../../cart_runtime/save_state.h"
#include "../../cart_runtime/region_frame_state.h"

extern int  printf(const char *, ...);
extern void native_cart_init(void);
extern void native_cart_update(int frame);
extern void native_cart_emit_digest(void);

#ifndef NFRAMES
#  define NFRAMES 30
#endif

int main(void)
{
    save_state_init();
    native_cart_init();

    for (int f = 0; f < NFRAMES; f++) {
        native_cart_update(f);
        native_cart_emit_digest();
        region_frame_state_get()->frame = (uint32_t)(f + 1);
    }

    return 0;
}
