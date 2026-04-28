# ADR-0032: Custom libretro frontends for standalone and hardware distribution

## Status
Accepted

## Context

The runtime ships as a libretro core. For RetroArch users, that's sufficient.
For standalone desktop and hardware distribution, a frontend is needed that
provides a branded experience. The options were:

1. Ship RetroArch as the standalone launcher. Simple, but GPL-3.0 triggers
   distribution obligations; no custom branding; RetroArch's UI is generic
   and exposes many irrelevant settings.
2. Build a simple custom frontend with no libretro dependency. Full brand
   control, but loses shader support, rewind UI, and input abstraction
   infrastructure — all needing to be rebuilt from scratch.
3. **Build a custom libretro frontend** — a separate program that uses the
   libretro API (the same way RetroArch does) with custom branding.

## Decision

**The project ships its own custom libretro frontend** for standalone desktop
and hardware distribution. This is not RetroArch — it is a separate program
using `libretro-common` (MIT-licensed) to host the libretro core.

**Desktop platform targets: Windows, macOS, and Linux.** If the standalone
frontend ships on Linux it must ship on Windows and macOS too — they are the
same codebase and SDL2 handles the platform differences. Primary development
is on macOS, so it is the first-class dev and test platform; Linux and
Windows are built and tested in CI. macOS distribution requires code signing
and notarization; Windows distribution is a zip or installer. All three
use the same SDL2 display/input/audio backend; no per-platform frontend
logic is expected.

**What the custom frontend provides:**
- Branded boot experience (startup chime, splash, console identity).
- Cart picker UI (browse local carts, launch with one action).
- Settings menu (display scaling, shader selection, input config).
- Pause menu with save state and rewind access.
- Platform-specific display handling (window, fullscreen, hardware panel).

**What it inherits from libretro infrastructure:**
- Shader support (GLSL/Slang shader chains, CRT/scanline/LCD shaders).
- Rewind (libretro's ring buffer state management).
- Save state management (serialize/unserialize patterns).
- Input abstraction (device API for gamepad/keyboard/touch mapping).
- Frame pacing (libretro timing infrastructure).

**Licensing:** `libretro-common` is MIT-licensed. The custom frontend is not
a RetroArch user; no GPL-3.0 obligation. Hardware distribution has no GPL
entanglement.

## Consequences

- Branded UX: players experience the console's identity, not RetroArch's.
- Libretro infrastructure (shaders, rewind, input abstraction) is inherited
  for free; no need to build these independently.
- Same libretro core code runs in RetroArch and in the custom frontend —
  no divergence.
- The custom frontend is the home for console-specific features (cart picker,
  LAN netplay UI) that RetroArch wouldn't implement. The achievement browser
  joins this set when the achievement system ships (ADR-0014).
- Estimated size: ~2000–5000 lines (mostly UI code).
- Browser frontend remains custom (libretro-in-WASM is less ergonomic than
  direct Emscripten compilation of a minimal frontend).
