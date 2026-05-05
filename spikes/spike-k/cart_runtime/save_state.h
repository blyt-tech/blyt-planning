/* Spike K — save-state wire format and runtime API.
 *
 * The save-state buffer is: a packed `save_state_header_t` followed by a
 * concatenation of packed POD region blocks in fixed order (see PLAN.md
 * § "Save state is the byte image of all tracked regions, in a fixed order").
 * Every f32 field tracked by a region is canonicalized to a single quiet-NaN
 * pattern at the *write* boundary — this is stricter than Spike D, which
 * canonicalized only at digest emission, and catches the class of bugs where
 * a non-canonical NaN reaches a state buffer field but never the digest.
 *
 * Integer fields are stored little-endian; both reference hosts (linux/amd64,
 * linux/arm64) and the guest (RV32 LE) are little-endian, so no swap is
 * performed.  A big-endian host would need a swap layer; out of scope per
 * ADR-0007's reference platforms.
 *
 * The header carries a 64-bit `layout_hash` over a stable text description
 * of every tracked region (name | type | size | per-field name:type:offset,
 * NUL-separated, in declaration order).  On restore the runtime recomputes
 * the hash from the *current* binary's tracked-region description and
 * rejects the load if it differs — this is the safety gate that catches
 * silent layout drift between the cart that wrote the buffer and the cart
 * that reads it (e.g. a field widened from i32 to i64).
 */

#ifndef CART_RUNTIME_SAVE_STATE_H
#define CART_RUNTIME_SAVE_STATE_H

#include <stdint.h>
#include <stddef.h>

#define SAVE_STATE_MAGIC   0x46433253u   /* 'FC2S' little-endian */
#define SAVE_STATE_VERSION 1u

typedef struct __attribute__((packed)) {
    uint32_t magic;          /* SAVE_STATE_MAGIC */
    uint32_t version;        /* SAVE_STATE_VERSION */
    uint64_t layout_hash;    /* FNV-1a-64 over runtime_tracked_describe() */
    uint32_t frame;          /* frame at which the save was taken */
    uint32_t total_size;     /* total bytes including this header */
} save_state_header_t;

/* Compute and cache the layout hash for the currently linked tracked-region
 * registry.  Call once at cart startup (before save_state_save / _load). */
void     save_state_init(void);

/* Serialize the runtime's tracked regions into `buf`.  Returns the number
 * of bytes written, or 0 if `cap` is too small for the header + body.
 * Canonicalizes NaN on every f32 field at write time. */
uint32_t save_state_save(uint8_t *buf, uint32_t cap, uint32_t frame);

/* Deserialize a saved buffer into the runtime's tracked regions.  Returns
 * true on success.  Failure modes (each returns false, no partial mutation):
 *   - magic mismatch
 *   - version mismatch
 *   - layout_hash mismatch (cart and saved buffer describe a different
 *     tracked-region shape — e.g. a field was widened, reordered, renamed)
 *   - total_size > size (truncated buffer)
 *   - body parse failure (region count or size out of bounds) */
int      save_state_load(const uint8_t *buf, uint32_t size);

/* Print "BUFFER <frame> <hex...>" to stdout — one line, lower-case hex,
 * no whitespace inside the hex string.  This is the cross-host transport. */
void     save_state_emit_hex(const uint8_t *buf, uint32_t size);

/* Helpers used by the per-region serializers; exposed for the synthetic
 * test harness in the corruption-detection negative tests. */
uint64_t save_state_layout_hash(void);
size_t   save_state_buffer_capacity(void);

#endif /* CART_RUNTIME_SAVE_STATE_H */
