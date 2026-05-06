/* Spike L — palette expansion to XRGB8888.
 *
 * ADR-0033 specifies XRGB8888 expansion in the libretro adapter, after
 * blyt_cart_draw completes, against the current 256-entry palette read
 * fresh every frame so palette animation (cycling, fades, programmatic
 * writes) round-trips correctly.
 *
 * The palette is stored in XRGB8888 form already by the runtime (see
 * blyt_facade.c init_palette). If a future runtime stores the palette
 * in some other shape (RGB332 packed, RGBA), this is the cheap
 * conversion site — PLAN.md §risk note "Palette format mismatch".
 */

#include <stdint.h>
#include <stddef.h>

void adapter_expand_palette(const uint8_t *src,
                            const uint32_t *palette,
                            uint32_t       *dst,
                            size_t          pixels)
{
    if (!src || !palette || !dst) return;
    /* The hot loop. 320×240 = 76800 lookups per frame at 60 fps =
     * 4.6M lookups/s — well within budget on any libretro target.
     * Production would prebuild a palette LUT and use SIMD; spike L
     * does not gate on a specific ms number per PLAN.md §performance
     * ceiling tuning. */
    for (size_t i = 0; i < pixels; i++) dst[i] = palette[src[i]];
}
