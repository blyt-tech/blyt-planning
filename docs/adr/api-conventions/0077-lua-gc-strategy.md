# ADR-0077: Lua GC strategy — generational mode, per-tick minor collection, emergency fallback

## Status
Accepted

## Context

Lua's default GC runs automatically, triggered by allocation pressure. In a
game loop, this produces unpredictable timing: a GC step can fire mid-update
at any allocation, introducing frame time variability that is difficult to
reason about and hard for authors to diagnose.

The goal is predictable GC timing without accepting the risk of unbounded
memory growth if a cart over-allocates within a single tick.

Lua 5.4 (ADR-0066) introduced generational GC mode, which is well-suited
to game workloads. Most Lua allocations in a game are short-lived
(temporaries, intermediate tables, string concatenations) and die in the
same frame they were created. Generational GC exploits this: a **minor
collection** scans only recently-allocated objects and is very cheap when
most of them are dead. A **major collection** runs less frequently and
handles long-lived objects that survive to the old generation.

## Decision

**The runtime disables Lua's automatic GC and manages it explicitly.**

### Per-tick minor collection

At the end of each `update()` tick — after cart code returns, before
`draw()` is called — the runtime performs a minor collection:

```c
lua_gc(L, LUA_GCSTEP, 0);   // in generational mode, steps a minor collection
```

This clears objects that were allocated during `update()` and did not
survive to the end of the tick. The cost is proportional to the size of
the young generation: if the cart allocated little, the step is essentially
free (a check that the young generation is already empty). If the cart
allocated heavily, the step reclaims those objects promptly before they
accumulate.

Major collections fire automatically as part of Lua's generational
machinery when enough objects are promoted from young to old — no
additional management is required.

### Emergency full collection as safety net

If a cart allocates enough within a single tick to exhaust available memory
before the end-of-tick minor collection runs, Lua's allocator triggers an
**emergency full collection** automatically before retrying the failed
allocation. This is built into Lua's core and requires no additional
runtime code. The `isemergency` flag suppresses finalizer calls during
the emergency collection to avoid re-entrancy.

Outcomes:
- **Emergency collection frees sufficient memory**: the allocation retries
  and succeeds. The cart is unaware anything happened; the frame runs
  slightly long.
- **Emergency collection cannot free sufficient memory**: `LUA_ERRMEM`
  propagates as a Lua error. The cart is genuinely over its memory budget
  and the error is correct.

The emergency collection is a safety net for bursts, not a substitute
for the per-tick minor collection. Carts that regularly trigger emergency
collections are over-allocating and should use object pooling or the
console's typed state buffers (ADR-0009) for hot-path data.

### Dev-mode tracking

In dev mode, the runtime counts emergency full collections and surfaces
them in the debug overlay: "emergency GC on frame N". This turns a silent
frame spike into an actionable signal. Authors seeing emergency GC warnings
know to profile and reduce per-frame allocations on the identified frames.

No tracking overhead in release builds.

### What is not managed

The runtime does not attempt time-budgeted GC (running GC only if frame
time remains). Under load — when allocations are heaviest — there is least
remaining frame time, so time-budgeted GC would suppress itself precisely
when it is most needed, allowing memory to grow until an emergency
collection fires anyway or `LUA_ERRMEM` is raised. The per-tick minor
collection runs unconditionally; its cost is low enough to absorb even
when the tick is otherwise busy.

## Consequences

- GC always fires at a predictable point in the frame (end of `update()`),
  never mid-cart-code.
- Minor collection cost when the cart allocates little is near-zero.
- Emergency full collection provides a transparent safety net for
  within-tick allocation bursts, with no cart-visible effect if memory
  is available.
- Carts that over-allocate receive clear dev-mode feedback rather than
  unexplained frame spikes.
- No Lua GC API is exposed to carts. GC is an implementation detail of
  the runtime's Lua hosting; cart code neither configures it nor observes
  it directly.
