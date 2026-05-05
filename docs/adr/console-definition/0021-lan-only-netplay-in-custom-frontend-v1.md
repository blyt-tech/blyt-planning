# ADR-0021: LAN-only netplay in the custom frontend for v1

## Status
Accepted

## Context

The custom libretro frontend (ADR-0034) does not automatically inherit
RetroArch's lobby and netplay UI — it is a separate program that uses the
libretro API. Three options were evaluated for v1:

- **Option A:** Defer netplay to RetroArch in v1. Custom frontend exposes no
  netplay; users wanting netplay use RetroArch.
- **Option B:** Implement LAN discovery only. Same-network multiplayer
  without internet infrastructure.
- **Option C:** Full netplay parity — LAN discovery + internet lobby
  (`lobby.libretro.com`) integration in the custom frontend.

## Decision

**Option B — LAN-only netplay in the custom frontend for v1.**

LAN discovery and host/join UI for same-network multiplayer is implemented
in the custom frontend. Internet lobby is not integrated in v1; players who
want internet netplay use RetroArch.

**What v1 builds:**

*LAN discovery:* UDP broadcast on a chosen port at ~1–2 second intervals
while hosting; UDP listener on the same port while browsing. Broadcast
payload: cart binary hash (BLAKE3-256 of the loaded `.blyt` file), cart
title (display only, taken from `cart.info`), host name, player count
(current/max), host IP. Hosts aged out after ~5 seconds without broadcasts.

*Host/join UI:* "Host multiplayer session" and "Join LAN session" options in
the frontend menu. Player connection screen. In-session status indicator.
Disconnection handling.

*Cart compatibility check:* join attempt includes the joining client's cart
hash; host accepts only when the hashes match exactly. This is stricter than
a developer-declared id+version (no chance of two builds claiming the same
identity, no need for authors to remember to bump a version field) and free
to compute since the host already has the cart loaded. A mismatch returns
a clear error message.

*Connection protocol:* uses libretro-common's netplay infrastructure for
input exchange and rollback — not reimplemented.

**Estimated scope:** ~1000–1500 lines of frontend code.

## Consequences

- LAN netplay covers the highest-value use case: friends in the same room,
  each with their own device, playing together without internet infrastructure.
- Internet lobby users are enthusiasts happy to use RetroArch.
- Internet lobby (Option C) is deferred to v2: it adds NAT traversal, relay
  server fallbacks, and lobby protocol work.
- Browser netplay is a separate problem (WebRTC required due to browser
  security restrictions on TCP/UDP) and is also v2+.
- QR code joining is the preferred UX for browser netplay when built (host
  displays QR; nearby devices scan to join).
