# ADR-0024: Unified RISC-V ELF format for all carts

## Status
Accepted (revised — "statically linked" superseded by the controlled dynamic
linking model described below)

## Context

A cart format must be chosen that works for both "native" carts (C/Rust/Zig)
and "Lua" carts, without requiring two separate loaders or two distribution
formats. Options included a custom container format, a ZIP-based archive, and
standard ELF with custom sections.

The original decision specified statically linked binaries. This was revised
because static linking has no clean solution for native RISC-V execution: on
real RISC-V hardware the `ecall` instruction is a Linux syscall, not a
console API call, so the ECALL-based API convention (ADR-0085) cannot be used
directly from cart code. The options for bridging this — IPC ring buffer,
load-time binary patching, custom indirect-call table — all amount to
re-implementing dynamic linking in a non-standard way. Standard dynamic
linking achieves the same result with no custom mechanism, integrates
naturally with debuggers and profilers, and handles versioning via
well-understood ELF symbol versioning. A single controlled dynamic dependency
on `libblyt32.so` captures the necessary change without opening carts to
arbitrary dynamic linking.

## Decision

**All carts are standard RISC-V ELF binaries** (RV32IMAFC, little-endian),
regardless of implementation language. One format, one loader, one
distribution story.

**Dynamic linking — one controlled dependency, variant-specific.**
Carts declare a dynamic dependency on exactly one runtime-provided variant
library matching the cart's declared console (manifest field `console:`
in `.cart.info`, defaulting to `blyt32`; see ADR-0105):

- Blyt32 carts depend on `libblyt32.so` (default; `console:` may be
  omitted).
- BlyTTY carts (planned) depend on `libblytty.so` (`console: blytty`).
- Blyt3D carts (far future) depend on `libblyt3d.so` (`console: blyt3d`).

Lua carts additionally depend on the variant's Lua library
(`libblyt32lua.so`, `libblyttylua.so`, `libblyt3dlua.so`), also
runtime-provided (see ADR-0025). All other code — game logic, stage SDK,
language shims, user libraries — is statically linked into the cart binary.
The packer validates that the `DT_NEEDED` entries match exactly the
console declared in `.cart.info` (or the `blyt32` default if absent); a
cart with an unexpected dynamic dependency, or with a mismatch between
manifest console and `DT_NEEDED`, is rejected at pack time and at load
time.

The variant library is not shipped with the cart. It is provided by the
runtime environment (the `blyt` runner, libretro core, or hardware image),
which can host any variant. The runtime selects the appropriate variant
library at load time based on `.cart.info`. The library behaves differently
depending on execution context:

- **On emulated platforms:** the variant library (e.g. `libblyt32.so`) is an
  RV32IMAFC shared library pre-mapped into the cart's guest address space by
  the emulator before execution begins. Its function bodies are thin stubs
  that issue ECALLs to the emulator's internal dispatch table (see ADR-0085).
  PLT/GOT entries for console API symbols are resolved in guest space before
  the cart entry point is called. This is the same dynamic-loader path
  established by Spike C (which validated pre-mapping a RV32IMAFC shared
  library into guest memory).
- **On native RISC-V hardware:** the variant library is a real implementation
  library mapped into the cart process by the runtime before execution
  begins. Console API calls are direct function calls into the library with
  no IPC overhead.

Cart code calls console API functions identically on both paths. The
two-mode behaviour of the variant library is the runtime's concern, not the
cart's. A variant library may internally re-export symbols from a private
shared library (`libblytcommon.so`-class) holding logic common to all
variants — this is an implementation detail; carts see only the variant
library's exported surface.

**ELF identity and verification.**
The standard ELF header provides layered cart verification before any section
is parsed:

- `e_ident[EI_MAG0..3]` = `\x7fELF` — standard ELF magic.
- `e_ident[EI_CLASS]` = `ELFCLASS32` — 32-bit.
- `e_ident[EI_DATA]` = `ELFDATA2LSB` — little-endian.
- `e_machine` = `EM_RISCV` (0xF3) — RISC-V ISA.
- `e_flags` = `EF_RISCV_RVC | EF_RISCV_FLOAT_ABI_SINGLE` — confirms the
  RV32IMAFC profile; a runtime built for a different subset rejects here.
- `e_ident[EI_OSABI]` — set to `0x42` ('B' for Blyt) to distinguish carts
  from arbitrary RISC-V ELF binaries. Value sits in the OS-specific range
  (64–127). If the project later registers a formal value with the RISC-V
  community, the formal value supersedes (see ADR-0105).

The runtime checks these fields in sequence; each is a fast rejection point
requiring no section parsing. Dynamic section validation (permitted
`DT_NEEDED` entries only) follows immediately after.

**ELF section conventions:**
- `.text`, `.data`, `.bss`, `.rodata` — standard code and data.
- `.dynamic`, `.dynsym`, `.plt`, `.got` — standard dynamic linking sections,
  present in all cart binaries. The only external symbols resolved at load
  time are those imported from the variant library (e.g. `libblyt32.so`)
  and, for Lua carts, the variant Lua library (e.g. `libblyt32lua.so`).
- `.cart.info` — frontend metadata (title, author, API version, size class,
  etc.), compiled to FlatBuffers by the packer (see ADR-0073).
- `.cart.config` — runtime configuration (state buffer schemas, voice groups,
  palette cycles, etc.), compiled to FlatBuffers by the packer (see ADR-0073).
- `.cart.lua` — Lua-specific configuration (bytecode version, etc.), compiled
  to FlatBuffers by the packer. Present only in Lua carts; read by the
  variant Lua library (e.g. `libblyt32lua.so`) during VM initialisation. The
  host runtime does not parse this section (see ADR-0073).
