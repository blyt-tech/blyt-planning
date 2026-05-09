# ADR-0039: Lua performance strategy — rich native API primitives in v1, inline native deferred

## Status
Accepted

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

**Layer 2 (v2+, if real usage justifies): Inline native code in Lua carts.**

The unified ELF format (ADR-0024) makes this natural: the packer accepts
`.c`/`.rs`/`.zig` files alongside `.lua`, compiles them to RV32IMAFC, links
them into the cart ELF with the shim, and generates Lua bindings.

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
- v2 inline native requires packer changes (compile + link + bind) but no
  runtime changes; the ELF format already supports it.
- LuaJIT is not used. WASM portability requires a single Lua VM build
  (vanilla PUC Lua) that runs everywhere including browser. LuaJIT's WASM
  support is incomplete.
