# Spike R Results

**Question:** Can a hand-written raw BPF seccomp filter correctly implement
two-phase enforcement for a fork/exec launcher, where the launcher is LP64
(`AUDIT_ARCH_RISCV64`) and the cart process after exec is ILP32
(`AUDIT_ARCH_RISCV32`), given that `libseccomp` has no `SCMP_ARCH_RISCV32`
constant?

**Environment:** Fedora 42 RISC-V Cloud (kernel 6.16.4-200.0.riscv64.fc42),
running under QEMU 11.0.0 (`qemu-system-riscv64`, Apple Silicon host).
Date: 2026-05-11.

---

## Summary

**Partially answered.** The raw BPF filter mechanics (filter construction,
LIFO multi-filter semantics, libseccomp inadequacy) are validated.  However,
the core production path — launcher execs an ILP32 cart directly under the
kernel's native compat layer — was **not testable** on this kernel because
Fedora 42's kernel lacks the RISC-V ILP32 ELF binary loader.  Stages 2–4
were completed against a fallback workaround (rv32emu as the cart runner)
that does not represent the production architecture.

---

## Per-stage results

| Stage | Result | Notes |
|-------|--------|-------|
| 0 — environment | **PASS** | seccomp(2) present; CONFIG_COMPAT present but **no ILP32 ELF loader** |
| 1 — raw BPF filter | **PASS** | Filter mechanics: write allowed, socket blocked, LIFO confirmed |
| 2 — launcher integration | **PASS (workaround)** | Tested against rv32emu, not native ILP32 exec |
| 3 — rv32emu strace allowlist | **PASS (workaround)** | rv32emu LP64 host syscalls — not the production ILP32 native syscall set |
| 4 — adversary re-verification | **PASS (workaround)** | Valid for rv32emu path; native ILP32 path untested |

---

## Key findings

### Finding 1: Fedora 42 kernel lacks the RISC-V ILP32 ELF binary loader

The Fedora 42 kernel (6.16.4) compiles `compat_sys_*` functions (visible in
`/proc/kallsyms`) but does **not** include the ELF32 binary loader for
RISC-V ILP32.  Attempting to `execve` an ELF32 RISC-V binary directly returns
`ENOEXEC` (shell exit code 126).

**This is the central gap in the spike.**  The production architecture calls
for the launcher to exec ILP32 cart binaries directly, relying on the kernel's
native compat path.  Because that path is absent on this kernel, direct ILP32
exec was never tested.

**Workaround used in Stages 2–4:** rv32emu (an LP64 RV64 binary) was used as
the cart runner in place of direct ILP32 exec.  rv32emu interprets RV32
instructions in software; from the kernel's perspective it is a plain LP64
process.  This unblocked the remaining stages but does not represent the
production path.

### Finding 2: seccomp_data.arch = AUDIT_ARCH_RISCV64 for all processes — on this kernel only

Because the workaround runs ILP32 cart code inside rv32emu (an LP64 process),
`seccomp_data.arch` is always `AUDIT_ARCH_RISCV64` (0xC00000F3) on this
kernel.  `AUDIT_ARCH_RISCV32` (0x400000F3) is never reported.

**This finding is specific to the Fedora 42 / missing ELF32 loader situation.**
On a kernel with native ILP32 exec support, the cart process after `execve`
would be a genuine ILP32 process, and `seccomp_data.arch` would be
`AUDIT_ARCH_RISCV32`.  Arch-dispatch between launcher and cart would then
work as the original PLAN assumed.

### Finding 3: LIFO multi-filter semantics confirmed (kernel-independent)

Test C: install phase-1 (kills uname by NR) then phase-2 (allows all).
Result → SIGSYS (rc=159).  LIFO confirmed: phase-2 ALLOW passes to phase-1 KILL.

This is a property of the Linux seccomp subsystem and holds regardless of
the ILP32 exec question.  Option B two-phase enforcement (launcher installs
permissive phase-1, cart runner installs restrictive phase-2 at startup) is
viable.

### Finding 4: libseccomp is NOT sufficient — raw BPF required

