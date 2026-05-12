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

**Answered for the emulated target path; hardware trusted-exec path
deferred to Spike S.**

The raw BPF filter mechanics (construction, LIFO semantics, libseccomp
inadequacy) are fully validated. Stages 2–4 validated the complete
seccomp filter stack for **emulated targets** (rv32emu on desktop, Pi,
WASM): the 23-syscall LP64 host allowlist was derived, end-to-end
cart execution under the production filter was confirmed.

The **hardware trusted native-exec path** (ADR-0119: pre-installed cart
exec'd as a native ILP32 process, seccomp_data.arch = AUDIT_ARCH_RISCV32,
arch-dispatch filter) was not testable on this kernel because Fedora 42
lacks the RISC-V ILP32 ELF binary loader. That path requires Spike S.

---

## Per-stage results

| Stage | Result | Notes |
|-------|--------|-------|
| 0 — environment | **PASS** | seccomp(2) present; CONFIG_COMPAT present but no ILP32 ELF loader (hardware trusted-exec path untestable on this kernel) |
| 1 — raw BPF filter | **PASS** | Filter mechanics: write allowed, socket blocked, LIFO confirmed |
| 2 — launcher integration | **PASS** | Emulated target path: launcher + rv32emu cart runner |
| 3 — rv32emu strace allowlist | **PASS** | 23 LP64 host syscalls; production allowlist for emulated targets |
| 4 — adversary re-verification | **PASS** | Emulated target: adversary probes + rv32emu cart run confirmed |

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

### Finding 2: seccomp_data.arch = AUDIT_ARCH_RISCV64 for all processes — emulated targets only

On emulated targets, ILP32 cart code runs inside rv32emu (an LP64 process),
so `seccomp_data.arch` is always `AUDIT_ARCH_RISCV64` (0xC00000F3).
`AUDIT_ARCH_RISCV32` (0x400000F3) is never reported.

**This is correct and expected for emulated targets.** On the hardware
trusted native-exec path (ADR-0119), the cart process is a genuine ILP32
process and `seccomp_data.arch = AUDIT_ARCH_RISCV32`, enabling the original
arch-dispatch design. That path is covered by Spike S.

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

## Emulated target findings (Stages 2–4)

These findings describe the rv32emu-based path, which is the correct and
complete architecture for emulated targets (desktop, Pi, WASM).

**rv32emu syscall set (23 LP64 host syscalls observed):**
rv32emu interpreter-only (CONFIG_EXT_A, no JIT/TCG) needs 23 LP64 host
syscalls to load and run RV32 cart workloads.  This is 53% fewer than
qemu-riscv32-static (49 syscalls), which adds JIT/TCG threads, ILP32 compat
wrappers, and musl-specific startup calls.

**faccessat(48) gap:** rv32emu's own ld.so calls `faccessat` to check
`/etc/ld.so.preload` at startup.  This was absent from the initial allowlist
and would have caused SIGSYS.  Added in Stage 3.

**seccomp_allowlist.h** contains the production 23-syscall set for emulated
targets.  The hardware trusted-exec path requires a separate allowlist
(ILP32 native syscalls, AUDIT_ARCH_RISCV32); see Spike S.

---

## Open items

- **Hardware trusted-exec path (Spike S):** The hardware native-exec path
  (ADR-0119) requires a RISC-V kernel with the ILP32 ELF binary loader.
  Fedora 42's kernel lacks this.  Spike S validates: native ILP32 exec
  succeeds; `seccomp_data.arch = AUDIT_ARCH_RISCV32` for the cart process;
  arch-dispatch filter works; ILP32 native syscall allowlist derived.
  Test environment: custom kernel build in QEMU or real hardware.
- **Option B phase-2 seccomp in libblyt32.so:** LIFO semantics confirmed
  (Finding 3).  libblyt32.so's constructor must install the phase-2 filter
  before returning to cart code.  Implementation deferred to Spike S.
- **`SECCOMP_FILTER_FLAG_TSYNC`:** Needed if the cart runner is multi-threaded.
