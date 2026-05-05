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
