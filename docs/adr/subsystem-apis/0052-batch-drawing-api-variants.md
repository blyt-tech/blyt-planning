# ADR-0052: Batch drawing API variants

## Status
Accepted

## Context

Lua's per-call overhead to native code (ECALL boundary) is non-trivial for
vanilla PUC Lua. A cart drawing 200 sprites per frame at ~1 µs per ECALL
spends ~200 µs in call overhead alone. Batch variants eliminate this by
submitting an array of draw parameters in a single call.

## Decision

**All drawing primitives have N-element batch variants alongside their
single-element forms.**

Single-element (ergonomic):
```c
fc_image_blit(img, x, y, flags);
fc_rect_fill(x, y, w, h, color, pattern);
```

Batch (performance):
```c
// Batch blit: array of fc_blit_cmd_t structs, N elements
fc_image_blit_n(img, cmds, n, flags);

// Batch rect fill: array of fc_rect_cmd_t structs, N elements
fc_rect_fill_n(cmds, n);
```

Batch variants accept a pointer to an array of command structs and a count.
The array lives in cart-addressable memory (typically a typed state buffer
or a stack-allocated array). The runtime processes all N elements in a single
native call.

In Lua, batch variants accept a table or a typed buffer slice:

```lua
img:blit_n(sprites_buffer, n)  -- typed buffer slice
```

The single-element variants remain the primary API; batch variants are an
optimization applied when profiling identifies ECALL overhead as a bottleneck.

## Consequences

- Carts drawing hundreds of sprites per frame can reduce ECALL overhead
  from O(N) calls to O(1) calls.
- The batch API is additive; no existing single-element code changes.
- Typed state buffers (ADR-0010) are the natural source of batch draw data:
  an enemies buffer can be submitted directly as a batch blit call.
- The runtime's inner draw loop is unchanged; batch variants are thin wrappers
  that loop over the command array in native code rather than in Lua.
