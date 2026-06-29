# ADR-0122: Runtime-managed previous-frame surface

## Status
Proposed — deferred post-v1

> **Forward note (2026-06-29):** spec **#195** generalizes "surface" to
> runtime-managed mutable surfaces (a writable `blyt_image_h`) with an
> `acquire`/`release` access model; the previous-frame surface here is one
> read-only instance of that. See #195.

## Context

Frame-feedback effects are a cornerstone of demoscene-style rendering: a
plasma tunnel, motion blur, trail accumulation, or recursive distortion all
read the completed output of the previous frame and layer new content on top
of it. The technique needs read access to "what the framebuffer looked like
when last frame finished."

ADR-0087 deliberately clears the framebuffer to palette index 0 before every
`draw()` to give every re-render facility a deterministic starting point. It
acknowledges the feedback use case explicitly and identifies the workaround:
*"Carts that want frame-to-frame pixel persistence draw onto a persistent image
surface they own and blit it each frame. This is explicit, save-state-clean,
and integrates with the dev-mode draw inspector."* That remains correct for
carts that need the feedback effect to survive save-state restores faithfully —
the image surface lives in tracked state memory and is automatically
serialised.

For effects where that level of fidelity is not required (the vast majority of
demoscene-style feedback patterns), the workaround imposes unnecessary
manifest surface declarations, explicit post-draw copies, and state-buffer
sizing work. A runtime-managed surface removes the boilerplate without
undermining any system invariant.

## Decision

**The runtime maintains a previous-frame surface — a 320×240 palette-index
buffer holding the completed framebuffer from the last canonical `draw()`
call.** It is accessible as a read-only image handle via
`blyt_gfx_prev_frame()`, valid only within `blyt_cart_draw()`.

### Accessor

```c
// Returns a read-only image handle pointing to the previous-frame surface.
// Valid only within blyt_cart_draw(); calling outside draw() is a dev-mode
// error (warning, or hard error under --strict).
blyt_image_h blyt_gfx_prev_frame(void);
```

The returned handle is a standard `blyt_image_h`. All image operations
available on loaded image resources are valid on it (`blit`, `blit_region`,
`get_pixel`, etc.). It is read-only: attempting to use it as a render target
is a dev-mode error.

The typical feedback pattern:

```lua
function draw()
    local prev = gfx.prev_frame()
    prev:blit(0, 0, BLIT.OPAQUE)  -- lay down previous frame as base
    -- draw current content on top
end
```

```c
void blyt_cart_draw(void) {
    blyt_image_blit(blyt_gfx_prev_frame(), 0, 0, BLYT_BLIT_OPAQUE);
    // draw current content on top
}
```

### Contents

After every canonical `draw()` completes, the runtime copies the finished
framebuffer — including all blits, fills, and post-process passes
(screen shake: ADR-0051, palette cycling: ADR-0061) — into the
previous-frame surface. The copy is taken after post-processing so the cart
sees exactly what was presented.

### Zero-fill conditions

The surface is zeroed (all pixels set to palette index 0) whenever the cart
has no valid previous frame. This occurs:

- **On first frame.** No previous frame exists.
- **After any state restore.** After `on_load_state` fires for any reason
  (`BLYT_LOAD_SAVE_GAME`, `BLYT_LOAD_SAVE_STATE`, `BLYT_LOAD_REWIND`,
  `BLYT_LOAD_HOT_RELOAD`), the surface is zeroed before the next `draw()`.
  In all cases the cart has arrived at a point in time without a valid
  prior frame in this rendering context.

The first `draw()` after a state restore sees a zeroed surface; subsequent
frames accumulate normally.

Carts that need the feedback effect to survive restores faithfully — such
that frame N after a restore shows the same accumulated state as frame N in
the original run — should use the ADR-0087 workaround: declare an image
surface in tracked state memory, blit it at the start of `draw()`, and copy
the framebuffer back into it afterward.

### Phase restriction

`blyt_gfx_prev_frame()` is valid only in `draw()`. Calling it from `update()`
is a dev-mode error, matching the enforcement pattern of ADR-0076 (tracked
state writes during draw) and ADR-0041 (cosmetic RNG called from update).

This preserves the fundamental invariant of ADR-0076: `update()` is the
simulation tick; the framebuffer is a presentation artifact and must not
influence simulation state. A cart that reads the previous frame in `update()`
and routes it into tracked state buffers would create a dependency on host
rendering, violating the simulation/presentation boundary.

### Not tracked state; not in save states

The previous-frame surface is a rendering artifact, not simulation state. It
is not included in save states, rewind snapshots, retro_serialize payloads, or
netplay state. Its absence from the snapshot is consistent with its zero-fill
semantics: restoring a snapshot zeroes the surface, so restoring and then
continuing is well-defined.

The 75KB runtime buffer is always-present in all builds. Carts that never call
`blyt_gfx_prev_frame()` incur no API overhead.

### Netplay

Each peer runs `draw()` deterministically from identical tracked state
(ADR-0076, ADR-0007). Since `draw()` is a deterministic function over that
state, peers produce identical framebuffers independently — and therefore
arrive at identical previous-frame surfaces at the start of each frame. No
wire traffic is required. This is the same argument as ADR-0041's cosmetic
RNG: locally derivable values cost nothing on the wire.

Netplay sessions start fresh (ADR-0023), so no state restore occurs
mid-session; the zero-fill rule is not encountered in normal netplay play.

### Non-canonical draws

Non-canonical draws — save-state thumbnail capture, replay-driven thumbnails,
the draw-step inspector's backward step — must not corrupt or consume the live
previous-frame surface. The runtime saves and restores the surface around
every non-canonical draw.

For **thumbnail captures** (save-state or replay), the cart-visible
previous-frame surface during the thumbnail draw is zeroed. Thumbnails are
single-frame captures for display in save UIs; accumulated feedback history is
not meaningful in that context.

For the **draw-step inspector's backward step** (ADR-0107), the runtime
records the previous-frame surface state at the start of each `draw()` in dev
mode, as part of that frame's draw call log entry. This adds 75KB to the
per-frame dev-mode log. When backward-stepping, the runtime restores this
snapshot before replaying the primitive log, so the inspector produces
bit-identical output to the original live frame. This follows the ADR-0065
pattern: zero release-build cost.

## Consequences

- Feedback effects (trails, blur, plasma, recursive distortion) are
  implementable with two lines: `gfx.prev_frame():blit(0, 0, BLIT.OPAQUE)`
  followed by normal draw code. No manifest declarations required.
- The previous-frame surface is zeroed after any state restore. The first
  frame after a rewind or save-state load will not carry prior accumulated
  state. For demoscene-style carts, which are typically non-interactive and
  not subject to player-triggered restores, this is not a practical
  limitation.
- Carts that need restore-faithful feedback continue to use the ADR-0087
  workaround. The two approaches coexist without conflict.
- The runtime always allocates the 75KB buffer, even for carts that do not
  use the feature. On the reference hardware class (ADR-0002) this is within
  normal operating budget.
- Dev-mode draw inspector backward-stepping costs an extra 75KB per logged
  frame. This is dev-mode-only (ADR-0065).
- Thumbnail renders (save-state, replay) see a zeroed previous-frame surface
  and do not capture accumulated feedback history. This is expected and
  correct for single-frame thumbnail semantics.
- The simulation/presentation boundary (ADR-0076) is not weakened. The
  previous-frame surface is inaccessible from `update()` by design, so
  framebuffer content can never influence simulation state.
- Netplay is unaffected. No new wire traffic; no new handshake state.
