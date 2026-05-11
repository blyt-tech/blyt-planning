# ADR-0113: Runtime handle validation

## Status

Accepted

## Context

ADR-0046 specifies that all runtime objects are exposed as opaque
`uint32_t` handles, with zero as the invalid/null sentinel. ADR-0057
specifies the packed `(buffer_id, field_index)` encoding for state-buffer
field handles. Neither ADR specifies the security invariants for handle
validation at the API boundary under adversarial input.

Because cart code may be hostile, every handle value crossing an ECALL
call site must be treated as untrusted input. A handle stored in a state
buffer field and later passed to an API call has been under guest control
since it was written; it cannot be assumed to still be valid.

## Decision

### Static resource handles

Static resource handles are packer-generated integer IDs addressing
entries in the `.cart.resources` bundle. The count of valid IDs is
determined by the host's parse of the resource bundle, not by any
guest-supplied claim.

Validation at every ECALL call site: `id >= 1 && id <= resource_count`.
Handle 0 is always rejected.

Each resource type occupies a disjoint sub-range of the ID space. The
range check is therefore simultaneously a type check: an image handle
outside the image-ID sub-range is rejected by the range check before any
type-specific operation is attempted.

### Dynamic resource handles

Dynamic resource handles (`blyt_image_h`, `blyt_voice_h`, `blyt_tilemap_h`,
etc.) identify runtime-allocated objects such as loaded images and active
voices.

**Format:** a packed `uint32_t` encoding `(generation:16, index:16)` —
a 16-bit generation counter in the high half and a 16-bit slot index in
the low half. Handle 0 (generation = 0, index = 0) is always rejected.

**Validation at every ECALL call site:**
1. Extract index from bits 15:0. Range-check: `index >= 1 && index <= slot_count`.
2. Extract generation from bits 31:16.
3. Compare generation against the slot's current generation counter. If
   they differ, the handle is stale: return `BLYT_ERR_STALE_HANDLE`.
4. Confirm the slot is currently occupied (not freed). A freed slot
   increments its generation counter; the generation mismatch in step 3
   covers the common case. An additional `occupied` flag provides an
   explicit check.

**Generation counter wraparound:** the 16-bit generation counter wraps
at 65 535 reuses per slot. For the expected cart lifetime and resource
allocation patterns this is acceptable. The documented assumption is that
no single slot will be freed and reallocated more than 65 535 times in
one cart session.

**Validate at use, not at storage.** A handle read from a state buffer
field is a guest-controlled integer that may have been overwritten by a
`sw` instruction since any previous check. Revalidate at every ECALL
call site. The state-buffer schema's declaration that a field holds a
handle to a particular type (ADR-0057) is a layout hint for the guest
compiler; it is not a security invariant.

### State-buffer field handles

`blyt_field_h` values (ADR-0057) encode `(buffer_id:16, field_index:16)`.

In adversarial-test and dev builds: the runtime asserts at every
state-buffer access that the field handle's buffer_id matches the
declared buffer. This check may be elided in release builds as a
performance choice, provided the equivalent invariant is enforced by
the packer-generated constant set and not reachable from arbitrary cart
code.

### Type confusion prevention

Each `blyt_*_h` typedef identifies exactly one backing store type. The
ECALL dispatch entry for every API function validates that the handle's
value falls within the valid index range for that function's expected
handle type before any further processing. Passing a `blyt_image_h`
value to an audio function is rejected at the range check because image
and audio ID sub-ranges are disjoint.

## Consequences

- Handle validation is a uniform O(1) check at every ECALL entry; it
  does not require per-type specialisation beyond the sub-range bounds.
- Stale-handle detection via generation counters closes the liveness
  category: a freed resource handle cannot be silently reinterpreted as
  a newly allocated resource in the same slot.
- The packed format is a single `uint32_t` value, matching the existing
  `blyt_*_h` typedef. No API surface change is required.
- The 16-bit generation limit (65 535 reuses per slot) is explicitly
  documented as an assumption; if future workloads push against it,
  the field widths can be revisited in a v2 format change.
