/* FNV-1a-64 digest emission for Spike D frames.
 *
 * The digest is computed over the byte image of `frame_state_t`.  Every
 * float field is canonicalized (NaN → 0x7fc00000) before hashing so
 * that "two valid NaN encodings" cannot create a false-positive
 * cross-host divergence.  The output line is exactly:
 *
 *     DIGEST <frame> <hex64>\n
 *
 * with `frame` decimal, `hex64` lower-case hex 16 chars wide, no
 * timing or other content interleaved.  The diff harness greps for
 * `^DIGEST ` and any other line on stdout becomes meta-noise (cart
 * preamble / postamble), not the digest stream.
 */

#ifndef CART_RUNTIME_DIGEST_H
#define CART_RUNTIME_DIGEST_H

#include <stdint.h>
#include "frame_state.h"

void  frame_state_canonicalize(frame_state_t *s);
uint64_t frame_state_fnv1a(const frame_state_t *s);
void  frame_state_emit_digest(frame_state_t *s);  /* canonicalizes, hashes, prints */

#endif /* CART_RUNTIME_DIGEST_H */
