# ADR-0115: Memory protection, W^X, and ecall trap

## Status

Accepted

## Context

The interpreter model provides memory-safety guarantees that a JIT does
not, but the interpreter's memory-access paths still require the host OS
to enforce appropriate permissions on the guest's mapped regions. Without
this, a cart could write to its own code pages and self-modify in ways
that bypass the static opcode scan (ADR-0112). An `ecall` trap is also
needed as belt-and-braces alongside the static scan.

These measures apply to all deployment targets (standalone runner,
libretro core, WASM container) wherever the underlying OS supports them.

## Decision

### Memory protection — interpreter deployments

On emulated platforms (rv32emu running on x86, ARM64, WASM), the host CPU
reads guest code pages as **data** (to decode instructions) and never
executes them as native instructions. Guest code pages therefore do not
need to be executable from the host OS's perspective. After the loader
maps each LOAD segment:

- LOAD segments with `PF_X` set (cart code): `mprotect` to
  `PROT_READ` only — readable, not writable, **not executable**.
- LOAD segments with `PF_W` set (cart data): `mprotect` to
  `PROT_READ | PROT_WRITE` — readable and writable, not executable.
- No guest page receives `PROT_EXEC` on emulated platforms.

This is strictly more restrictive than plain W^X: the host CPU cannot
execute any guest memory region, regardless of its ELF flags. A cart
that exploits a hypothetical JIT miscompilation, or any mechanism that
tries to make the host CPU execute guest code, will fault at the OS level.

### Memory protection — native RISC-V hardware deployment

On native RISC-V hardware (K230D etc.) the host CPU executes the
cart binary directly. The `PF_X` ELF flag controls which pages receive
execute permission:

- LOAD segments with `PF_X` set: `mprotect` to `PROT_READ | PROT_EXEC`.
- LOAD segments with `PF_W` set: `mprotect` to `PROT_READ | PROT_WRITE`.
- No page receives both `PROT_WRITE` and `PROT_EXEC`. The loader already
  rejects any segment with both flags set (ADR-0112); this `mprotect` step
  enforces the same invariant at the OS level (classic W^X).

The loader already rejects any segment with both `PF_W` and `PF_X` set
(ADR-0112). The `mprotect` step is belt-and-braces enforcement at the OS
level, independent of the static check.

**Platform notes for native builds:**
- Linux / other POSIX: `mprotect`.
- macOS Hardened Runtime: W^X is enforced by default; no additional call
  required for an interpreter-only build.
- Windows (libretro deployment): `VirtualProtect`. Optionally set
  `PROCESS_MITIGATION_DYNAMIC_CODE_POLICY` with `ProhibitDynamicCode = 1`
  for the worker thread.

### ECALL dispatch bounds-check

The ECALL dispatch table is indexed by the value in `a7`. Before
dispatching:

1. Range-check `a7`: `1 <= a7 < ECALL_TABLE_SIZE`. Any value outside
   this range → log and terminate the guest.
2. Look up the handler pointer. A null handler (a reserved slot)
   produces the same outcome as an out-of-range number.

This is the primary enforcement of the ECALL surface as the host-level
security boundary described in ADR-0038.

### EBREAK trap

The interpreter's `ebreak` handler unconditionally logs the event and
terminates the guest. It does not single-step, does not invoke a debugger,
and does not resume execution. This is belt-and-braces against any bug in
the static opcode scanner (ADR-0112). The cost is zero for well-formed
binaries that the static check already rejects.

In dev mode, the debugger (Spike J) intercepts `ebreak` via the GDB stub
hook *before* this handler runs. The production EBREAK trap is the fallback
for non-debug deployments.

### ecall/ebreak in call-on-demand mode

The `rv32emu_call_fn` call-on-demand mechanism (ADR-0111) runs the
interpreter until ECALL `0xDEAD` (57005) fires. During bounded execution:

- Non-sentinel ECALLs are dispatched through the normal ECALL dispatch
  table, subject to all the same bounds-checking and argument validation.
- An `ebreak` in guest code invokes the same EBREAK trap and terminates
  the guest.
- A step limit `FC32_CALL_ON_DEMAND_STEP_LIMIT` is enforced: if the
  call-on-demand execution runs that many interpreter cycles without
  emitting ECALL `0xDEAD`, the guest is terminated. This prevents an
  adversarial or buggy Rust function from hanging the WASM host.

## Consequences

- On emulated platforms, no guest page is ever executable from the host
  CPU's perspective. Any host-side code-execution exploit via guest memory
  is impossible by construction.
- On native hardware, the W^X invariant closes the self-modification
  bypass class at the OS level, independent of whether the static check
  succeeded.
- The ECALL dispatch bounds-check makes the host-level security surface
  exactly enumerable: every legal host effect maps to exactly one entry
  in the dispatch table.
- The EBREAK trap is zero-cost in production (well-formed binaries never
  reach it after the static check). In dev mode the debugger takes
  precedence.
- The call-on-demand step limit prevents an infinite-loop Rust function
  from hanging the WASM host.
