# ADR-0039: Lua performance strategy — rich native API primitives in v1, inline native deferred

## Status

Accepted — amended by ADR-0111 (Lua+Rust hybrid binding layer: promoted
to v1 for Rust; binding generation is via SDK proc macro, not the packer;
WASM target uses a host-trampoline path rather than same-address-space
Lua C API calls).

## Context

Lua authors will inevitably want to drop to native code for performance-
critical paths (particle updates, pathfinding, collision broad-phase, noise
generation). The question is when and how to support this hybrid.

LuaJIT's FFI makes Lua-to-C calls near-free because the JIT compiles them.
Vanilla Lua (the choice here, for WASM portability) has real per-call
overhead. This matters for per-element callbacks from Lua — 10,000 calls doing
1 element each has prohibitive overhead; 1 call doing 10,000 elements is fine.

## Decision

**Two-layer approach: rich native API primitives in v1; inline native code
in Lua carts deferred to v2.**

**Layer 1 (v1): Rich native-implemented primitives in the console API.**

The runtime exposes a deliberately generous set of primitives implemented in
native C:
- Tilemap rendering (scrolling, layers, parallax).
- Sprite batch submission.
- Spatial queries over typed buffers (rect/circle queries, raycasts,
  nearest-neighbor).
- Collision primitives (AABB, circle, point-in-polygon, swept).
- Tween/ease library.
- Procedural noise (Perlin, simplex, value noise, FBM).
- Bulk vector math over typed buffers (add, scale, map-with-predefined-op).

Deferred to v1.x (post-launch, if real usage justifies):
- Particle systems (create/update/render thousands via data descriptors).
- Pathfinding (A* over grids).

These cover ~80–90% of performance-sensitive hot paths in typical games.

**Layer 2: Inline native code in Lua carts.**

For **Rust** this is a v1 capability, not v2-deferred, since Rust is a
first-class cart language (ADR-0108). The binding mechanism is defined in
ADR-0111: `#[lua_export]` annotations on individual Rust functions emit a
`.lua_exports` ELF section; the SDK proc macro generates the Lua C API
wrappers on rv32 targets and the runtime generates host-side trampolines
from `.lua_exports` on the WASM target. The packer is not involved in
binding generation.

For **C and other languages** the v2 characterisation below still applies.
The unified ELF format (ADR-0024) makes it natural: the packer accepts
`.c`/`.zig` files alongside `.lua`, compiles them to RV32IMAFC, links them
into the cart ELF, and generates Lua bindings (via the `cart_lua_modules`
hook described in ADR-0025).

**API shape rules that enable both layers efficiently (enforced in v1 design):**
- Bulk operations on typed buffers, not per-element callbacks.
- Closed set of operations for bulk ops (no arbitrary Lua functions as
  per-element callbacks — enables SIMD vectorization).
- State buffers accessible by raw pointer from native code (layout
  descriptors produce predictable memory layouts).

## Consequences

- Most Lua carts never need to leave Lua when the primitive library is
  aggressive enough.
- The v1 API design (bulk ops, no per-element callbacks) sets up v2 inline
  native cleanly without retrofit.
- v2 inline native for C/Zig requires packer changes (compile + link) but
  no runtime changes; the ELF format already supports it. For Rust, no
  packer changes are required — the proc macro and runtime handle it (ADR-0111).
- LuaJIT is not used. WASM portability requires a single Lua VM build
  (vanilla PUC Lua) that runs everywhere including browser. LuaJIT's WASM
  support is incomplete.

## Amendment (ADR-0136, 2026-07-07)

**The "drop to native for performance" premise is superseded by host-Lua.**
This ADR assumed Lua authors will inevitably drop to native code *for speed*.
With host-Lua everywhere (ADR-0136), the Lua tier runs native and is fast: on
the floor device, cart-native-C over host-Lua is ~1.0× on game logic (a tie —
native is ~10% slower interpreted) and ~1.4× on a tight numeric loop. The ~50×
that once looked like "Lua is slow" was **emulation overhead**, which host-Lua
reclaims — it was never a Lua-vs-C gap.

So the native tier is **not primarily a performance escape hatch**. Its role is
(a) **bring your preferred native toolbox** — reach an existing C/C++/Rust
library rather than reimplement it in Lua; and (b) **language preference** —
author in Rust/C because that is the workflow you want. The **performance**
surface is the console's rich native API primitives (Layer 1 above — gfx/audio,
host-C, fast and meterable-as-calls on every target); a cart never needs to go
native *for speed*. Genuinely compute-bound cart-native work that the primitives
do not cover (a custom software renderer, DSP, procedural generation) is the
exception, and is a signal to add a primitive rather than to JIT the cart tier.
