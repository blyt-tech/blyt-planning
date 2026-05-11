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

## Per-stage results

| Stage | Result | Notes |
|-------|--------|-------|
| 0 — environment | **PASS** | seccomp(2) present; CONFIG_COMPAT compat_sys_* in kallsyms |
| 1 — raw BPF filter | **PASS** | Tests A (uname allowed), B (socket blocked), C (LIFO confirmed) |
| 2 — launcher integration | **PASS** | socket → SIGSYS; uname → exit 0 |
| 3 — strace allowlist | deferred | Requires rv32emu + cart workloads in guest |
| 4 — adversary re-verification | deferred | Depends on Stage 3 |

---

## Key findings

### Finding 1: CONFIG_COMPAT does NOT support native ELF32 exec on this kernel

The Fedora 42 kernel (6.16.4) compiles `compat_sys_*` functions (visible in
`/proc/kallsyms`) but does **not** include the ELF32 binary loader for
RISC-V ILP32.  Attempting to `execve` an ELF32 RISC-V binary after disabling
binfmt_misc returns `ENOEXEC` (shell exit code 126).

**Consequence:** ILP32 cart binaries run via `binfmt_misc` +
`qemu-riscv32-static`, not via the kernel's native compat path.

### Finding 2: seccomp_data.arch = AUDIT_ARCH_RISCV64 for all processes

Because ILP32 binaries run inside `qemu-riscv32-static` (an LP64 RV64
process), `seccomp_data.arch` is always `AUDIT_ARCH_RISCV64` (0xC00000F3).
`AUDIT_ARCH_RISCV32` (0x400000F3) is never reported.

**Empirically confirmed:**
- Diagnostic E (RISCV64-only filter, ALLOW all): ILP32 uname → rc=0 ✓
- Diagnostic F (RISCV32-only filter, ALLOW all): ILP32 uname → rc=159 ✓ (arch≠RISCV32)

**Consequence:** Arch-dispatch between LP64 and ILP32 via `seccomp_data.arch`
is not possible on this kernel.  A unified RISCV64 filter applies to both the
LP64 launcher and the ILP32 cart runner (qemu-riscv32-static / rv32emu).

### Finding 3: Unified RISCV64 filter correctly controls cart syscalls

The raw BPF unified filter (check arch=RISCV64, then allow/block by NR) works
correctly:

- `uname` (NR 160) in allowlist → ILP32 adversary exits 0 (Test A ✓)
- `socket` (NR 198) not in allowlist → ILP32 adversary gets SIGSYS (Test B ✓)
- `openat` (NR 56) in allowlist (needed by qemu-riscv32-static/rv32emu to load
  the cart ELF).  Filesystem isolation is provided by mount namespace in
  production, not by seccomp blocking openat.

### Finding 4: LIFO multi-filter semantics confirmed

Test C: install phase-1 (kills uname by NR) then phase-2 (allows all).
ILP32 uname → rc=159 (SIGSYS).  LIFO confirmed: phase-2 ALLOW passes to
phase-1 KILL.

**Implication (Option B):** The launcher can install a permissive phase-1
filter before `execve`, then have the cart runner (rv32emu) install a
restrictive phase-2 filter at startup (before running cart code) to block
`execve` and other sensitive calls.  LIFO ensures phase-2's KILL rule wins
over phase-1's ALLOW.

### Finding 5: libseccomp is NOT needed — and would not work

`libseccomp` has no `SCMP_ARCH_RISCV32` constant and its LP64 (RISCV64)
filter does not include `uname` (Spike H finding: `seccomp_syscall_resolve_name
("uname")` returns `SCMP_ERROR` on RISCV64).  The raw BPF approach correctly
includes `uname` (NR 160) in the filter and it works.

---

## Delivered allowlist: seccomp_allowlist.h

The `seccomp_unified_nrs[]` array contains the LP64 host syscalls needed by:
- The LP64 launcher child between `install_raw_bpf_filter()` and `execv()`
- `qemu-riscv32-static` (binfmt_misc ILP32 handler) or `rv32emu` (production)
  to initialize and run the cart

Syscalls confirmed by strace of `qemu-riscv32-static` running the adversary
on Fedora 42 kernel 6.16.4.

Notable entries:
- `rseq` (293): restartable sequences, called by qemu-riscv32-static's musl
- `clone3` (435): TCG worker thread creation in qemu-riscv32-static
- `clock_nanosleep` (115): TCG worker thread scheduling
- `openat` (56): qemu-riscv32-static/rv32emu opens the cart ELF
- `fcntl` (25): qemu-riscv32-static reads /proc/self/maps flags
- `lseek` (62): seeking within the cart ELF during loading

**Stage 3 note:** This allowlist was derived from `qemu-riscv32-static`.
The production rv32emu interpreter should be strace'd separately to confirm
the allowlist (rv32emu likely needs fewer syscalls than qemu-riscv32-static's
full JIT compilation path, but the ELF loading and memory management
syscalls should be similar).  Update `seccomp_allowlist.h` after running
`derive_allowlist.sh` over each cart workload type.

rv32emu commit hash for Stage 3: (to be filled after Stage 3 strace run)

---

## What changes from the original PLAN design

The PLAN assumed arch-dispatch (RISCV64 vs RISCV32) would work.  Reality:

| PLAN assumption | Actual finding |
|-----------------|----------------|
| `seccomp_data.arch = RISCV32` for ILP32 cart | `arch = RISCV64` always |
| Option A: single arch-dispatch filter | Unified RISCV64 filter (no arch-dispatch) |
| LP64 allowlist ≠ ILP32 allowlist | Single unified allowlist for both |
| libseccomp limitation is RISCV32 arch | libseccomp also missing uname on RISCV64 |
| Fedora 42 uses CONFIG_COMPAT for ILP32 | binfmt_misc + qemu-riscv32-static |

The core mechanism (raw BPF seccomp, LIFO semantics, Option B two-phase) is
correct and validated.  The arch assumption was wrong but doesn't affect
the approach — a unified RISCV64 filter is simpler than arch-dispatch anyway.

---

## Open items

- **Stage 3:** Run strace of rv32emu over Spike I/O/Q cart workloads to derive
  the production allowlist.  Update `seccomp_allowlist.h`.
- **Stage 4:** Re-verify all adversary probes after Stage 3 allowlist update.
- **execve blocking:** Implement Option B two-phase in `launcher_r.c` — cart
  (rv32emu) installs phase-2 at startup before running cart code.
- **Milk-V Duo hardware:** Verify rv32emu host syscall set on real hardware.
- **`SECCOMP_FILTER_FLAG_TSYNC`:** Add if rv32emu uses multiple threads.
