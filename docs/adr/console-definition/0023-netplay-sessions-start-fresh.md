# ADR-0023: Netplay sessions start fresh; cart saves are cart-author's responsibility

## Status
Accepted

## Context

When a netplay session starts, the question arises: whose save file (if any)
should the session load? Options include using the host's save, using a fresh
start, or letting the cart decide. A fixed policy (always load host's save, or
always start fresh) avoids edge cases but may not suit all games.

## Decision

**Netplay sessions do not automatically load or save cart-managed saves.**
Each netplay session starts the cart fresh. Session ends when the cart exits;
nothing persists to individual cart save slots.

Cart-managed save/load during netplay is the cart author's responsibility.
Carts that want multiplayer persistence can do it themselves via the cart save
API (ADR-0013), with whatever rules they want (host's save wins, vote to save,
etc.).

Save states and rewind continue to work normally during netplay — these are
runtime state snapshots, not cart saves, and libretro handles them across the
netplay session (all participants rewind together).

## Consequences

- Simplest model; avoids "whose save does the session use?" and "what if
  players have different progress?" edge cases.
- Matches how typical couch multiplayer works (no one saves a Bomberman
  session; you play and stop).
- Multiplayer persistence is available to cart authors who want it; it just
  isn't automatic.
- Save states and rewind remain available in netplay — players can jointly
  rewind a bad moment, for example.