`libseccomp` has no `SCMP_ARCH_RISCV32` constant and its RISCV64 filter omits
syscalls like `uname` (Spike H: `seccomp_syscall_resolve_name("uname")` returns
`SCMP_ERROR` on RISCV64).  The raw BPF approach works correctly and avoids
this dependency entirely.  This finding is independent of the ILP32 exec gap.

---

## What the PLAN assumed vs. what was found

| PLAN assumption | Actual finding |
|-----------------|----------------|
| Fedora 42 kernel supports native ILP32 exec via CONFIG_COMPAT | ELF32 loader absent; direct ILP32 exec returns ENOEXEC |
| `seccomp_data.arch = RISCV32` for cart process | RISCV64 always (consequence of missing loader; see Finding 2) |
| Arch-dispatch filter: RISCV64 rules for launcher, RISCV32 rules for cart | Untested on this kernel; expected to work on a kernel with the ELF32 loader |
| libseccomp limitation is RISCV32 arch only | libseccomp also incomplete for RISCV64 (uname missing) |

The LIFO semantics and raw BPF construction are correct and validated.
The arch-dispatch design is still the right approach for production — it just
needs a kernel that actually loads ILP32 ELFs natively.

---

## rv32emu workaround findings (Stages 2–4)

These findings describe the rv32emu-based path tested in Stages 2–4.  They
are not the production path but are recorded for completeness.

**rv32emu syscall set (23 LP64 host syscalls observed):**
rv32emu interpreter-only (CONFIG_EXT_A, no JIT/TCG) needs 23 LP64 host
syscalls to load and run RV32 cart workloads.  This is 53% fewer than
qemu-riscv32-static (49 syscalls), which adds JIT/TCG threads, ILP32 compat
wrappers, and musl-specific startup calls.

**faccessat(48) gap:** rv32emu's own ld.so calls `faccessat` to check
`/etc/ld.so.preload` at startup.  This was absent from the initial allowlist
and would have caused SIGSYS.

**seccomp_allowlist.h** reflects this 23-syscall rv32emu workaround set.
It must be re-derived once native ILP32 exec is available (see Open items).

---

## Open items

- **Primary gap — native ILP32 exec:** The production path (launcher execs
  ILP32 cart directly; kernel native compat) has not been tested.  Requires a
  RISC-V kernel with the ILP32 ELF binary loader enabled.  See "Way forward"
  below.
- **Option B two-phase implementation:** LIFO semantics are confirmed viable.
  The launcher must install a permissive phase-1 filter; the process that runs
  cart code must install a restrictive phase-2 filter (blocking execve, etc.)
  before executing any cart instructions.
- **`SECCOMP_FILTER_FLAG_TSYNC`:** Needed if the cart runner is multi-threaded.

---

## Way forward

The spike needs to be re-run (or extended as Spike S) on a kernel that
includes the RISC-V ILP32 ELF binary loader.  Two options:

**Option 1 — Custom kernel build in QEMU guest.**
Build a Fedora 42 kernel from source with `CONFIG_COMPAT=y` and the
`arch/riscv/kernel/compat_elf.c` / ILP32 ELF loader patches applied.  This
keeps the QEMU test infrastructure unchanged and avoids the need for hardware.
Main cost: kernel build time inside a RISC-V QEMU guest (~hours).

**Option 2 — Real hardware.**
Run on a RISC-V board (Milk-V Duo, VisionFive 2, etc.) whose vendor kernel
ships with ILP32 support enabled.  Validates on real silicon simultaneously.
Main cost: hardware procurement and setup.

**What to test on the new kernel:**
1. Confirm `execve` of an ILP32 ELF succeeds natively (no ENOEXEC).
2. Confirm `seccomp_data.arch = AUDIT_ARCH_RISCV32` for the cart process.
3. Build an arch-dispatch filter (RISCV64 rules for launcher, RISCV32 rules
   for cart) and verify it applies correctly after `execve`.
4. Derive the ILP32 native syscall allowlist via strace of cart workloads
   running natively (not inside rv32emu).
5. Validate Option B two-phase: launcher phase-1 + cart process phase-2.
