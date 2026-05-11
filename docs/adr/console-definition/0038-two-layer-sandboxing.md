# ADR-0038: Two-layer sandboxing — RISC-V ABI and Lua environment

## Status
Accepted

## Context

Carts are third-party code; the runtime must protect the host system from
malicious or buggy carts. Sandboxing at a single layer (e.g., only at the
Lua level) would leave native carts unsandboxed. Sandboxing only at the
RISC-V level would leave Lua carts able to reach the host via Lua's standard
library (`os`, `io`, `require`, etc.) — unless the Lua environment is also
restricted.

## Decision

Two layers of sandboxing. The host enforces one; the VM enforces the other.

**Layer 1: RISC-V ABI sandbox (applies to all carts, enforced by the host).**

Every cart is a RISC-V ELF running in a controlled execution environment.
The cart can only affect the outside world through the console API. On
emulated platforms, the RISC-V interpreter bounds-checks memory accesses and
the console API is reached via ECALL; the ECALL dispatch table is the
complete host-level audit boundary. On native RISC-V hardware, the console
API is reached via direct function calls into `libblyt32.so` (see ADR-0024);
seccomp and namespace isolation enforce that no other host effects are
reachable from the cart process.

Carts cannot: access the host filesystem, open network sockets, read memory
outside allocated regions, execute privileged instructions, or escape via
side channels.

**Layer 2: Lua environment sandbox (applies to Lua carts, enforced inside
the VM by the Lua interpreter itself).**

Because the Lua interpreter runs inside the RISC-V sandbox (ADR-0025), the
host never sees Lua API calls directly — they are ordinary function calls
within the VM. The Lua environment is configured before cart code runs to
strip dangerous standard library access:

- **Removed:** `os`, `io`, `package`, `require`, `loadfile`, `dofile`,
  `debug` (mostly) — anything reaching the host filesystem or OS.
- **Kept:** `string`, `table`, `math` (with console's deterministic
  transcendentals), `coroutine`, basic globals.
- **Console-provided:** graphics, audio, input, state, resource loading,
  time, RNG, deterministic math — all via ECALL.

The `debug` library is not exposed, so authors cannot escape the sandbox
via `debug.getinfo` into C land.

**Combined effect:** a Lua cart is sandboxed at the RISC-V level (what the
cart's binary can do on the host) and at the Lua level (what the Lua script
can see within the VM). A native cart is sandboxed only at the RISC-V level,
which is sufficient. The RISC-V boundary is the single consistent host-level
security perimeter for all carts.

## Amendment (ADR-0115, ADR-0116, ADR-0118, 2026-05-11)

**Layer 1 on emulated platforms (ADR-0115):** The ECALL dispatch
bounds-check and the `mprotect`-enforced memory protection (no guest page
carries `PROT_EXEC` on interpreter deployments) are the two concrete
mechanisms that enforce Layer 1. ADR-0115 specifies both, along with the
EBREAK trap that is belt-and-braces for the static opcode scan.

**OS-level sandbox for standalone deployment (ADR-0116):** The description
of "Linux process isolation" in the Consequences section is now specified
as a two-phase seccomp filter (raw BPF, pending Spike R), user/mount/pid/
net namespace isolation, cgroups v2 cpu.max throttling, and rlimits. The
libretro deployment is explicitly documented as suitable for trusted and
curated content only; it does not claim OS-level hostile-input containment.

**Layer 2 scope for hybrid carts (ADR-0118):** Layer 2 (Lua environment
sandbox) applies as a security boundary only to the scripted Lua portions
of a cart. For hybrid carts, the native Rust code has access to Lua C API
symbols in the guest and can manipulate Lua VM state. Layer 2 is not a
security boundary for the native portions of a hybrid cart. Layer 1
(ECALL dispatch) remains the complete perimeter for all cart code
regardless of language.

## Consequences

- Carts can be distributed and run without trust in the author.
- On emulated platforms, the ECALL dispatch table is the complete host
  security boundary; auditing what carts can do means auditing the ECALL
  list. On native hardware, the equivalent boundary is `libblyt32.so`'s
  exported symbol set plus the seccomp allowlist.
- Lua sandboxing is now the VM's own responsibility. The host runtime has no
  Lua-specific security logic; it does not need to know whether a cart is
  implemented in Lua or native code.
- Lua authors lose access to `os.time`, `os.clock`, `io.open`, `require`,
  and `pcall` on dynamically loaded modules. Console equivalents are provided
  where needed.
- The Lua sandbox is enforced at VM configuration time (dangerous globals are
  simply not defined in the initial environment); it is not bypassable from
  script.
- On hardware, Linux process isolation supplements the RISC-V memory layout
  controls — belt-and-suspenders for the native execution path.
