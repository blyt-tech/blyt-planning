# Spike R — task checklist

Tracks pass/fail per `PLAN.md` step.

Test surfaces:
- **Host** — Docker cross-compile + binary verification
- **QEMU guest** — Fedora 42 RISC-V (kernel 6.16.4-200.0.riscv64.fc42)

One QEMU guest run:

| Guest | Kernel | Date |
|-------|--------|------|
| Fedora 42 Cloud riscv64 | 6.16.4-200.0.riscv64.fc42 | 2026-05-11 |

---

## Stage 0 — environment check

| # | Step | Host | Guest | Notes |
|---|------|------|-------|-------|
| 1 | Fedora 42 QEMU image present | ✅ | n/a | `spike-h/qemu/Fedora-Cloud-Base-Generic-42.riscv64.qcow2` |
| 2 | U-Boot ELF present | ✅ | n/a | `spike-h/qemu/uboot-riscv64.elf` |
| 3 | Spike H adversary binary present | ✅ | n/a | `spike-h/build/adversary` (RV32 musl static ilp32d) |
| 4 | `seccomp_raw_test` cross-compiles | ✅ | n/a | RV64 static binary; `make docker-build-r` uses docker cp |
| 5 | `launcher_r` cross-compiles | ✅ | n/a | RV64 dynamic binary |
| 6 | seccomp(2) syscall (277) present in guest | ✅ | ✅ | Found in /proc/kallsyms |
| 7 | CONFIG_COMPAT compat_sys_* in kallsyms | ✅ | ✅ | Compiled in but **no ELF32 loader** (see Finding 1) |

## Stage 1 — raw BPF filter validation

| # | Step | Host | Guest | Notes |
|---|------|------|-------|-------|
| 8 | `AUDIT_ARCH_RISCV32 = 0x400000F3` defined | ✅ | n/a | In header; never reported by kernel (Finding 2) |
| 9 | `build_unified_filter()` generates correct bytecode | ✅ | ✅ | Verified empirically |
| 10 | Test A: ILP32 uname → exit 0 | n/a | ✅ | Unified filter allows uname(160) |
| 11 | Test B: socket(198) → SIGSYS (rc=159) | n/a | ✅ | Default-deny blocks socket |
| 12 | Test C: multi-filter LIFO → SIGSYS | n/a | ✅ | Phase-2 ALLOW passes to phase-1 KILL |
| 13 | Stage 1 overall: PASS | n/a | ✅ | All 3 sub-tests pass |

## Stage 2 — launcher integration (emulated target path)

| # | Step | Host | Guest | Notes |
|---|------|------|-------|-------|
| 14 | `install_raw_bpf_filter()` replaces spike_h_install_seccomp | ✅ | n/a | No libseccomp dependency |
| 15 | `launcher_r` compiles without libseccomp | ✅ | n/a | Uses raw seccomp(2) syscall directly |
| 16 | `socket` probe → SIGSYS (rc=159) | n/a | ✅ | socket(198) not in allowlist |
| 17 | `uname` probe → exit 0 | n/a | ✅ | uname(160) in allowlist — key difference from Spike H |
| 18 | Stage 2 overall: PASS | n/a | ✅ | Emulated target path (rv32emu as cart runner) |

**Probes NOT testable by seccomp alone (require mount namespace or Option B):**
- `open` (openat=56 is in allowlist for ELF loading; filesystem isolated by mount namespace)
- `execve` (in allowlist for launcher→cart exec; Option B required for post-exec blocking)
- `mprotect-exec` (mprotect=226 in allowlist; arg filtering or W^X kernel policy needed)

## Stage 3 — allowlist derivation (emulated target path)

| # | Step | Host | Guest | Notes |
|---|------|------|-------|-------|
| 22 | rv32emu available in guest | ✅ | ✅ | Built from spike-a source in guest (CONFIG_EXT_A + multi-dynload) |
| 23 | Cart workloads available in guest | ✅ | ✅ | spike-i case_a/b + spike-q rust_cart.elf |
| 24-27 | strace each workload type | n/a | ✅ | 22 unique syscalls; strace_*.txt in build/results/stage3-strace/ |
| 28-31 | Allowlist culled and committed | ✅ | n/a | seccomp_allowlist.h updated; 23 syscalls (was 49) |
| 32 | Stage 3 overall: PASS | n/a | ✅ | 2026-05-11; production allowlist for emulated targets |

**Note: seccomp_allowlist.h is the production allowlist for emulated targets (rv32emu LP64 host syscalls).**
The hardware trusted-exec path (AUDIT_ARCH_RISCV32, native ILP32 syscalls) requires a separate
derivation in Spike S.

## Stage 4 — adversary re-verification (emulated target path)

| # | Step | Notes |
|---|------|-------|
| 33 | rv32emu cart_a under launcher_r | ✅ | cart exits rc=0; no SIGSYS (2026-05-11) |
| 34 | socket probe → SIGSYS | ✅ | rc=159 confirmed |
| 35 | uname probe → SIGSYS | ✅ | rc=159 — correctly blocked (not in rv32emu allowlist) |
| 36 | Stage 4 overall: PASS | ✅ | 2026-05-11; emulated target path complete |

---

## Key findings

### Finding 1: Fedora 42 kernel lacks the RISC-V ILP32 ELF binary loader — core gap

Fedora 42 kernel 6.16.4 has `compat_sys_*` wrappers (in kallsyms) but no
ELF32 binary loader for RISC-V ILP32.  Attempting to exec an ILP32 binary
directly returns ENOEXEC (exit code 126).

**This blocked the production path.** Stages 2–4 fell back to rv32emu (an LP64
process that interprets RV32 instructions) as the cart runner.  This is not the
production architecture.

### Finding 2: seccomp_data.arch = RISCV64 always — on this kernel only

Consequence of Finding 1: ILP32 cart code runs inside rv32emu (LP64), so
`seccomp_data.arch` is always `AUDIT_ARCH_RISCV64`.  On a kernel with native
ILP32 exec, the cart process would report `AUDIT_ARCH_RISCV32` and
arch-dispatch would work as originally designed.

### Finding 3: LIFO multi-filter semantics confirmed (kernel-independent)

Phase-1 (KILL uname by NR) + Phase-2 (ALLOW all): ILP32 uname → SIGSYS.
Confirms: phase-2 ALLOW → eval phase-1 → KILL.  Option B two-phase is viable.
This is a Linux kernel property unaffected by the ILP32 exec gap.

### Finding 4: libseccomp is insufficient — raw BPF required

libseccomp has no SCMP_ARCH_RISCV32 and omits syscalls like uname on RISCV64.
Raw BPF seccomp(2) works correctly and is required.

---

## Open items

- **Spike S — hardware trusted-exec path:** Fedora 42 kernel lacks the RISC-V
  ILP32 ELF binary loader, so the hardware native-exec path (ADR-0119) was not
  testable here.  Spike S validates: native ILP32 exec, AUDIT_ARCH_RISCV32,
  arch-dispatch filter, ILP32 native syscall allowlist derivation.
- **Option B — libblyt32.so phase-2 filter:** LIFO semantics confirmed; the
  constructor in libblyt32.so must install the phase-2 filter.  Deferred to Spike S.
- **`SECCOMP_FILTER_FLAG_TSYNC`:** Needed if the cart runner is multi-threaded.
