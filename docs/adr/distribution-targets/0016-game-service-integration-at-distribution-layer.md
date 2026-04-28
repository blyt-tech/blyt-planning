# ADR-0016: Game-service integration at the distribution wrapper layer

## Status
Accepted

## Context

Game services (Steam, GOG Galaxy, Epic Online Services, Xbox Live, etc.)
provide cloud saves, achievement sync, leaderboards, rich presence, and
storefront integration. Integrating these directly into the runtime would
embed service-specific SDKs, create per-service maintenance burden, and couple
the cart format to specific services. Carts running on multiple storefronts
would need to be aware of which service they're running under.

## Decision

**Services integrate at the frontend/distribution wrapper layer, not in the
cart or runtime.**

A Steam-distributed version of the console is a Steam-integrated frontend
hosting the standard runtime. The same cart binary runs identically whether
launched via Steam, GOG Galaxy, itch.io, or standalone.

**v1 design requirements that keep integration doors open:**

1. **Event-based communication:** cart actions services care about are
   runtime events. Frontends subscribe via the runtime's C API: cart save
   written, session started/ended, cart loaded/unloaded. A Steam wrapper
   subscribes and forwards. (Achievement-unlock events arrive when the
   runtime achievement system ships in a future version, ADR-0014.)
2. **Frontend-owned cart identity:** carts carry no `cart_id` field. Each
   distribution channel keys saves and service entitlements with the
   identifier native to that channel (Steam app ID, itch game ID, GOG
   product ID, etc.). Standalone frontends pick a scheme appropriate for
   how they store carts. This avoids a global cart-id namespace and the
   coordination/collision problems that come with one.
3. **Filesystem-visible save directories:** per-cart dirs on disk make cloud
   save sync trivial — a wrapper syncs the directory.
4. **No assumed online connectivity:** core features work offline; service
   sync augments, doesn't replace.

**Explicitly not v1 scope:** Steam SDK / GOG / Epic SDK integration, generic
`console.services.*` API, authentication, DLC, service overlay UI,
cross-service cloud save.

## Consequences

- Carts have no service-specific API calls. The same cart works on any
  distribution without modification.
- Service wrappers are separate projects (~a few thousand lines each),
  buildable by any motivated party without modifying the core runtime.
- The runtime's C event API becomes load-bearing for service integration;
  it must be stable and well-documented.
- In v1, players have local-only saves. Cloud sync is a future addition
  via distribution wrappers, alongside the achievement system itself
  (ADR-0014) and any service-side mirroring.
