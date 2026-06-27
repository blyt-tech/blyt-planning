# ADR-0068: Lua handles are objects with methods

## Status
Accepted

## Context

The C API uses opaque `uint32_t` handles for all runtime objects. In Lua,
passing handle integers to module functions (`blyt32.image.blit(img, x, y)`)
is correct but verbose and does not leverage Lua's object-oriented idioms. A
metatables-based wrapper makes Lua cart code more concise and idiomatic.

## Decision

**In the Lua API, handles are userdata or table objects with methods.**

Rather than:
```lua
blyt32.image.blit(img, x, y, {})
blyt32.voice.stop(voice)
local active = blyt32.voice.is_playing(voice)
```

Cart code writes:
```lua
img:blit(x, y)
voice:stop()
local active = voice:is_playing()
```

Handle objects are thin wrappers around the underlying `uint32_t` handle.
They support `__eq` (compare by handle value), `__tostring` (debug name),
and `__gc` (advisory release, consistent with ADR-0027).

**Expired handle behavior:** an expired or invalid handle (e.g., a voice
handle whose voice has finished) is a silent no-op on all method calls except
`is_playing()`, which returns `false`. No error is raised for expired voice
handles — this is intentional: carts that fire-and-forget sounds do not need
to guard every method call with a validity check.

**Untracked voice handles:** `is_playing()` also returns `false` for
voices in untracked groups (ADR-0054) — SFX and any cart-declared
group not opted into tracking. The cart never sees a `true`
`is_playing` for these voices, so branching on them is not
meaningful. Dev mode warns on such calls so the author either moves
the voice to a tracked group or removes the check.

**State buffer handles** (`blyt_buffer_h`) are not Lua objects — they are
accessed via the SOA metatable sugar (ADR-0011) which provides `buffer.field[i]`
syntax at the aggregate level rather than per-slot object wrappers.

## Consequences

- Lua code reads naturally as method chains on objects.
- The zero-invalid-handle convention (ADR-0046) maps to silent no-ops, making
  fire-and-forget sound patterns safe.
- Expired voice handles are distinct from errors; `voice:is_playing()` is
  the idiomatic check before calling methods that matter only when active.
- The Lua object layer is thin (metatables pointing to C functions); no
  significant overhead over the bare handle integer approach.

## Amendment (2026-06-27): first concrete typed resource handle — text vs raw (#166)

Issue #166 implements the first concrete ADR-0068 typed handle: the resource
content kind (`text` vs `raw`/bytes). It establishes the cross-language scheme
that later typed handles (image, voice, font, tilemap) follow, and resolves how
"typed-ness" is expressed in each language given that **the runtime is
deliberately byte-blind to resource type** — type is a *codegen-only* property
(no host type field, no type carried in the ELF/index, no resource ECALL
change). The kind is decided at build time by extension (ADR-0088 amendment
2026-06-27) and flows only into generated constants and the SDK layer.

**Lua (this ADR's domain) — typed constant objects + kind-specific metatables.**
Generated `cart_resources.lua` emits typed constants rather than bare integers:
`R.GREETING = blyt.resource.text_resource(1)`, `R.BLOB =
blyt.resource.bytes_resource(2)`. `R.X:load()` returns a kind-specific handle:
the text metatable carries `:text()` + `:release()`, the bytes metatable carries
`:bytes()` + `:release()`. The wrong accessor is simply **absent** from the
metatable — `:text()` on a bytes handle raises "attempt to call a nil value
(method 'text')" — which is the Lua analogue of ADR-0068's object model rather
than a runtime type check. The underlying value `:text()`/`:bytes()` returns is
an ordinary Lua string (a byte buffer). The low-level `blyt.resource.pin/unpin`
escape hatch stays kind-agnostic and id-based. This must be implemented
**identically** in the guest Lua runtime and the WASM host-Lua fast path.

**Rust — concrete newtypes, not phantom `Handle<K>`.** `TextResource` /
`BytesResource` (the generated constants), `LoadedText` / `LoadedBytes`,
`PinnedText` / `PinnedBytes`; method-style `R_X.load()`. The text accessor
exists only on the text types, so misuse is a compile error. Concrete newtypes
(not a phantom `ResourceHandle<Kind>`) are chosen for consistency with ADR-0108's
handle taxonomy: future typed handles (image/voice/font) are distinct concrete
newtypes with non-generalizing APIs, so phantom typing would make the
content-resource pair the odd ones out. Phantom typing (à la `FieldHandle<B>`)
pays off only when an operation genuinely generalizes over the kind, which the
text/bytes accessors do not.

**C — distinct integer typedefs, no compile enforcement.**
`blyt_text_resource_t` / `blyt_bytes_resource_t` (both `uint32_t` aliases,
consistent with ADR-0108's integer-typedef C handle family). C aliases give no
compile-time enforcement; the error path is the text contract (see ADR-0088
amendment): `blyt_resource_text_get` on a raw resource fails the trailing-NUL
check and returns `NULL`. The unsafe tier accepts the residual hole (a raw blob
that happens to end in `0x00`).
