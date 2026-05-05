/* Spike K — runtime_tracked.c.
 *
 * Implements the string sink helpers used by every region's `describe`
 * callback, plus the registry walker that concatenates each region's
 * description in declaration order.  The registry array itself
 * (`runtime_tracked_regions`) is defined per-cart in a sibling
 * translation unit; this file does NOT define it.
 */

#include <stdint.h>
#include <stddef.h>

#include "runtime_tracked.h"

/* ── string sink ─────────────────────────────────────────────────────────── */
/* Appends are bounds-checked; overflow sets a flag the caller inspects.
 * No malloc — the caller hands the sink a fixed buffer at startup. */

void string_sink_putc(string_sink_t *s, char c)
{
    if (s->pos + 1 > s->cap) { s->overflowed = 1; return; }
    s->buf[s->pos++] = c;
}

void string_sink_puts(string_sink_t *s, const char *str)
{
    while (*str) string_sink_putc(s, *str++);
}

/* Decimal unsigned integer.  Used by every region's offset/size emission;
 * pinning this routine here means the layout-hash input is identical
 * across hosts regardless of any host-side printf differences. */
void string_sink_putu(string_sink_t *s, unsigned long v)
{
    if (v == 0) { string_sink_putc(s, '0'); return; }
    char tmp[24];
    int  n = 0;
    while (v) { tmp[n++] = '0' + (char)(v % 10); v /= 10; }
    while (n--) string_sink_putc(s, tmp[n]);
}

/* ── registry walker ─────────────────────────────────────────────────────── */

void runtime_tracked_describe(string_sink_t *out)
{
    for (uint32_t i = 0; i < runtime_tracked_region_count; i++) {
        const runtime_tracked_region_t *r = runtime_tracked_regions[i];
        /* Each region's contribution opens with its name and size, then
         * the region's describe() callback writes its field-by-field
         * description.  A NUL separates regions to keep the hash input
         * unambiguous (no name-prefix collisions between regions). */
        string_sink_puts(out, "REGION:");
        string_sink_puts(out, r->name);
        string_sink_puts(out, ":size=");
        string_sink_putu(out, r->size);
        string_sink_putc(out, ':');
        if (r->describe) r->describe(out);
        string_sink_putc(out, '\0');
    }
}

uint32_t runtime_tracked_total_size(void)
{
    uint32_t total = 0;
    for (uint32_t i = 0; i < runtime_tracked_region_count; i++) {
        total += runtime_tracked_regions[i]->size;
    }
    return total;
}
