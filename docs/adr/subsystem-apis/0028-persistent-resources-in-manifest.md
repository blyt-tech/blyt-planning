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
`console.resource.load()` (or its Lua equivalent) on the resource ID to
get a typed handle of the kind required by the API they want to call (image,
audio, tilemap, etc., per ADR-0027 and ADR-0068):

```lua
local R = require("R")                       -- packer-generated IDs (ADR-0040)
local sprites = console.resource.load(R.PLAYER_SPRITES)  -- typed image handle
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
