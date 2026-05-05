# ADR-0022: Netplay — join during lobby phase; player count fixed once session starts

## Status
Accepted

## Context

Libretro architecturally supports mid-session drop-in/drop-out with new
players caught up via save state synchronization. In practice, libretro has
long-standing bugs specifically around 3+ player drop-in/drop-out. More
importantly, mid-session joining creates cart-design complexity ("where does
a new player spawn? what's their starting state?") that most carts don't
naturally handle.

## Decision

**During the lobby phase** (before the cart starts): players join and leave
freely, up to the cart-declared maximum (max 4). Host starts the session when
ready.

**During play:** player count is fixed at what it was when the session started.
- If a player drops out (disconnect/quit), their input stream goes to zero;
  the cart sees them as "not pressing anything."
- If the host drops out, the session ends.
- No new joiners mid-session.

```lua
-- Carts that want to handle drop-out gracefully:
if not blyt32.input.is_player_connected(3) then
  -- handle: AI takeover, pause, remove from match
end
```

Carts that don't check get default behavior: disconnected player reads as
no input — safe and unremarkable.

## Consequences

- Matches how most couch multiplayer naturally works ("everyone ready?
  let's go!").
- Avoids libretro's rough edges around mid-session player count changes.
- Cart authors don't need to handle mid-session spawn/init edge cases unless
  they explicitly opt in to that design.
- A disconnected netplay player is indistinguishable from a local player
  who isn't pressing anything — the cart can't tell it's running netplay.
- Mid-session joining (with cart opt-in via manifest declaration), host
  handoff, and spectator mode UI are deferred to v2+.
