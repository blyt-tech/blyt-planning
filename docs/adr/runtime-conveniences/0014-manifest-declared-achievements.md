# ADR-0014: Achievements — deferred to a future version

## Status
Deferred — not in v1.

## Context

Achievements are a quality-of-life feature players expect from modern games.
A runtime-provided design — manifest-declared achievements with a runtime
notification banner, browser UI, and per-cart persistence — would give a
consistent experience across every cart on the console without each cart
re-implementing notification visuals and storage.

The runtime UI surface is non-trivial, however: a notification banner
that does not fight with the cart's own draw output, an achievement
browser navigable from the pause menu (ADR-0064), localised strings, an
icon-rendering path, persistence format, conflict handling under hot
reload, etc. None of that is impossible, but each piece is polish that
benefits from real cart usage to inform decisions, and v1 has no such
usage to learn from.

## Decision

**Achievements are not part of v1.** The runtime ships no achievement
API, no notification banner, no browser, and no `achievements:` field
in `cart.config.yaml`. Carts that want achievement-like feedback in v1
implement it themselves (their own banner, their own save flag).

A future version will add a runtime-provided achievement system. The
intended shape — manifest-declared metadata, cart code only signals
unlocks, runtime owns UI and persistence — remains the design target;
deferring buys the time to design the UI properly against real cart
needs.

**Implications for related ADRs:**

- **ADR-0016 (game services):** v1 has no achievement-unlock event for
  service wrappers to subscribe to. Service achievement integration is
  consequently also deferred. The service event API itself (cart save
  written, session started/ended, cart loaded/unloaded) remains in v1.
- **ADR-0059 (packer-generated constants):** the `A_*` / `A` namespace
  for achievement handles is reserved for the future addition; no
  achievement constants are generated in v1.
- **ADR-0073 (manifest files):** `cart.config.yaml` has no
  `achievements:` field in v1. The schema will gain it when the feature
  ships.

## Consequences

- v1 cart authors who want achievements implement them ad-hoc; this
  produces inconsistency, but only across the small set of carts that
  ship achievements before the runtime feature lands.
- The runtime UI design (banner, browser, pause-menu integration,
  localisation) can be informed by what carts actually do, rather than
  guessed up front.
- Migration when achievements ship: existing carts with hand-rolled
  systems can adopt the runtime feature optionally; their pre-existing
  saves of unlocks remain in their own save data and can be migrated by
  the cart author if desired.
- RetroAchievements / Steam / GOG service mirroring is also deferred,
  consistent with ADR-0016's existing v2+ framing for service
  integration.
