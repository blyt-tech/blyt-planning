# ADR-0112: ELF load-time security checks

## Status

Accepted

## Context

ADR-0024 specifies the ELF identity checks (magic, class, endianness,
machine, e_flags, EI_OSABI) and the DT_NEEDED allowlist performed before
any cart section is parsed. Those checks are fast rejection points on the
ELF header; they do not cover the structural security properties of the
binary's full layout, the contents of its executable sections, or the
integrity of its custom sections (`.cart.info`, `.cart.config`,
`.cart.resources`, `.lua_exports`).

Because a cart binary may be hand-assembled by a hostile actor, no
metadata or layout property supplied by the binary can be taken on trust.
Every security-relevant fact must be derived and verified by the host.

## Decision

The following checks are performed by the runtime's loader on every cart
binary before any guest code executes. All checks apply equally to the
ELF file and to the directory-container dev-mode equivalent (where the
corresponding serialised bytes are read from the staging directory).

### Segment layout

- Reject any LOAD segment that has both `PF_W` and `PF_X` set. No
  writable-executable mapping under any circumstances.
- Reject any two LOAD segments whose file-offset ranges overlap.
- Reject any two LOAD segments whose virtual-address ranges overlap.
- Reject any LOAD segment whose `p_offset + p_filesz` would exceed the
  file size. All size and offset arithmetic uses overflow-checked
  unsigned arithmetic; integer overflow in any computation is treated as
  a rejection.
- Reject if `e_entry` does not fall within a LOAD segment that has `PF_X`
  set. The cart's entry point must be inside an executable region.
- Reject if a `PT_GNU_STACK` segment is present with `PF_X` set. Absence
  of `PT_GNU_STACK` is permitted (the runtime defaults to a non-executable
  stack).

### Dynamic section

The DT_NEEDED allowlist is specified in ADR-0024. Additionally:

- Reject if a `PT_INTERP` segment is present. The runtime is the loader;
  the OS dynamic linker must not be involved.

### Symbol import allowlist

Every entry in the cart's dynamic symbol table (`.dynsym`) that has
`STB_GLOBAL` binding and `SHN_UNDEF` section (i.e. an imported symbol)
must appear on an explicit runtime-maintained allowlist of permitted
symbols. The allowlist is the union of public exported symbols from the
DT_NEEDED-permitted libraries (`libblyt32.so`, `libblyt32lua.so`,
`libblytc.so`). Any cart that imports a symbol not on this list is
rejected at load time.

Internal symbols in runtime libraries — such as `blytc_arena_init` in
`libblytc.so` — are marked `visibility("hidden")` and are absent from
those libraries' `.dynsym` tables. They therefore cannot appear on the
allowlist and cannot be imported by a cart.

This check applies on both loading paths: the custom loader inspects the
cart's `.dynsym` directly; the trusted native-exec path enforces it at
pack time (the packer validates the cart ELF before it is installed) and
the custom loader enforces it again at runtime.

### RELRO and BIND_NOW

- Reject if `PT_GNU_RELRO` is absent. All cart binaries must be linked
  with `-z relro -z now`. The packer enforces this at pack time.
- After all PLT/GOT relocations are resolved at load time (eagerly, per
  BIND_NOW semantics), the GOT pages are remapped read-only via
  `mprotect(PROT_READ)` before the cart entry point is called. This
  prevents runtime GOT modification.
- On the trusted native-exec path, ld.so handles RELRO+BIND_NOW via the
  standard mechanism. On the custom-loader path, the runtime performs the
  final `mprotect` after resolving all relocations.

### Opcode scan

Walk every byte of every LOAD segment with `PF_X` set, checking at both
4-byte-aligned and 2-byte-aligned offsets to cover RVC 16-bit encodings
as well as 32-bit instructions:

- Reject if the byte pattern `0x73 0x00 0x00 0x00` (`ecall`) appears at
  any 4-byte-aligned position.
- Reject if the byte pattern `0x73 0x00 0x10 0x00` (`ebreak`) appears at
  any 4-byte-aligned position.
- Reject if either pattern appears at any 2-byte-aligned position (a
  compressed instruction could be followed by these bytes which, when
  decoded as a 32-bit instruction, would match).
- Reject any instruction encoding that is reserved or illegal under the
  supported RV32IMAFC subset.

False positives — data bytes that happen to match these patterns — are
acceptable. A cart that is rejected for this reason must be rebuilt.
False negatives are not acceptable.

The rationale: on emulated platforms, `ecall` is the internal mechanism
of the runtime-provided `libblyt32.so`; it must not appear in cart code.
On native RISC-V, cart code calls into `libblyt32.so` via the PLT; `ecall`
in cart code would reach the Linux kernel's syscall interface, bypassing
the console API boundary entirely. The static check is belt-and-braces
alongside the runtime's ecall trap (ADR-0115).

### `.cart.info` and `.cart.config` sections (FlatBuffers)

