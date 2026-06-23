# ADR-0027: Explicit resource release API

## Status
Accepted

## Context

Long-running carts with phase-specific resources (cutscenes, level intros,
boss fights with unique assets) can accumulate decompressed resource cache
beyond the 16 MB working memory budget if resources are never freed. The
runtime needs a way to reclaim this memory. Two approaches: automatic GC-only
eviction, or explicit release combined with GC as a fallback.

## Decision

**Carts can explicitly release loaded resources:**

```lua
-- R_HERO_SPRITES: packer-generated compile-time constant (ADR-0059)
local hero_sprites = blyt32.resource.load(R_HERO_SPRITES)
-- ... use it ...
blyt32.resource.release(hero_sprites)
-- handle is now invalid; re-load returns a new handle
```

- `load` is idempotent: loading an already-cached resource returns quickly
  without re-decompressing.
- `release` is advisory: tells the runtime "cart no longer needs this."
  The runtime may or may not immediately free the decompressed bytes —
  keeping recently released resources cached enables fast re-load.
- Actual eviction is the runtime's decision. v1: evict under memory pressure
  only. v2: consider LRU with explicit hints.

**Lua GC integration:** resource handles are userdata with `__gc` metamethods
that call `release` on collection. Carts that don't manage lifecycles
explicitly still get reasonable behavior; explicit release is recommended for
large resources like cutscene art.

**Handle stability across save/restore:** resource handles encode logical
identity (resource name + load generation), not physical address. Save state
preserves which resources were loaded; restore re-establishes the physical
mapping. Handles survive save/load, hot reload, and cross-platform transport.

**Persistent resources** (ADR-0028) are declared in the manifest and are
never evicted; `release` on a persistent resource is a no-op.

## Amendment (ADR-0120, 2026-05-12): raw pointer access via pin/unpin

Cart code that needs to pass resource bytes directly to a C library
(e.g. via `fmemopen` for a library with a `FILE*` API, or directly via
a `load_from_memory(void*, size_t)` API) requires a stable pointer
guarantee that the advisory `load`/`release` model does not provide.

Two new calls are added, taking the integer resource ID (packer-generated
constant, e.g. `R_MY_ANIM_CLIP`) rather than a typed handle:

```c
blyt_result_t blyt_resource_pin  (blyt_resource_id_t id,
                                   const void **out_ptr,
                                   size_t      *out_size);
blyt_result_t blyt_resource_unpin(blyt_resource_id_t id);
```

```lua
local ptr, size = blyt32.resource.pin(R.MY_ANIM_CLIP)
blyt32.resource.unpin(R.MY_ANIM_CLIP)
```

**`pin` semantics:**
- Loads and decompresses the resource if not already cached.
- Increments an internal pin count for that resource ID.
- Returns a `const void*` pointer and byte size.
- While the pin count is greater than zero the runtime guarantees the
  pointer is stable **within the current frame**: the resource will not be
  evicted or moved before the frame boundary. The guarantee does **not**
  extend across frames (see "Frame scope" below).

**`unpin` semantics:**
- Decrements the pin count.
- When the count reaches zero the resource becomes eligible for eviction
  under memory pressure (same as after `release`).
- The pointer returned by the matching `pin` is invalid after `unpin`
  returns and must not be accessed.

**Reference counting:** each `pin` call must be matched by exactly one
`unpin`. Multiple concurrent `pin` calls on the same ID are valid; the
resource remains stable (within the frame) until all corresponding `unpin`
calls complete.

**Frame scope (clarified 2026-06-23):** a pin is valid only for the frame in
which it was taken. Any pin still held at the frame boundary is force-released
by the runtime, and its pointer must not be accessed in a later frame. The
runtime cannot promise a stable address across frames because dev-mode asset
hot-reload (ADR-0088) may replace a resource's bytes between frames; an
unbounded cross-frame pointer guarantee would conflict directly with it.
`pin`/`unpin` is a raw-access *window*, not a lifetime extension — the
canonical use (parse-then-close, below) already lives within a single frame.
A cart that needs the bytes to outlive the frame copies them into its own
buffer while pinned. (Keeping a resource *resident* across frames is what
`load`/`release` and persistent resources provide; that is about caching
bytes, not about a stable pointer.)

**Persistent resources** (ADR-0028) are permanently resident; `pin`/`unpin`
calls on them adjust a reference count but eviction never occurs regardless.
The frame-scope rule above still applies to the *pointer* returned by `pin`
— re-pin each frame to obtain a valid pointer — even though the underlying
bytes are never evicted.

**Typical pattern — parse-then-close:**

```c
const void *ptr;
size_t size;
blyt_resource_pin(R_DIALOG_SCRIPT, &ptr, &size);
FILE *f = fmemopen((void *)ptr, size, "r");
dialog_load(f);        /* library reads, parses, builds its own structs */
fclose(f);
blyt_resource_unpin(R_DIALOG_SCRIPT);
/* resource may now be evicted; library's internal structs remain valid */
```

The resource needs to remain pinned only for the duration of the library
call. Once the library has built its own representation from the bytes,
the resource can be unpinned and released.

## Consequences

- Authors with phase-specific large resources can proactively free memory at
  scene transitions, preventing budget exhaustion.
- The API is consistent regardless of whether a resource is compressed —
  `release` on an uncompressed mmap'd resource is a no-op; the cart doesn't
  need to know which case applies.
- v1 ships without automatic LRU eviction; authors relying on the runtime to
  manage cache size automatically may hit budget limits. The memory
  introspection API (ADR-0029) helps them detect and respond to this.
