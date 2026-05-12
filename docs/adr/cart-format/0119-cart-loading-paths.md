# ADR-0119: Cart loading paths — trusted native exec and untrusted custom loader

## Status

Accepted

## Context

ADR-0024 described a single loading model: the runtime's own ELF loader
maps the cart binary, maps runtime libraries, resolves PLT/GOT, and jumps
to the cart entry point. The OS dynamic linker is not involved; PT_INTERP
is rejected.

This model works correctly for all emulated targets (rv32emu on desktop,
Pi, and WASM) and for untrusted hardware carts. It was chosen partly
because EI_OSABI=0x42 in the cart ELF would cause the Linux kernel to
reject a direct exec, and partly for pre-execution validation control.

Two changes open a viable alternative for trusted hardware carts:

1. EI_OSABI is changed to `ELFOSABI_NONE` (0) in this ADR, removing the
   kernel exec rejection.
2. Pre-exec file-based validation (ADR-0112 checks performed before exec
   rather than on a mmap'd image) is sufficient for the hardware threat
   model, where trust means pre-installed on the device.

A native-exec path is desirable for hardware: it uses the OS dynamic
linker, allows the full ILP32 compat ABI, and enables the original
arch-dispatch seccomp design (launcher = RISCV64, cart process = RISCV32)
that was not testable on the Fedora 42 QEMU test kernel (Spike R Finding 1).

## Decision

### Two loading paths

**Custom-loader path (untrusted)** — used for all emulated targets and
for non-pre-installed carts on hardware:

- The runtime's own ELF loader maps the cart's LOAD segments and all
  runtime libraries (libblyt32.so, libblytc.so if present,
  libblyt32lua.so if present) into the execution environment.
- On rv32emu targets this means the rv32emu guest address space; on
  hardware it means the launcher process's address space.
- PLT/GOT entries are resolved before execution begins.
- PT_INTERP must be absent (rejected by ADR-0112).
- Seccomp is installed by the launcher after all linking is complete,
  before jumping to the cart entry point. No ld.so syscalls need to be
  permitted.

**Trusted native-exec path** — hardware only, pre-installed carts:

- The launcher validates the cart file against all ADR-0112 checks
  (file-based, pre-exec).
- The launcher sets LD_LIBRARY_PATH to the runtime library directory and
  installs a phase-1 seccomp filter permissive enough for ld.so startup
  (openat, read, mmap, mprotect, fstat, faccessat, close, and related
  syscalls).
- The launcher exec's the cart binary. The OS loads it natively as an
  ILP32 process; ld.so resolves libblyt32.so, libblytc.so, and
  libblyt32lua.so.
- libblyt32.so's constructor installs a phase-2 seccomp filter that
  blocks execve and other post-exec sensitive syscalls. LIFO semantics
  (confirmed by Spike R) ensure phase-2 KILL rules take precedence over
  phase-1 ALLOW rules.
- The cart entry point runs under the combined phase-1 + phase-2 filter.
- PT_INTERP must be present and must name the platform's expected ILP32
  dynamic linker path (validated by launcher before exec).
- `seccomp_data.arch = AUDIT_ARCH_RISCV32` for the cart process, enabling
  arch-keyed allowlists: RISCV64 rules for the launcher, RISCV32 rules
  for the cart.

### Trust definition

**Trusted = pre-installed on the device image.** For the hardware
buildroot image (ADR-0035), carts installed as part of the image build
are trusted; dynamically loaded or user-sideloaded carts are untrusted.
Cryptographic signature verification is deferred; it can be layered onto
the trusted path without architectural changes.

### EI_OSABI change

Cart ELF binaries use `EI_OSABI = ELFOSABI_NONE` (0). Cart identity is
established entirely by the `.cart.info` section (FlatBuffers), not by
the OSABI field. This supersedes ADR-0024's `0x42` ('B') convention.
Existing toolchain output and checks must be updated accordingly.

## Consequences

- The trusted native-exec path on hardware enables the arch-dispatch
  seccomp design from the original plan: the cart process genuinely
  reports `AUDIT_ARCH_RISCV32`, so the phase-2 filter can be
  RISCV32-specific without touching the launcher's RISCV64 filter.
- The production ILP32 native syscall set (for the RISCV32 phase-2
  filter) has not yet been characterised. This requires a spike on a
  kernel with the RISC-V ILP32 ELF binary loader enabled — the Fedora 42
  QEMU test kernel (Spike R) lacked this. A follow-on spike (Spike S) is
  needed.
- The custom-loader path is unchanged for all emulated targets and is the
  fallback for untrusted hardware carts. Spike R's rv32emu LP64 host
  syscall allowlist (`seccomp_allowlist.h`, 23 entries) correctly covers
  the emulated target path.
- Removing EI_OSABI=0x42 requires toolchain and packer updates.
- The TOCTOU race between pre-exec validation and exec is accepted for
  the trusted path given the hardware threat model (pre-installed = device
  owner controls the filesystem).
