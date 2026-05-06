# ADR-0012: Lua coroutines — transient via coroutine.create, persistent via blyt32.coroutine.create

## Status
Accepted; **amended 2026-05-06** based on Spike M findings —
`blyt32.coroutine.create{start, save, restore}` (table-of-three) is
superseded by `blyt32.coroutine.create(function(ctx), seed?)` with a
runtime-owned constrained-shape `ctx`.  See "Amendment — Spike M
findings" at the bottom of this document for the rationale, the
constrained shape spec, and the namespace additions.

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

> **Superseded by the Spike M amendment below** — the original
> table-of-three shape is documented here for historical context.
> New code must use the single-function form
> `blyt32.coroutine.create(function(ctx), seed?)`.

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

## Amendment — Spike M findings (2026-05-06)

Spike M (`spikes/spike-m/`, results in `docs/design/spike-m-results.md`)
validated the persistent-coroutine save/restore mechanism end-to-end on
a real Lua VM, against eight workloads, 29 save frames per workload,
4 cross-host directions per save frame — 928 cross-host runs, all
byte-identical.  The spike's findings collapse the original API and pin
the previously-implicit `ctx` shape; this amendment captures the
resulting contract.

### 1. API: single function, runtime-owned `ctx`

Replace:

```lua
blyt32.coroutine.create{
  start   = function(ctx) ... end,
  save    = function(ctx) return {…} end,
  restore = function(data) return {…} end,
}
```

with:

```lua
blyt32.coroutine.create(function (ctx) ... end, seed?)
```

`seed` is an optional table of initial `ctx` fields; defaults to `{}`.
The runtime owns ctx serialization and restoration entirely — `save`
and `restore` callbacks are elided.  Their realistic distribution in
the spike's workloads was pass-through identity; authors who need to
*transform* state at save time (drop a derived cache, recompute on
restore) place that derived state in **locals inside the body** —
locals are scoped to the body's frame, automatically rebuilt when
the body re-enters on restore, and never serialised:

```lua
blyt32.coroutine.create(function (ctx)
  local cache = build_expensive_lookups(ctx)  -- rebuilt on restore
  while ctx.step < N do
    cache:apply(ctx)
    ctx.step = ctx.step + 1
    coroutine.yield()
  end
end)
```

### 2. Body re-entrancy contract

On a load resume, the runtime re-creates the underlying Lua coroutine
from scratch via `_raw_create(body)` and resumes it once with the
deserialized `ctx`.  **The body's first action on that resume must
reach the equivalent yield point by virtue of loop conditions over
`ctx`'s contents — never by relying on the Lua coroutine's preserved
program counter.**

For bodies with a single yield site this is trivial — the loop
condition `while ctx.step < N` naturally drives the body to the same
next yield given any saved `ctx.step`.  For bodies with multiple
yield sites (branching arms, nested loops) the author must encode
the program counter into `ctx`.  The recommended pattern:

```lua
function (ctx)
  while ctx.step < N do
    if (ctx.pc or "loop_top") == "loop_top" then
      ctx.step = ctx.step + 1
      ctx.pc   = "after_a"
      coroutine.yield()
    end
    if condition_b and ctx.pc == "after_a" then
      …
      ctx.pc = "after_b"
      coroutine.yield()
    end
    ctx.pc = "loop_top"
  end
end
```

Spike M's Stage 2 demonstrated that the naïve form (without the
`ctx.pc` marker) fails when a save is taken inside (or just before)
a branched arm — the new coroutine restarts at line 1 and skips
the arm entirely.  The `ctx.pc` pattern is necessary AND sufficient
for branching cart code; it is the expected idiom for non-trivial
bodies.

If author feedback rejects the `ctx.pc` boilerplate during real-cart
authoring, an Eris-style suspended-`lua_State` serialisation is the
fallback — order-of-magnitude more complex but author-friendly.
This is flagged as a contingent follow-up; not required for the
amendment to ship.

### 3. Constrained `ctx` shape

`ctx` MUST conform to a constrained shape that the runtime can
flatten cross-host bit-deterministically:

- **Top-level keys** — strings only.  Integer-indexed top-level keys
  are not supported (top-level array shape would be ambiguous with
  the constrained-shape rule).
- **Top-level values** — one of:
  - `number` (integer or float subtype preserved via `lua_isinteger`),
  - `string`,
  - `boolean`,
  - a flat array (sequence) of the same primitive subtypes
    (`{1, 2, 3}` or `{"a", "b"}` etc., element types must be uniform).
- **NOT supported** — nested arbitrary tables, functions, userdata,
  metatables, `nil`-as-value (use `false` as a sentinel for absence).

The runtime's flatten protocol:

- Sort keys lexicographically by raw bytes (memcmp with length tiebreak).
- Emit `(field_count, [(key_len, key_bytes, tag, value_bytes)…])`.
- Integers as i64 little-endian.
- Floats as f64 little-endian, with quiet-NaN canonicalisation
  (single bit pattern `0x7ff8000000000000`).
- Strings as `(len: u32, bytes: u8[len])`, UTF-8 verbatim.
- Booleans as `u8` (0 or 1).
- Flat arrays as `(elem_tag: u8, elem_count: u32, [elem_bytes…])`
  with no per-element tag.

Violations of the constrained shape throw `BLYT_ERR_FLATTEN_*` at
flatten time, before any byte is written:

