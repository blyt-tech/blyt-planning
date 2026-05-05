# ADR-0012: Lua coroutines — transient via coroutine.create, persistent via blyt32.coroutine.create

## Status
Accepted

## Context

Coroutines are useful for sequenced game logic — cutscenes, state machines,
dialog flows, animations. However, serializing arbitrary coroutines across
save/restore cycles is a hard problem. Libraries like Eris can serialize Lua
coroutine state, but they are heavyweight, tightly version-locked to specific
Lua releases, and difficult to maintain.

The naive approach — leaving raw `coroutine.create` available with a
documentation note that coroutines don't survive saves — silently discards
coroutine state at save/restore/reload/rewind boundaries. The failure is
distant from the cause and produces no error; the author just notices their
cutscene reset.

Removing `coroutine.create` from the namespace entirely would break vendored
pure-Lua libraries that use coroutines internally as generators or for
within-frame async patterns.

## Decision

**`coroutine.create` and `coroutine.wrap` are replaced at environment
setup time with runtime-provided wrappers.** Cart code and third-party
libraries call them normally; the wrappers add tracking and boundary
enforcement transparently.

### Tracking

The runtime replaces `coroutine.create` and `coroutine.wrap` before any
cart code runs, registering every created coroutine in a weak-keyed table:

```lua
local _raw_create = coroutine.create
local _live = setmetatable({}, {__mode = "k"})  -- weak; dead coroutines GC automatically

coroutine.create = function(fn)
  local co = _raw_create(fn)
  _live[co] = true
  return co
end
```

`coroutine.resume` is also wrapped to check an invalidation flag before
delegating to the raw resume.

### Boundary enforcement

At every save/restore/reload/rewind boundary the runtime walks `_live`,
checks `coroutine.status(co) == "suspended"`, and marks any suspended
coroutines as invalidated. Attempting to resume an invalidated coroutine
throws immediately:

```
RuntimeError: coroutine crossed a save/restore boundary.
Use blyt32.coroutine.create{} if this coroutine needs to survive saves.
```

In dev mode the error includes the traceback from the original
`coroutine.create` call site, making the source easy to locate.

In dev mode the runtime also emits a warning at the boundary itself (before
any resume attempt), so authors learn about the problem as soon as it occurs
rather than when the next resume is hit.

### Persistent coroutines

Authors who need coroutines that survive save/restore use
`blyt32.coroutine.create{}` — the only function in the `blyt32.coroutine`
namespace. Its different call shape (a table of three named callbacks vs a
single function) makes the distinction unambiguous at every call site:

```lua
blyt32.coroutine.create{
  start   = function(ctx) ... end,
  save    = function(ctx) return {step = ctx.step, ...} end,
  restore = function(data) return {step = data.step, ...} end,
}
```

The runtime calls `save` on serialize and `restore` on deserialize, giving
the author full control over which state persists. Persistent coroutines are
not entered into `_live` and are never invalidated.

## Consequences

- Third-party pure-Lua libraries using coroutines internally are unaffected:
  coroutines that start and complete within a single `update()` or `draw()`
  are never suspended at a boundary and are never invalidated.
- The failure mode for crossing a boundary changes from silent state loss
  to an immediate, descriptive error at the resume site (or earlier, at the
  boundary itself in dev mode).
- No Eris-style full serialization is needed; the runtime stays simple and
  version-stable.
- `coroutine.create` and `coroutine.wrap` behave identically to their Lua
  standard counterparts for all within-frame usage; the wrapping is invisible.
- The `blyt32.coroutine` namespace is minimal — a single function,
  `blyt32.coroutine.create{}`, for the persistent case only. Transient
  coroutines are just `coroutine.create`; no `blyt32.coroutine.transient`
  or similar is needed.
- The call-site shape difference (`coroutine.create(fn)` vs
  `blyt32.coroutine.create{start=…, save=…, restore=…}`) makes intent
  unambiguous without additional API surface.
- `blyt32.coroutine.create{}` covers the main use cases for persistent async
  logic: cutscenes, sequenced animations, AI state machines.
- The author provides save/restore hooks — they know their own state better
  than the runtime does. The API is explicit about what persists.
