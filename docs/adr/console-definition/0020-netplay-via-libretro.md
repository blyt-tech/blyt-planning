# ADR-0020: Netplay via libretro infrastructure

## Status
Accepted

## Context

Multiplayer netplay is valuable but notoriously hard to implement correctly.
The console's determinism guarantee (ADR-0007) makes libretro's rollback
netplay infrastructure directly applicable: because both machines run identical
simulations and only inputs need exchanging, the hard networking work is already
solved by libretro/RetroArch.

The alternative — building a custom netplay implementation — would require
peer discovery, input exchange protocol, rollback state management, latency
measurement, relay servers, and spectator mode. This is weeks to months of
engineering that libretro provides for free.

## Decision

**Netplay is provided via libretro's infrastructure.** Because the runtime
is a libretro core and is deterministic, multiplayer netplay works for free
through RetroArch without any netplay engineering by the console project.

**Netplay UI is frontend-owned, not cart-owned.** Session management (host,
browse, join, configure input delay/rollback, assign player slots,
disconnection handling) all live in the frontend. Cart code is unaware of
netplay.

**Cart-level API:**
```lua
local me = console.input.local_player()  -- which player slot this machine is
```
Used for per-player rendering in asymmetric games. Most games are symmetric
and ignore it.

**Per-player views:** both machines run the same simulation; each machine
renders from its local player's perspective based on `local_player()`.

**Hidden information:** the lockstep model runs identical state on both
machines. Client-side view filtering works for honest players; determined
cheaters can read hidden state from memory. This is a known characteristic of
peer-to-peer deterministic lockstep (Age of Empires, StarCraft, fighting games
all share it). Client-server anti-cheat is out of scope.

## Consequences

- Cart authors do not become network programmers. Any deterministic cart
  supports netplay automatically without code changes.
- RetroArch provides lobby, connection, rollback, spectator mode, and
  latency measurement for the libretro frontend path.
- Bandwidth is minimal: input state per frame (a few bytes per player) at
  60 Hz is ~1 KB/s.
- The custom libretro frontend (ADR-0034) handles its own netplay UI; it
  does not automatically inherit RetroArch's lobby (see ADR-0021 for the
  custom frontend netplay decision).
- Games with information-based hidden state (fog of war, hidden card hands)
  work for friendly play but are not cheat-proof in competitive contexts.
