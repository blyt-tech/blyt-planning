# ADR-0028: Persistent resources declared in cart manifest

## Status
Accepted

## Context

Most carts have a set of resources they always need — primary font, main
palette, player sprites, UI elements. Making authors explicitly `load` and
maintain every such resource in code is error-prone (easy to forget to keep
them loaded) and adds boilerplate to every cart's `init`. A declarative
alternative avoids this for the common case.

## Decision

Resources the cart always needs can be declared persistent in the manifest:

```yaml
# cart.config.yaml
persistent_resources: [font_ui, palette_main, player_sprites]
```

The persistent declaration is an **eviction-policy and load-timing hint**,
not a separate access pattern. Persistent resources are:

- **Pre-loaded by the runtime before `init()` runs** — bytes decompressed
  and placed in the resource cache before any cart code executes.
- **Pinned in cache for the cart's entire lifetime** — never evicted under
  memory pressure; allocation failures elsewhere happen first.
- **No-op on `release`** — explicit release calls and `__gc` finalisation
  on a Lua handle to a persistent resource do nothing.

**Obtaining a usable handle is unchanged.** Carts still call
`blyt32.resource.load()` (or its Lua equivalent) on the resource ID to
get a typed handle of the kind required by the API they want to call (image,
audio, tilemap, etc., per ADR-0027 and ADR-0068):

```lua
local R = require("R")                       -- packer-generated IDs (ADR-0040)
local sprites = blyt32.resource.load(R.PLAYER_SPRITES)  -- typed image handle
sprites:blit(x, y)                           -- ADR-0068 method-style call
```

For persistent resources `load` is essentially free — the bytes are already
cached; the runtime only constructs the typed handle wrapper. Carts
typically do this once in `init()` and store the handle as an upvalue.

`R.PLAYER_SPRITES` itself remains an integer ID (ADR-0040), not a typed
handle: the runtime needs the typed-load call to know whether to produce
an image, audio, or tilemap handle for the same underlying bytes. The
persistence declaration does not bypass this — it changes when bytes are
loaded and whether they may be evicted, not how cart code reaches them.

Non-persistent resources use the same `load`/`release` API; the only
difference is that their bytes are loaded on the first `load` call and may
be evicted after `release` (ADR-0027).

## Consequences

- The common case (always-needed resources) is handled declaratively with
  zero code.
- Frees authors from manually loading fundamental assets in `init` and
  worrying about keeping them alive.
- The explicit load/release API (ADR-0027) remains for genuinely transient
  resources (cutscene art, per-level assets) where lifecycle matters.
- Persistent resources count against the 16 MB working memory budget from
  cart start; authors should be aware that large persistent sets reduce
  the available budget for runtime allocations.

## Implementation amendment (issue #160, 2026-06-29)

Realised as the last child of the resource-memory epic (#156), on top of the
compression (#157), eviction (#137), and unified-budget (#158) machinery.

**Manifest field & carrier.** The declaration lives in `blyt.build.yaml`
(`persistent_resources: [name, …]`, the resource names ADR-0040/ADR-0088
derive). The packer resolves each name to its integer resource id and emits the
sorted id list as a dedicated **`.cart.persistent` ELF section** (little-endian
`u32` ids). This is deliberately *not* carried in the FlatBuffers `.cart.config`
schema: the native bare-metal path is `-nostdlib` and discovers resources by
walking `.cart.resource.<id>` sections through the shared ELF reader
(`blyt_elf32_*`), so a parallel section read the same way keeps the host and
native paths in lockstep without pulling FlatBuffers into the metal path. The
section is emitted into both the packed `.blyt` and the dev ELF.

**Runtime.** A per-entry `persistent` bit ANDs on top of the single-sourced
refcount eligibility predicate (`blyt_rl_is_evictable`): a persistent entry is
excluded from eviction, from `evict_to_fit` victim selection, and from the
resident-evictable cache total, and is counted in the **non-evictable
footprint** from cart start. It is pre-loaded (materialised) before `init()`
runs, at the point where `guest_heap_used == 0`. `release`/`unpin` (and Lua
`__gc`) are no-ops on its residency by construction — eviction only ever touches
entries the persistent bit excludes. `mem.stats().resources_loaded` (ADR-0029)
lists persistent resources as resident from frame 0 (`load_count == 0`).

**Over-budget set — layered, single sum check** (`Σ decompressed size > 16 MiB`;
an individual oversized resource trips the same sum):

1. **Build-time guard (primary).** The packer fails the build when the declared
   set exceeds 16 MiB — deterministic and identical for every leg by
   construction (same packer, before any cart ships), and also the point that
   rejects an unknown resource name.
2. **Load-time validation (defensive).** The runtime preload re-checks the
   budget and refuses to start (host returns no session / native exits) — cover
   for a hand-crafted cart that bypassed the packer; an honestly-built cart
   never reaches it.
3. **Preload always fits.** Because preload runs at `guest_heap_used == 0` and
   the build guard bounded the set to ≤ 16 MiB, the reservation can never
   over-subscribe at preload.

A large-but-legal persistent set (e.g. 15 MiB) that starves the heap is *not* a
preload failure — it degrades to the existing unified-budget (#158)
deterministic allocation failure during `init()`, with the persistent bytes
safely resident (never a mid-frame corruption). Cache residency stays advisory
(ADR-0027 v2): persistence affects *which* entries are non-evictable, not the
deterministic success/failure of any allocation.