- `.cart.resources` — directory of named resources (sprites, tilemaps, audio,
  Lua source, etc.) accessed by name through the resource API.

**Loading:**
- On hardware: runtime's own ELF loader parses program headers; reads the
  declared variant from `.cart.info`; maps the matching variant library
  (and variant Lua library for Lua carts) into the cart process; resolves
  PLT/GOT entries; then begins execution. Resource sections are direct
  pointers into the mmap'd cart bytes (zero-copy). The cart is not exec()'d
  or dlopen()'d by the OS — the runtime controls the load environment,
  including applying seccomp and namespace isolation before jumping to the
  cart entry point.
- In emulator: emulator's dynamic loader (approach established by Spike C)
  maps the cart into guest memory; pre-maps the emulator-side variant
  library (and variant Lua library for Lua carts) at fixed guest addresses;
  resolves PLT/GOT entries in guest space before execution begins.

**Single cart file (`.blyt`).** No separate resource archives or metadata
sidecars. Variant libraries (e.g. `libblyt32.so`, `libblyt32lua.so`) are
platform infrastructure, not cart files; they are never bundled with the
cart. The `.blyt` extension is shared across all consoles; the manifest
field `console:` in `.cart.info` discriminates which library set a cart
links to (defaulting to `blyt32` if absent — see ADR-0105). Non-interactive
carts (per ADR-0031's `interactive: false`) may use the placeholder suffix
`.blyt.demo` as a file-manager affordance; frontends filter on the
manifest field, not the suffix.

**Dev-mode directory container.**
In dev mode the runtime accepts a directory path in place of a cart file and
treats it as an alternative container with identical semantics. The packer's
internal pipeline always stages its output to this directory; releasing a cart
is the final bundling step that packs the directory into the ELF file. Watch
mode simply omits that step.

Directory layout (default: `<project>/build/`, gitignored by the
`.gitignore` that `blytbuild new` writes — see ADR-0044):

```
build/
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

## Amendment (ADR-0112, 2026-05-11)

ADR-0112 specifies the complete set of structural security checks
performed at load time, extending the fast-rejection identity checks
described here. ADR-0112 covers: segment layout (no W+X, no overlap, no
EOF overrun, entry-point validation), opcode scanning for `ecall` /
`ebreak` and reserved encodings in all executable segments, FlatBuffers
parser hardening for `.cart.info` and `.cart.config`, resource bundle
bounds-checking, and `.lua_exports` section validation. The checks in
ADR-0112 are performed immediately after the identity and DT_NEEDED
checks described above.

## Amendment (ADR-0119, ADR-0120, 2026-05-12)

**EI_OSABI:** Changed from `0x42` to `ELFOSABI_NONE` (0). The Linux
kernel ELF loader may reject non-standard OSABI values at exec time,
which would block the trusted native-exec path introduced in ADR-0119.
Cart variant continues to be determined from the `.cart.info` `console:`
field; EI_OSABI no longer carries identity information.

**DT_NEEDED allowlist:** Extended to include `libblytc.so` as an
optional entry (see ADR-0120). Valid DT_NEEDED sets are now:
`{libblyt32.so}`, `{libblyt32.so, libblyt32lua.so}`,
`{libblyt32.so, libblytc.so}`,
`{libblyt32.so, libblyt32lua.so, libblytc.so}`.

**PT_INTERP and loading paths:** PT_INTERP is no longer universally
rejected. ADR-0119 introduces two cart loading paths; which applies
determines whether PT_INTERP is absent or required. See ADR-0112
amendment and ADR-0119 for the path-conditional rules.

**Loading on hardware:** ADR-0119 describes the trusted native-exec path
(pre-installed carts exec'd directly by the OS) alongside the existing
custom-loader path. The loading section above describes the custom-loader
path; ADR-0119 adds the trusted path.

## Consequences

- ELF is battle-tested with mature tooling (binutils, objcopy, linker scripts)
  and debuggers (GDB, LLDB) understand ELF and DWARF natively.
- Console API calls are visible to standard debuggers and profilers as named
  function symbols in `libblyt32.so`, not as opaque ECALL numbers.
- Resources in ELF sections enable zero-copy mmap on hardware — a real
  performance and simplicity win.
- Language extensibility is natural: any language targeting RV32IMAFC ELF is
  a first-class cart format without special handling.
- One format simplifies the packer, the runtime loader, and authoring
  documentation.
- Lua carts are also ELF carts (see ADR-0025); the format distinction between
  "native" and "Lua" is an authoring distinction, not a format distinction.
- The `.blyt.*` ELF section namespace is open-ended; frameworks can add
  their own resource types without runtime changes.
- Native RISC-V execution no longer requires an IPC ring buffer. The
  ring-buffer design proposed in Spike H (Stage 2) is superseded; direct
  calls into `libblyt32.so` eliminate per-call IPC overhead entirely. Spike H
  Stages 1, 3, and 4 (RV32 on RV64 kernel, OS-level isolation, cgroups CPU
  quota) remain relevant.
- ADR-0085's ECALL convention is now the internal mechanism used by the
  emulator-side `libblyt32.so`, not the cart-facing ABI. The cart-facing ABI
  is the C function call convention of `libblyt32.so`'s symbols. ADR-0085
  requires an annotation noting this scope change.
- ADR-0038 Layer 1's description of ECALL as the sole host-effects boundary
  applies only to emulated platforms. On native hardware, `libblyt32.so`
  function calls are the API boundary; seccomp enforces that no other host
  effects are reachable. ADR-0038 requires an annotation.
