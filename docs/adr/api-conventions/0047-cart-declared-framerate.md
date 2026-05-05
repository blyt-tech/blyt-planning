# ADR-0047: Cart-declared framerate

## Status
Accepted

## Context

Most carts run at 60 Hz, but some genres (turn-based games, visual novels,
certain puzzle games) have no meaningful gameplay reason to update 60 times
per second and would run correctly at 30 Hz or lower. The runtime needs to
know the intended tick rate to configure the frontend accumulator (ADR-0036)
and to set deterministic time expectations.

## Decision

**Carts declare their intended tick rate in `cart.config.yaml`; the default is 60.**

```yaml
# cart.config.yaml
fps: 30  # optional; default 60
```

The runtime exposes this to the frontend via `blyt_cart_fps()`, called after
cart load. The frontend uses the declared rate to configure its accumulator
loop: `dt = 1 / fps`, tick-interval = `1 / fps`.

`update(dt)` always receives the declared `dt` as a constant (e.g., `1/30`),
never a measured elapsed time. `blyt32.time.frame()` advances by 1 each
`update`, regardless of wall-clock rate.

**Supported rates:** any positive integer from 1 to 60 inclusive. The packer
rejects values outside this range. No further restriction — 35 (Doom-style),
50 (PAL), 24 (cinematic), 15, and any other integer in range are all valid.
Libretro's `retro_get_system_av_info` declares fps as a `double`, so the
libretro frontend path handles any value without special casing.

**No sub-rate rendering:** there is no facility for updating at one rate and
rendering at another (no interpolation between update ticks in v1). A 30 Hz
cart renders at 30 Hz. This keeps the model simple and avoids interpolation
artifacts.

## Consequences

- Turn-based and narrative carts can declare 30 Hz or 24 Hz and spend fewer
  CPU cycles without changing any game logic.
- The frontend accumulator loop is parameterized by `blyt_cart_fps()`; all
  frontends behave consistently for any declared rate.
- Netplay (ADR-0020) works correctly at any declared rate since tick counting
  is still integer-based.
- Replay and determinism are unaffected: `dt` is still a constant, inputs
  are still snapshotted per tick.
- Sub-rate rendering (render at 60, update at 30) is explicitly deferred to
  v2; the complexity is not justified for v1.
