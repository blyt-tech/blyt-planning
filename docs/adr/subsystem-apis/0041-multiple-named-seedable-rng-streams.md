# ADR-0041: RNG — two runtime streams, manifest-extensible, session-seeded

## Status
Accepted

## Context

Random number generation must be deterministic (ADR-0007): given the same
inputs, the simulation produces the same outputs. The seed is one of those
inputs, captured into session state from frame zero.

A single global stream is the obvious starting point. The risk it creates is
narrow but real: an `rng.next()` call added for a cosmetic effect (particle
jitter, screen-shake offset) advances the same stream that gameplay code
reads from. For full-snapshot replay (save state, rewind, netplay rollback)
this never matters — the snapshot restores RNG state verbatim. It only
matters for the hypothetical case of replaying a recording across code
edits, and for the practical case of an author wanting a worldgen seed
that stays stable while they iterate on cosmetic effects.

The earlier draft of this ADR resolved the risk with 16 manifest-declared
streams. That was overweight for what it accomplishes — the actual need is
a clean place to put "this randomness doesn't feed back into simulation"
(so cosmetic edits never disturb anything else), plus an escape hatch for
authors who want finer separation.

## Decision

**Two runtime-provided streams; cart-declared streams in the manifest;
all state runtime-tracked; per-session entropy by default.**

### Runtime-provided streams

Defined in `fc_cart.h`, always available without manifest declaration:

| Constant | Purpose |
|----------|---------|
| `FC_RNG_DEFAULT` | The standard stream. Gameplay and anything that feeds back into simulation reads from here. |
| `FC_RNG_COSMETIC` | Randomness whose result is consumed and thrown away the same frame — particle jitter, screen-shake offset, sound pitch variation. Nothing in the simulation reads back from this stream. |

The split exists so authors have a documented home for output-only
randomness. Routing cosmetic `rng.next()` calls through `FC_RNG_COSMETIC`
keeps `FC_RNG_DEFAULT`'s sequence undisturbed by polish work, which makes
fixed-seed worldgen and any future input-only replay system viable
without further plumbing.

### Cart-declared streams

Carts that want finer separation (e.g., separate worldgen and AI streams
so AI tweaks don't shift world generation) declare extras in the manifest:

```yaml
# cart.config.yaml
rng_streams: [worldgen, enemy_ai]
```

The packer generates per-stream constants following the universal
packer-generated-constant pattern (ADR-0059):

```c
// Generated: cart_rng.h
#define RNG_WORLDGEN ((fc_rng_h)1)
#define RNG_ENEMY_AI ((fc_rng_h)2)
```

```lua
local RNG = require("RNG")
console.rng.next(RNG.DEFAULT)     -- runtime-provided
console.rng.next(RNG.COSMETIC)    -- runtime-provided
console.rng.next(RNG.WORLDGEN)    -- cart-declared
```

Per ADR-0059, the Lua `RNG` module is pre-populated with runtime constants
before the packer appends cart-declared ones; authors call `require("RNG")`
regardless of origin.

### Seeding

**Per-session entropy by default.** At cold start the frontend supplies a
64-bit session root seed (from OS entropy on desktop / mobile, from a
suitable source on hardware). This seed is captured into session state
from frame zero and is therefore part of every save snapshot, every
replay recording, and every netplay handshake.

**`FC_RNG_DEFAULT` is seeded directly with the session root.** All other
streams (`FC_RNG_COSMETIC` and any cart-declared streams) derive their
seeds deterministically from the root using a documented mixing function
(splitmix64 keyed by stream ID). One root reproduces the full RNG state;
adding or removing cart-declared streams never disturbs the seeding of
existing streams.

**Manifest override** for carts that want a fixed root (demos, deterministic
screensavers, daily challenges with author-controlled seed):

```yaml
# cart.config.yaml
# rng_seed: 0xDEADBEEF   # optional; default is per-session entropy
```

When set, the manifest seed replaces the session root entirely. All streams
re-derive from it.

**Runtime reseeding** is always available:

```lua
console.rng.seed(RNG.DEFAULT, today_as_int)   -- e.g., daily challenge
console.rng.reseed_all(new_root)              -- rederive every stream
```

### State and persistence

All stream state lives in the runtime's tracked region and is saved /
restored automatically with the rest of cart state (ADR-0010, ADR-0013).
Carts cannot directly hand the runtime a buffer to use as RNG state — the
runtime owns the storage and the implementation choice (xoshiro128**, 16
bytes per stream).

### Netplay

Session seed agreement happens in the lobby handshake alongside cart-hash
exchange (ADR-0021): the host generates the session root and broadcasts
it; clients adopt it before tick 0. From there, every client derives
identical per-stream seeds locally. Lockstep simulation needs nothing
further from the RNG layer — same root + same code + same inputs = bit-
identical state.

### Implementation

xoshiro128** (or equivalent 32-bit-output PRNG with well-characterised
statistical properties). 16 bytes of state per stream. The mixing function
for derived per-stream seeds is splitmix64; both are stable across runtime
versions so save/replay compatibility is preserved.

## Consequences

- The common case is two streams the cart never has to think about. Authors
  only declare extras when they have a reason — most carts won't.
- Adding or removing a cosmetic `rng.next()` call cannot perturb gameplay,
  worldgen, or any other simulation-affecting stream, because they live
  on different streams seeded independently from the session root.
- Default-entropy seeding means each player's first run differs naturally;
  the determinism contract is preserved because the seed is just another
  input captured from frame zero. Authors who want fixed-seed reproducibility
  set `rng_seed` in the manifest or call `console.rng.seed()` at init.
- Save/restore, rewind, and netplay rollback all work transparently — the
  full RNG state is in the snapshot.
- Netplay seed agreement is part of the existing handshake (ADR-0021); no
  new netplay-specific RNG protocol is needed.
- Cart-declared stream identifiers follow the same packer-generated-constant
  pattern as resources, RNG, locale keys, etc. (ADR-0059) — no string
  lookups at runtime, no new mental model.
- The splitmix64 derivation function is documented and stable. Changing it
  would invalidate all existing save states; treated as a major-version
  break (ADR-0032) if ever needed.
