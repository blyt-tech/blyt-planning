# ADR-0058: Active slot management built into the buffer API

## Status
Accepted

## Context

State buffers (ADR-0010) hold fixed-count arrays of structs (e.g., 128
enemy slots). Carts need to allocate slots for new entities, free them when
entities are destroyed, and iterate over all currently active slots. Without
built-in slot management, every cart reimplements a free-list or generation-
count scheme, often incorrectly.

## Decision

**Active slot management is a first-class capability of the buffer API.**

```c
// Allocate the next free slot; returns BLYT_INVALID_SLOT if full
blyt_result_t blyt_buffer_alloc_slot(blyt_buffer_h buf, int32_t *out_slot);

// Mark a slot as free for reuse
blyt_result_t blyt_buffer_free_slot(blyt_buffer_h buf, int32_t slot);

// Iteration over active slots (for use in a loop)
blyt_result_t blyt_buffer_iter_begin(blyt_buffer_h buf, blyt_iter_h *out_iter);
bool        blyt_buffer_iter_next(blyt_iter_h iter, int32_t *out_slot);
```

Active-slot state is stored in the buffer's own tracked region (a compact
bitset alongside the POD data). It is saved and restored as part of the
normal save/restore cycle.

**Slot search strategy:** the runtime performs eager slot search at frame
boundaries (not on every `alloc_slot` call), maintaining a pointer into the
bitset to amortize search cost. `alloc_slot` in the common case is O(1).

**Dev overlay:** the memory introspection API (ADR-0029) includes per-buffer
active slot counts (used / total), visible in the dev overlay.

```lua
-- Lua ergonomics
local slot = enemies:alloc_slot()
for slot in enemies:slots() do
    -- process active enemies
end
enemies:free_slot(slot)
```

## Consequences

- Carts use the same slot management machinery for every entity buffer;
  no per-cart reimplementation of free lists.
- Slot state is automatically serialized as part of the buffer; save/restore
  preserves exactly which slots are active.
- Iteration is safe to interleave with `free_slot` (freeing a slot during
  iteration marks it inactive; the iterator skips inactive slots).
- The bitset overhead per buffer is minimal: 128 slots = 16 bytes.
- The dev overlay gives authors immediate visibility into buffer utilization
  without custom debug code.