These sections contain FlatBuffers-encoded metadata. The parser must:

- Verify the FlatBuffers root offset is within section bounds before any
  field access.
- Bounds-check every table offset, vector length, and string length read
  from the binary.
- Cap maximum vector element count at a reasonable limit to prevent
  degenerate inputs from causing unbounded work.
- Reject if `api_version` in `.cart.info` is outside the runtime's
  supported range (from the runtime's compiled-in minimum to the
  runtime's own API version).

### `.cart.resources` section

- Bounds-check every resource entry's `offset` and `size` field against
  the section bounds, with overflow-safe arithmetic.
- Reject any resource entry where `offset + size` would exceed the
  section length.
- Reject any two resource entries whose byte ranges overlap within the
  section.
- Reject duplicate resource IDs.
- Cap the maximum resource count.

### `.lua_exports` section (WASM target)

On the WASM target the host reads this section at load time to register
Lua–Rust trampoline functions. On rv32emu targets the section is present
in the ELF but the runtime uses the `cart_lua_modules` path instead;
the section is still structurally validated.

- Bounds-check the entire section length before iterating entries.
  Each entry is a fixed-size record; reject if section length is not a
  multiple of the entry size.
- Cap the maximum entry count.
- For each entry's `sym_addr` field: reject if zero, if outside the
  cart's guest address space, if not within a LOAD segment that has
  `PF_X` set, or if equal to the sentinel address (`FC32_SENTINEL_ADDR`
  from the memory-map ADR).
- For each string field (module name, function name): the stored offset
  must be within the section; the string must be null-terminated within
  the section bounds; length must not exceed 256 bytes; characters must
  be printable ASCII (0x20–0x7E plus 0x00 terminator).
- Each type-signature field value must be within the defined type enum;
  unknown values are rejected.
- Reject duplicate `(module_name, function_name)` pairs.

## Amendment (ADR-0119, 2026-05-12)

The PT_INTERP check is now path-conditional (see ADR-0119):

**Custom-loader path (untrusted, and all emulated targets):** PT_INTERP
must be absent, as specified above. The runtime is the loader; the OS
dynamic linker must not be involved.

**Trusted native-exec path (pre-installed hardware carts):** PT_INTERP
must be present and must name the platform's expected ILP32 dynamic
linker path. A cart with PT_INTERP absent, or pointing to an unexpected
interpreter path, is rejected on this path.

All other checks in this ADR (segment layout, opcode scan, FlatBuffers
validation, resource bounds, `.lua_exports`) apply equally on both paths.
On the trusted path they are performed file-based by the launcher before
exec; the TOCTOU risk is accepted given the hardware threat model (trusted
= pre-installed on device).

## Amendment (issue #71, 2026-06-16)

The section name check is inverted from an allowlist to a **denylist**.

**Rationale:** an allowlist requires `cart_load.c` to enumerate every section
name a new compiler or language toolchain may emit, coupling the validator to
language-toolchain details. The genuinely dangerous sections — constructor and
destructor tables that execute at load time, bypassing the cart lifecycle — are
a small, stable set. A denylist is a cleaner invariant and makes the loader
language-agnostic.

**Denied sections (exact match):**
- `.init_array`
- `.fini_array`
- `.preinit_array`
- `.ctors`
- `.dtors`

A cart containing any of these section names is rejected with
`BLYT_CART_ERR_DENIED_SECT`. All other unknown sections are passed through
without validation — language-specific metadata sections (e.g. `.swift_modhash`,
`.llvm_addrsig`) are harmless, and stripping them at finalise time is the
language toolchain's responsibility.

The `.cart.*` namespace remains controlled by blyt tooling; unknown `.cart.*`
sections from newer tooling are treated as forward-compatible content and are
not denied.

The `DT_NEEDED` allowlist and symbol import allowlist are unaffected by this
amendment.

## Consequences

- The loader's rejection surface is well-defined and auditable. A
  hostile ELF that passes all checks is structurally constrained to the
  layout the runtime expects.
- The opcode scan rejects binaries with `ecall`/`ebreak` in cart code.
  Combined with the runtime's ecall trap (ADR-0115) this is belt-and-
  braces: the static check catches the binary before load; the runtime
  trap catches any scanner bug at run time.
- `.lua_exports` validation ensures that `sym_addr` values fed to
  `rv32emu_call_fn` on the WASM target are valid guest addresses. An
  adversarial `sym_addr` pointing to data (not code) still stays within
  the guest sandbox; this check prevents out-of-bounds values entirely.
- The checks add overhead only to the load path, not to the run-time
  hot path. Load-time cost is proportional to cart size, which is bounded
  by the cart's declared size class (ADR-0030).
- FlatBuffers parser hardening and resource-bundle bounds-checking
  eliminate a class of host-crash bugs that hostile carts could otherwise
  trigger before any guest code runs.