- `BLYT_ERR_FLATTEN_NON_STRING_KEY`
- `BLYT_ERR_FLATTEN_KEY_TOO_LONG`
- `BLYT_ERR_FLATTEN_UNSUPPORTED_TYPE`
- `BLYT_ERR_FLATTEN_ARRAY_MIXED_TYPES`
- `BLYT_ERR_FLATTEN_ARRAY_NON_SEQUENCE`
- `BLYT_ERR_FLATTEN_OVERFLOW`

Cross-host bit-equality of the emitted bytes is the spike's
load-bearing property under Stage 4's all-S sweep; the constrained
shape exists specifically to make that property achievable on a
PUC-Lua VM whose internal table iteration order is implementation-
defined.

### 4. Namespace additions

In addition to `create(body, seed)`, the `blyt32.coroutine` namespace
exposes:

- `resume(handle) → (ok, ctx)` — primary call site each frame.
  Returns `false, nil` if the slot is empty (body returned, slot
  auto-reclaimed).  Returns `true, ctx` after a successful body
  yield; `ctx` is the runtime-owned table the body has been
  mutating.
- `destroy(handle)` — early cancellation (player skips a cutscene;
  cart cancels owning-entity scripts).  Frees the slot immediately.
- `status(handle) → "suspended" | "running" | "dead" | "none"` —
  mirrors stock Lua's `coroutine.status` against the underlying
  coroutine; `"none"` if the slot is empty.

A body that returns (i.e. its outer loop exits) auto-reclaims its
slot.  Carts can detect "this script completed before save" on a
load resume via `console.script_has_saved_bytes(slot)` (or whatever
the production equivalent is) and skip re-creating the script.

### 5. Scripts have their own identity; entity refs are handles in `ctx`

The runtime does not couple scripts to entities.  A script is a
free-standing thing with its own slot in the runtime's
persistent-script table.  If a script needs to operate on a
specific entity, it stores that entity's handle (an integer index
into the cart-state buffer's entity array) inside its own `ctx`:

```lua
blyt32.coroutine.create(function (ctx)
  while console.entity_alive(ctx.entity) do
    local e = console.get_entity(ctx.entity)
    e.x = e.x + math.cos(ctx.step * 0.1)
    console.set_entity(ctx.entity, e)
    ctx.step = ctx.step + 1
    coroutine.yield()
  end
end, {entity = 3, step = 0})
```

Three properties fall out of this:

- **No script-type declaration** — carts do not register "type
  DialogState attaches to entity type NPC"; they create a script
  and put whatever handle they want in `ctx.entity`.
- **No per-entity script limit** — two scripts can hold the same
  entity handle in their respective `ctx`s; they execute
  independently against the same entity row.
- **No automatic lifecycle coupling** — a script does not die when
  its referenced entity dies.  The body checks
  `console.entity_alive(ctx.entity)` and exits cleanly, at which
  point the runtime auto-reclaims the slot.

### 6. Load-resume idiom contract

On a load resume, the cart re-runs from the top of its Lua main.
For each `blyt32.coroutine.create(body, seed)` call the cart
makes, the runtime allocates the next free slot (starting at 0 if
the load-side slot bitmap was just cleared) and reads that slot's
saved bytes; if non-empty, the bytes deserialize to `ctx` and
override the seed.

**Cart authors are responsible for structurally mirroring the
save-time topology on load.** For workloads with destroy/recreate
patterns (a script that gets destroyed at frame N and replaced by
a different body at frame N+1), the cart uses `console.frame()` (or
a similar topology marker) to gate which scripts it creates so that
its load-side `create` order matches the save-side slot allocation
order.

A wrapper-managed slot-keyed body-id (16-byte name stored alongside
the slot's flattened ctx) would simplify cart authoring at the cost
of a richer wrapper protocol.  Spike M's structural approach
minimises wrapper complexity but pushes a discipline burden onto
cart authors; the body-id refinement is flagged as a non-blocking
follow-up.

### 7. Slot-table sizing is a manifest concern

`MAX_PERSISTENT_SCRIPTS` and per-slot blob size are runtime
constants; production should expose them via the cart manifest
(ADR-0009) so carts declare their actual ceilings.  Spike M
hard-codes 64 slots × 256 bytes; production carts may need
substantially more for either dimension.

Slot-exhaustion at runtime returns `BLYT_ERR_SLOT_EXHAUSTED`;
slot-blob overflows return `BLYT_ERR_SLOT_BLOB_OVERFLOW`.

### Open follow-ups (non-blocking for ADR ratification)

- **Per-resume dirty-bit flatten cache.**  Required before netplay
  ships (per-frame serialise-send-deserialise cost shape makes
  unconditional flattening expensive).
- **Wrapper-managed transient ID list** for boundary-cross
  invalidation.  Spike M's Stage 5 cart marks the boundary-crossed
  transient explicitly via a temporary `mark_boundary_crossed`
  hook; production needs the runtime to serialise a list of
  suspended transient IDs into the buffer and auto-mark the
  matching N-th newly-created transient on load.  The throw
  mechanism this amendment validates is unchanged; only the
  bookkeeping differs.
- **Slot-keyed body-id in slot bytes.**  See §6.
- **Manifest-declared dynamic slot-table cap.** See §7.
- **Cross-Lua-version save migration.**  Spike B pins the Lua
  version; the flattener's wire format is independent of Lua's
  internal representation, but no test covers version drift.
- **Eris alternative.** Contingent on author feedback rejecting
  the `ctx.pc` re-entrancy idiom for complex bodies.
