# ADR-0031: Runtime as a library with multiple thin frontends

## Status
Accepted

## Context

The runtime must run on multiple platforms: desktop native app, browser (WASM),
libretro (for RetroArch distribution), and hardware (minimal Linux). The
question is how to factor the shared logic vs. the platform-specific surface.
A monolithic runtime with per-platform `#ifdef` blocks was considered and
rejected as a maintenance trap.

## Decision

The runtime is a **library** (`libfantasyconsole`) with a narrow public C API
(~15–20 functions). Multiple thin frontends (~300–500 lines each) wrap it for
specific platform contexts:

```
libfantasyconsole (core)
├── Cart loading (ELF) and parsing
├── RISC-V interpreter
├── Lua interpreter (runtime-owned)
├── Graphics (paletted framebuffer, palette application)
├── Audio (software mixer, format decoders)
├── Input (abstract button state)
├── State management and save/restore
├── Debug servers (DAP, GDB remote)
└── Public C API (~15–20 functions, frontend-agnostic)

Frontends (thin adapters):
├── SDL2 native app (desktop)
├── Libretro core (ships as .so/.dylib/.dll)
├── Emscripten/WASM + HTML (browser)
└── Hardware image (runtime as PID-1 under minimal Linux)
```

The C API is frontend-agnostic: frontends call `fc_init`, `fc_run_frame`,
`fc_get_framebuffer`, etc. No platform assumptions in the library.

## Consequences

- Libretro compatibility unlocks RetroArch's entire platform matrix (Switch
  homebrew, retro handhelds, macOS via OpenEmu, Android, iOS, desktop,
  browser, every Linux/BSD) plus netplay, save states, shaders, and recording
  — all for free.
- The same core code runs in RetroArch and in the custom libretro frontend
  (ADR-0032); no divergence between distribution paths.
- Frontends are small enough to understand and audit individually.
- New platforms (native iOS app, new SBC) require only a new thin adapter,
  not changes to the core.
- The public C API becomes the canonical integration contract; it must be
  stable, well-documented, and tested.
