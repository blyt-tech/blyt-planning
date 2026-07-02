# ADR-0134: Resource-constant encoding + constant-direct resource references

## Status

Accepted — settled in the #195 grill (2026-06-30) and a follow-on #196 grill
(2026-06-30); tracked by **#196**, implemented test-first on branch
`196-resource-constant-direct`. Amends ADR-0027 (explicit resource release) and
relates to ADR-0040/0088/0096. The surface model (#195) is designed on top of
this and lands after it.

## Context

Under the current resource model (ADR-0027/0040/0088, refined by #137 eviction,
#158 budget, #159 mem.stats, #160 persistent, #166 typed constants) a cart turns
a packer-assigned resource **constant** (`R_FOO`) into a **handle** via
`blyt_resource_load`, uses the handle, and `blyt_resource_release`s it. The
handle is runtime state: it can be invalidated by eviction (#137), by dev-mode
hot-reload moving the bytes, or across save/restore. So a cart must reason about
**invalidation** — defensive "is my handle still valid?" checks at frame start or
before each use. That is cumbersome and a determinism foot-gun: the handle is
runtime state that is *not* derivable from the serializable state buffers, so it
violates the "everything not in serializable state must be rebuildable"
discipline (cf. `--reset-every-frame`).

But the resource **constant already is a stable identity**: compile-time,
identical every run, trivially serializable, and **never invalid**. The runtime
can resolve constant → bytes on demand and evict under budget pressure (LRU,
#137) without the cart observing it. The `load` step's only real value was (a)
pre-warming residency and (b) a typed handle — and (a) is already provided by
#160 (manifest persistent resources, residency guaranteed at heap-zero) and (b)
by #166 (the constant is already typed). So a cart-held resource handle buys
nothing and costs the invalidation problem.

A second pressure: there will be **runtime-shipped (built-in) assets** as well as
cart-bundled ones, across *every* resource type — the constant encoding must
distinguish them.

## Decision

1. **Constant-direct references.** Resource-consuming APIs take the resource
   constant directly (e.g. `blit(R_FOO, x, y)`, `text_get(R_FOO)`). The runtime
   loads-or-cache-hits internally and evicts transparently. No cart-observable
   handle, no invalidation, no defensive checks.

2. **Remove `blyt_resource_load` / `blyt_resource_release`** and the
   `blyt_resource_h` type. `pin`/`unpin` (the within-frame raw-byte window) is
   **retained unchanged** as the explicit zero-copy raw-bytes escape hatch
   (confirmed in the #196 grill): it is a distinct mechanism from the residency
   handle, already takes the constant directly, and the copying `text_get` /
   `bytes_get` accessors are built on it. With `load` gone, the per-language
   accessors that used to hang off the loaded handle collapse one layer onto the
   constant itself (Rust `R_X.text_string()`/`.bytes_vec()`/`.pin()`; Lua
   `R.X:text()`/`:bytes()`; C `text_get`/`bytes_get` are already constant-direct).

3. **Resource-constant `u32` encoding** — compiled into `cart_resources.h` and
   shipped inside the `.blyt`, therefore a **forward-compat contract** a future
   runtime must still read:

   ```
   bits 31–29 : kind      (3b, in use)        ┐ upper 7 bits = kind-tag region;
   bits 28–25 : reserved tag headroom (4b)    ┘ tag may widen 3→7 bits without
                                                moving anything below
   bit  24    : provenance (0 = cart-bundled, 1 = runtime-shipped / built-in)
   bits 23–0  : id (16 M)
   ```

   - First-level classifier `kind = h >> 29`, **shared console-wide**. The kind
     values are fixed now (the value is baked, so it must never move):
     **`NONE = 0`** (the universal null sentinel; `0` is exclusively NONE),
     **`RESOURCE = 1`**, with **`SURFACE = 2`** and **`LOCKVIEW = 3`** reserved
     for the surface model (#195, not handled until then). RESOURCE is `1` rather
     than `0` precisely so kind `0` is unambiguously NONE — no overload between
     "null" and "a resource whose id happens to be 0". So a cart resource id `n`
     encodes to `0x2000_0000 | n` (e.g. `R_GREETING = 0x2000_0001`).
   - `blyt_entity_ref_t` keeps its own `gen«16» | index«16»` space and is
     **deliberately outside** this kind scheme — a high-generation entity ref
     aliases the kind bits, which is harmless because entity refs never flow into
     the resource / gfx APIs and vice versa; the type discipline for them is by
     API boundary, not by tag.
   - Resource **type** (image/text/audio/font/…) is resolved by registry lookup
     on `id` — the runtime stays byte-blind (ADR-0068 / #166); type is not
     encoded in the value.
   - Provenance lets runtime-shipped assets of *any* type coexist with cart
     assets in one constant space; it sits below the tag-growth region so growing
     the top-level kind tag never moves `id` or provenance. **No runtime-shipped
     assets exist yet**, so #196 only pins the bit: every cart resource is
     `provenance = 0`, decode validates `kind == RESOURCE` and masks the 24-bit
     id, and `provenance = 1` resolves to `NOT_FOUND` for now. The population
     mechanism for built-in assets (manifest vs runtime, registry plumbing) is
     **deferred to future work** (ties into #160 and ADR-0088).

     > **Amendment (2026-07-02, #201 implemented):** the first `provenance = 1`
     > constants shipped — the four built-in palettes
     > (`BLYT_PALETTE_AURORA/VGA/EGA/CGA`, ids 1–4, `BLYT_RESOURCE_ENCODE(id,
     > PROV_RUNTIME)`). This is **not** the general built-in-asset registry this
     > ADR deferred above — that population mechanism (manifest vs runtime,
     > registry plumbing, generalized to every future resource type: fonts,
     > sounds, …) **remains deferred**. #201 instead added a narrow,
     > hand-authored `blyt_builtin_palette(handle)` resolver in
     > `runtime/shared/blyt_palettes.c` — a small `switch` over 4 fixed ids
     > returning a `const uint32_t[256]` table, proportionate to four assets
     > that will never grow at runtime. The general registry is still the right
     > design for when fonts/sounds land and the built-in set is no longer a
     > fixed handful.
   - The canonical definition lives in a new **`runtime/shared/blyt_handle.h`**
     (kind enum + encode/decode, with a `_Static_assert` round-trip); the Rust
     packer mirrors the constants with a cross-reference comment and a round-trip
     unit test (the `BLYT_MEM_BUDGET_BYTES` ↔ `MEM_BUDGET_BYTES` precedent).

   Dynamic handles (surface, lock view; #195) carry no forward-compat constraint
   — never baked into a cart or save — so their layout is the running runtime's
   private choice.

## Consequences

- Carts never hold invalidatable resource state; the determinism discipline holds
  trivially (constants are reproducible; the cache is advisory, #137).
- The runtime owns residency end-to-end: declared-persistent (#160) pinned at
  heap-zero, the rest demand-loaded + LRU-evicted under the 16 MB budget (#158),
  introspectable via mem.stats (#159).
- ADR-0027's `load`/`release` lifecycle is superseded; the `pin`/`unpin` window
  is retained unchanged. ADR-0040/0088 (id assignment, asset pipeline) emit the
  new encoding; ADR-0096's tag scheme is the shared classifier.
- The shared lifecycle state machine (`runtime/shared/blyt_resource_lifecycle.h`)
  loses all load machinery (`load_count`, `load_gen`, handle packing,
  `blyt_rl_load` / `blyt_rl_release`); it keeps the `pin_count` window, and
  `blyt_rl_is_evictable` collapses to `pin_count == 0` (persistence AND-checked
  by the caller, ADR-0028). Cross-frame residency is now **purely advisory** —
  demand-loaded cache + LRU eviction (#137) + declared-persistent pinning (#160);
  nothing the cart holds. `mem.stats`'s loaded-resource list becomes the
  currently-resident/cached set (the same advisory determinism tier as before).
- The retired `RESOURCE_LOAD` / `RESOURCE_RELEASE` ECALL numbers are left
  reserved in place (as `RESOURCE_TEXT_GET` was), not renumbered.
- Existing resource tests migrate; text/bytes accessors (#166) and persistent
  residency (#160) keep their observable behaviour.

## Related

- **#195** (surfaces + acquire/release) — designed on top of this; shares the
  handle classifier.
- **#196** — implementation issue.
- ADR-0027, ADR-0040, ADR-0088, ADR-0096, ADR-0068; #137, #158, #159, #160, #166.
