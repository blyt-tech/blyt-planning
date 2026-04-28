# ADR-0044: CLI packer and VS Code dev loop

## Status
Accepted

## Context

Cart authoring needs a build toolchain and a rapid iteration loop. Options
ranged from an in-console editor (PICO-8 style — integrated, zero setup, low
ceiling) to a fully external "bring your own tools" approach. The console's
positioning (more capable than PICO-8, external tools for no ceiling) makes
the in-console editor incompatible with the design goals.

## Decision

**CLI packer:** `console pack ./myproject` produces `myproject.cart`. The
packer always stages its output to `myproject/.build/` first (see ADR-0024
for the directory layout), then bundles that directory into the ELF cart file.
`console run myproject.cart` for local testing; `console run ./myproject`
runs directly from the staging directory without packing.

**Watch mode:** `console watch ./myproject` runs the incremental build loop:
on any source or asset change, the packer reprocesses only the affected files
into `.build/`, then signals the runtime via the DAP connection to hot-reload
from the directory (ADR-0045). The ELF bundling step is skipped entirely
during watch mode. Compression is also skipped in the staging directory —
resources are written uncompressed for rebuild speed; the runtime reads them
directly.

**VS Code dev loop:** Extension or task-based setup that runs the packer on
save and reloads a webview panel containing the browser build of the runtime.
Hot reload for Lua carts (and optionally native carts via rebuild). Target
dev loop iteration time: <1 second for Lua/asset changes, <4 seconds for
native code changes.

**Debug connection:** in dev mode the runtime opens a TCP socket and acts as
a byte-pipe passthrough between VS Code (DAP client) and the in-VM DAP server
running inside the cart (ADR-0025). The runtime is agnostic to the DAP
protocol; `console watch` signals hot reload via this same connection using
a custom DAP command.

**`console new myproject`** creates a project with:
- Pre-configured manifest, VS Code workspace, recommended extensions.
- Starter Lua or native-cart code with inline comments explaining the API.
- Example sprite sheet, tilemap, tracker module in correct formats — a
  runnable "hello world" game the author modifies.
- Readme linking to recommended tools and quick-start guides.

**Asset pipeline:** the packer accepts common formats without conversion:
PNG (auto-palette-quantized), Aseprite `.ase`/`.aseprite` files, Tiled TMX
and JSON, LDtk native format, XM/IT/MOD/S3M tracker formats, WAV/OGG/MP3
for audio (converted to QOA/ADPCM internally), BMFont FNT.

## Consequences

- Authors work in their existing tools (Aseprite, Tiled, VS Code, OpenMPT)
  without conversion friction.
- The packer is the integration point for all asset formats; adding format
  support is cheaper than making authors convert.
- The VS Code extension is the primary authoring environment; its extension
  architecture should accommodate genre-specific framework extensions (v2+).
- `console new` turns "start from blank files" into "start from a working
  game and modify," significantly reducing initial friction.
- The dev loop targets sub-second rebuild for the common case (Lua source
  edit, asset change); native code rebuild is acceptably slower.
- Hot reload (ADR-0045) is the mechanism underlying the VS Code reload step.
