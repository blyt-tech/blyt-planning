# Console Definition and Constraints

What the console *is*: its ISA, hardware targets, display and audio
specifications, numeric model, runtime rules, size classes, and the
constraints that make features like save states, rewind, and netplay
work as structural properties of the system rather than bolt-ons.

| # | Decision |
|---|----------|
| [0001](0001-risc-v-isa-rv32imfc.md) | ISA choice — RV32IMFC, little-endian, no D extension |
| [0002](0002-reference-hardware-milk-v-duo.md) | Reference hardware — Milk-V Duo class SBCs |
| [0003](0003-display-320x240-paletted-256-colors.md) | Display — 320×240 paletted, 256 colors, double-buffered |
| [0004](0004-audio-format-tiers.md) | Audio — format tiers by cart class |
| [0005](0005-32-bit-numeric-model.md) | Numeric model — i32 and f32 everywhere |
| [0006](0006-audio-is-playing-queries-mixer-state.md) | Audio is_playing queries actual mixer state (bounded determinism exception) |
| [0007](0007-structural-determinism.md) | Full structural determinism |
| [0008](0008-api-based-memory-model.md) | Memory model — API-based, not memory-mapped |
| [0017](0017-input-spec-dpad-4face-2shoulders-4players.md) | Input spec — D-pad + 4 face + 2 shoulders + Start/Select, up to 4 players |
| [0020](0020-netplay-via-libretro.md) | Netplay via libretro infrastructure |
| [0021](0021-lan-only-netplay-in-custom-frontend-v1.md) | LAN-only netplay in the custom frontend for v1 |
| [0022](0022-netplay-join-during-lobby-fixed-player-count.md) | Netplay — join during lobby phase; player count fixed once session starts |
| [0023](0023-netplay-sessions-start-fresh.md) | Netplay sessions start fresh |
| [0030](0030-size-class-cart-caps.md) | Size-class-based cart caps (Demo / Mini / Standard / Large / Flagship) |
| [0031](0031-interactivity-orthogonal-to-size-class.md) | Interactivity is orthogonal to size class |
| [0032](0032-major-minor-api-versioning.md) | Major.minor API versioning |
| [0037](0037-fixed-timestep-main-loop.md) | Fixed-timestep main loop with accumulator |
| [0038](0038-two-layer-sandboxing.md) | Two-layer sandboxing — RISC-V ABI and Lua environment |
| [0076](0076-draw-is-read-only.md) | draw() is read-only over tracked state |
| [0078](0078-frozen-major-version-api-layers.md) | Frozen major-version API layers — backwards-compatible runtime subsystem |
| [0081](0081-text-mode-deferred.md) | Text mode — 80×30 character grid at 640×480 (deferred, v1.5+) |
| [0082](0082-emulator-mips-cap.md) | Emulator MIPS cap fixed to minimum emulation host throughput |
| [0083](0083-lua-cart-crash-diagnostics.md) | Lua cart crash diagnostics — runtime-owned dump |
| [0105](0105-project-naming-and-variants.md) | Project naming (Blyt) and console variants (Blyt32 / BlyTTY / Blyt3D) |
