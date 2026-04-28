# ADR-0024: Unified RISC-V ELF format for all carts

## Status
Accepted

## Context

A cart format must be chosen that works for both "native" carts (C/Rust/Zig)
and "Lua" carts, without requiring two separate loaders or two distribution
formats. Options included a custom container format, a ZIP-based archive, and
standard ELF with custom sections.

## Decision

**All carts are standard RISC-V ELF binaries** (RV32IMFC, little-endian,
statically linked), regardless of implementation language. One format, one
loader, one distribution story.

**ELF identity and verification.**
The standard ELF header provides layered cart verification before any section
is parsed:

- `e_ident[EI_MAG0..3]` = `\x7fELF` — standard ELF magic.
- `e_ident[EI_CLASS]` = `ELFCLASS32` — 32-bit.
- `e_ident[EI_DATA]` = `ELFDATA2LSB` — little-endian.
- `e_machine` = `EM_RISCV` (0xF3) — RISC-V ISA.
- `e_flags` = `EF_RISCV_RVC | EF_RISCV_FLOAT_ABI_SINGLE` — confirms the
  RV32IMFC profile; a runtime built for a different subset rejects here.
- `e_ident[EI_OSABI]` — set to a console-specific value to distinguish carts
  from arbitrary RISC-V ELF binaries. Value is name-pending (see
  `docs/pending-name.md`).

The runtime checks these fields in sequence; each is a fast rejection point
requiring no section parsing.

**ELF section conventions:**
- `.text`, `.data`, `.bss`, `.rodata` — standard code and data.
- `.cart.info` — frontend metadata (title, author, API version, size class,
  etc.), compiled to FlatBuffers by the packer (see ADR-0073).
- `.cart.config` — runtime configuration (state buffer schemas, voice groups,
  palette cycles, etc.), compiled to FlatBuffers by the packer (see ADR-0073).
- `.cart.lua` — Lua-specific configuration (bytecode version, etc.), compiled
  to FlatBuffers by the packer. Present only in Lua carts; read by
  `liblua.rv32` during VM initialisation. The host runtime does not parse
  this section (see ADR-0073).
- `.cart.resources` — directory of named resources (sprites, tilemaps, audio,
  Lua source, etc.) accessed by name through the resource API.

**Loading:**
- On hardware: mmap the cart file; runtime's own ELF loader parses program
  headers; resource sections are direct pointers into the mmap'd bytes
  (zero-copy). The cart is not exec()'d or dlopen()'d by the OS.
- In emulator: ELF loader parses program headers; cart code/data copied into
  RISC-V interpreter's guest memory; resource sections mapped to known guest
  addresses.

**Single cart file (`.cart`).** No separate resource archives or metadata
sidecars. File extension is name-pending (see `docs/pending-name.md`).

**Dev-mode directory container.**
In dev mode the runtime accepts a directory path in place of a cart file and
treats it as an alternative container with identical semantics. The packer's
internal pipeline always stages its output to this directory; releasing a cart
is the final bundling step that packs the directory into the ELF file. Watch
mode simply omits that step.

Directory layout (default: `<project>/.build/`, gitignored):

```
.build/
  cart.info          FlatBuffers binary — content of the .cart.info ELF section
  cart.config        FlatBuffers binary — content of the .cart.config ELF section
                     Contains the resource registry: for each resource, its
                     integer ID, type, and path relative to resources/.
  cart.bin           Compiled RISC-V code — native cart binary or the Lua shim
                     ELF (ADR-0025). The shim is identical across all Lua carts
                     and is cached by the packer; only rebuilt when the SDK
                     template changes.
  resources/
    lua/
      main.lua       Lua source files are normalised to resources/lua/
      entities.lua   regardless of their location under src/.
      main.luac      Release builds: luac bytecode replaces .lua source.
                     Debugger attaches to .lua files for line-accurate stepping.
    assets/
      fonts/
        ui.bpf       assets/fonts/ui.ttf  → resources/assets/fonts/ui.bpf
      sprites/
        hero.spr     assets/sprites/hero.png → resources/assets/sprites/hero.spr
      music/
        theme.xm     Tracker modules pass through unchanged.
      sfx/
        jump.qoa
    vendor/
      sprites/
        hud.spr      vendor/sprites/hud.png → resources/vendor/sprites/hud.spr
```

Non-Lua resources mirror their full path from the project root into
`resources/`. The cart.build.yaml may declare multiple resource roots
(e.g., `assets/`, `vendor/`); each root's path is preserved, so resources
from different roots cannot collide and their origin is unambiguous when
inspecting the staging directory.
```

**Resource lookup in directory mode.** Resource filenames do not encode the
integer ID — they mirror the source asset path. The cart.config resource
registry maps each integer ID to its path and type; the runtime builds an
ID→path table once at cart load. This is the same one-time cost as the ELF
case (which maps ID→offset within `.cart.resources`); the lookup overhead is
identical.

The presence of `cart.info` and `cart.config` (both with valid FlatBuffers
preambles) is sufficient for the runtime to identify a valid directory
container. The runtime's loader presents an identical interface to the rest of
the system regardless of whether the backing store is an ELF file or a
directory.

## Consequences

- ELF is battle-tested with mature tooling (binutils, objcopy, linker scripts)
  and debuggers (GDB, LLDB) understand ELF and DWARF natively.
- Resources in ELF sections enable zero-copy mmap on hardware — a real
  performance and simplicity win.
- Language extensibility is natural: any language targeting RV32IMFC ELF is
  a first-class cart format without special handling.
- One format simplifies the packer, the runtime loader, and authoring
  documentation.
- Lua carts are also ELF carts (see ADR-0025); the format distinction between
  "native" and "Lua" is an authoring distinction, not a format distinction.
- The `.cart.*` ELF section namespace is open-ended; frameworks can add
  their own resource types without runtime changes.
