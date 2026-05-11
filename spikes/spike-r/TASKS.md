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

## Stage 2 — launcher integration

| # | Step | Host | Guest | Notes |
|---|------|------|-------|-------|
| 14 | `install_raw_bpf_filter()` replaces spike_h_install_seccomp | ✅ | n/a | No libseccomp dependency |
| 15 | `launcher_r` compiles without libseccomp | ✅ | n/a | Uses raw seccomp(2) syscall directly |
| 16 | `socket` probe → SIGSYS (rc=159) | n/a | ✅ | socket(198) not in allowlist |
| 17 | `uname` probe → exit 0 | n/a | ✅ | uname(160) in allowlist — key difference from Spike H |
| 18 | Stage 2 overall: PASS | n/a | ✅ | Both probes correct |

**Probes NOT testable by seccomp alone (require mount namespace or Option B):**
- `open` (openat=56 is in allowlist for ELF loading; filesystem isolated by mount namespace)
- `execve` (in allowlist for launcher→cart exec; Option B required for post-exec blocking)
- `mprotect-exec` (mprotect=226 in allowlist; arg filtering or W^X kernel policy needed)

## Stage 3 — production allowlist derivation

| # | Step | Host | Guest | Notes |
|---|------|------|-------|-------|
| 22 | rv32emu available in guest | ☐ | ☐ | Required for production strace runs |
| 23 | Cart workloads available in guest | ☐ | ☐ | spike-i/o/q .blyt files |
| 24-27 | strace each workload type | n/a | ☐ | `derive_allowlist.sh <cart_dir>` |
| 28-31 | Allowlist culled and committed | ☐ | n/a | Update seccomp_unified_nrs[] |
| 32 | Stage 3 overall: PASS | ☐ | ☐ | deferred |

## Stage 4 — adversary re-verification with production allowlist

| # | Step | Notes |
|---|------|-------|
| 33-36 | Verify with Stage 3 allowlist | deferred; depends on Stage 3 |

---

## Key findings

### Finding 1: CONFIG_COMPAT has NO ELF32 loader on this kernel

Fedora 42 kernel 6.16.4 has `compat_sys_*` wrappers (in kallsyms) but no
ELF32 binary loader for RISC-V ILP32.  Disabling binfmt_misc causes
`execve` of ILP32 binaries to fail with ENOEXEC (exit code 126).

ILP32 binaries run via **binfmt_misc + qemu-riscv32-static**.

### Finding 2: seccomp_data.arch = RISCV64 always

Because `qemu-riscv32-static` is an LP64 RV64 process, `seccomp_data.arch`
= `AUDIT_ARCH_RISCV64` (0xC00000F3) for ALL processes.
`AUDIT_ARCH_RISCV32` (0x400000F3) is never seen.

Empirical proof:
- RISCV64-only filter (allow all RISCV64): ILP32 uname → rc=0 ✓
- RISCV32-only filter (allow all RISCV32): ILP32 uname → rc=159 ✓

**Arch-dispatch between LP64 and ILP32 is not possible on this kernel.**

### Finding 3: Unified RISCV64 filter works for ILP32 cart syscalls

qemu-riscv32-static translates RV32 syscalls → LP64 host syscalls.
These host syscalls are subject to the seccomp filter.  Allowlisting
the LP64 host syscall NRs (uname=160, etc.) correctly allows ILP32
cart operations.

### Finding 4: LIFO multi-filter semantics confirmed

Phase-1 (KILL uname by NR) + Phase-2 (ALLOW all): ILP32 uname → SIGSYS.
Confirms: phase-2 ALLOW → eval phase-1 → KILL.  Option B two-phase is viable.

### Finding 5: Allowlist derivation was iterative

The production allowlist for qemu-riscv32-static was derived iteratively
via strace inside the Fedora 42 QEMU guest.  Key additions beyond the
original spike-h list: fcntl(25), openat(56), lseek(62), pread64(67),
nanosleep(101), clock_nanosleep(115), rseq(293), clone3(435), sysinfo(179),
madvise(233).

---

## Open items (from PLAN.md)

- Stage 3 strace of rv32emu (NOT qemu-riscv32-static) for production allowlist
- Option B implementation in launcher_r.c (phase-2 for execve blocking)
- Hardware validation on Milk-V Duo
- `SECCOMP_FILTER_FLAG_TSYNC` if rv32emu goes multi-threaded
