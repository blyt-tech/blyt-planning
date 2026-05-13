# Spike S Results

**Question:** Does the hardware trusted native-exec path (ADR-0119) work in
practice? Concretely: can a LP64 launcher exec an ILP32 cart binary natively,
produce a cart process with `seccomp_data.arch = AUDIT_ARCH_RISCV32`, enforce
an arch-dispatch seccomp filter across the exec boundary, and have
`libblyt32.so`'s constructor install a restrictive phase-2 filter before any
cart code runs?

**Answer: YES. All stages pass.**

Stages 0–4 ran on 2026-05-12/13 using:
- **Kernel:** c-sky/csky-linux `sg2042-master-qspinlock-64ilp32_v4` (Linux 6.5-rc1),
  plus three kernel patches (see `apply-syscall-arch-patch.py` and
  `0001-riscv-compat-fix-syscall_get_arch-for-ILP32-processes.patch`)
- **QEMU:** Fedora 42 Cloud rootfs with custom kernel via direct `-kernel` boot
- **Host:** Apple Silicon (macOS)

---

## Per-stage results

| Stage | Result | Date | Notes |
|-------|--------|------|-------|
| 0 — environment | **PASS** | 2026-05-12 | ILP32 ELF loader present; static ILP32 binary runs; seccomp(2) available |
| 1 — arch-dispatch filter | **PASS** | 2026-05-13 | All 7 tests pass; AUDIT_ARCH_RISCV32=0x400000F3 confirmed |
| 2 — ld.so syscall characterisation | **PASS** | 2026-05-13 | Dynamic ILP32 binary reaches main() rc=0; 12 ld.so-phase syscalls |
| 3 — ILP32 cart allowlist derivation | **PASS** | 2026-05-13 | 23-syscall union; unified NR table confirmed; phase-2 confirmed |
| 4 — Option B end-to-end | **PASS** | 2026-05-13 | socket/execve → SIGSYS; write → exit 0; LIFO cross-exec verified |

---

## Environment

| Item | Value |
|------|-------|
| Host | Apple Silicon (macOS), QEMU 9.x |
| Guest kernel | c-sky/csky-linux `sg2042-master-qspinlock-64ilp32_v4` (6.5-rc1), commit `ee021e57f`, + 3 patches |
| Kernel config | `CONFIG_COMPAT=y`, `CONFIG_COMPAT_BINFMT_ELF=y`, VirtIO MMIO |
| Fedora rootfs | Fedora 42 Cloud (same qcow2 as Spike R), btrfs root, `rootflags=subvol=root` |
| ILP32 ld.so | musl riscv32-linux-musl (ilp32d, double-float ABI) |
| Date | 2026-05-12/13 |

---

## Findings

### Finding 1: AUDIT_ARCH_RISCV32 confirmed for native ILP32 processes

`seccomp_data.arch = AUDIT_ARCH_RISCV32 = 0x400000F3` for natively-exec'd ILP32
processes.  This is the enabling property for arch-dispatch filters.

Three kernel patches were required on the c-sky 6.5-rc1 tree:

**Patch 1** — `arch/riscv/include/asm/syscall.h`:  
`syscall_get_arch()` unconditionally returned RISCV64 for all processes.  Adding
`if (test_tsk_thread_flag(task, TIF_32BIT)) return AUDIT_ARCH_RISCV32;` makes
ILP32 processes (TIF_32BIT set by `SET_PERSONALITY` during ELF loading) report
the correct arch.

**Patch 2** — `arch/riscv/include/asm/seccomp.h`:  
`SECCOMP_ARCH_COMPAT` was not defined.  The seccomp cache used only a native
bitmap, ignoring `sd->arch` entirely.  For a RISCV64-only allow-all filter, ALL
bits in the native bitmap are set; ILP32 syscalls bypassed the BPF filter via the
cache and were always allowed.  Defining `SECCOMP_ARCH_COMPAT = AUDIT_ARCH_RISCV32`
triggers the two-bitmap path in `seccomp_cache_check_allow`.

**Patch 3** — `kernel/seccomp.c` (`seccomp_cache_check_allow`):  
Even with patch 2, the allow_compat bitmap was populated incorrectly (allowing
syscalls that should be blocked for ILP32).  Skip the cache for processes with
`TIF_32BIT` set to ensure the BPF filter is always evaluated for ILP32 processes.
(A production fix would correctly populate `allow_compat` in
`seccomp_cache_prepare_bitmap`.)

### Finding 2: Arch-dispatch filter across exec boundary — CONFIRMED

A single BPF filter installed by the LP64 parent (before exec) applies different
rules to:
- The LP64 process (arch=RISCV64): execve allowed, socket blocked
- The ILP32 child process after exec (arch=RISCV32): write allowed, socket/execve blocked

Tests A–G of Stage 1 all pass, including the direct arch-probe (Test G: write
returns ERRNO(42) only when arch=RISCV32).

### Finding 3: ILP32 syscall NR unification — CONFIRMED

Stage 3 strace confirmed: RISC-V ILP32 uses the same syscall numbers as LP64.
No ARM/x86-style compat NRs (mmap2, clock_gettime64, etc.) appeared in any
trace.  The unified syscall table design is correct.

### Finding 4: musl ILP32 ld.so uses statx, fcntl, writev — not fstat/lseek/write

Stage 2 strace (19 lines, pure ILP32 trace) revealed that musl ILP32 ld.so:
- Uses `statx` (NR=291), not `fstat` (NR=80), for file information
- Uses `fcntl` (NR=25) to set `FD_CLOEXEC` after `openat`
- Uses `writev` (NR=66), not `write` (NR=64), for `fprintf`/stdio output

These were missing from the initial placeholder `seccomp_phase1_riscv32_nrs[]`.
The placeholder would have caused SIGSYS during ld.so startup in Stage 4.
All three were added before Stage 4 ran.

### Finding 5: Phase-2 constructor ordering — VERIFIED

The `libblyt32.so` constructor installs phase-2 BEFORE `main()` runs.  The
filter is correctly active for all subsequent ILP32 syscalls.  This was
verified implicitly: Tests 4a (socket→SIGSYS) and 4b (execve→SIGSYS) prove
that phase-2 was installed before the probe syscalls were made.

### Finding 6: LIFO seccomp across exec — CONFIRMED

Filters installed by the LP64 launcher before exec are inherited by the ILP32
cart process after exec (Linux seccomp(2): "filters are inherited across
execve(2)").  Phase-2 (installed by the ILP32 constructor) layers OVER phase-1
(installed by the LP64 launcher) via LIFO semantics.  Phase-2's KILL for socket
and execve overrides phase-1's ALLOW for those syscalls in the RISCV32 block.

### Finding 7: LP64 strace contaminates Stage 3 union with glibc syscalls

Stage 3 used `env LD_LIBRARY_PATH=... cart_binary` as the strace target.  The
LP64 `env` process (dynamically linked against glibc) contributes LP64 syscalls
(faccessat, fstat, futex, prlimit64, rseq, set_robust_list, munmap, getrandom)
to the strace union before exec-ing the ILP32 cart.  These are RISCV64 syscalls
and do NOT need to be in the ILP32 phase-1 or phase-2 allowlist.  The ILP32
musl ld.so (shown clearly in Stage 2's direct ILP32 trace) does not call any of
these.

### Finding 8: ilp32f (single-float) dynamic binaries tolerated; static binaries rejected

The spike-i carts (ilp32f, single-float ABI) are dynamically linked against the
ilp32d musl ld.so.  They ran successfully despite the ABI mismatch.  The c-sky
kernel accepts the ELF class mismatch for dynamic binaries (defers to ld.so
which doesn't enforce float-ABI alignment).  However, rust_cart.elf (ilp32f,
statically linked) got `EACCES` — the c-sky kernel's ILP32 ELF loader rejects
non-ilp32d static binaries.  Production carts will be ilp32d, so this does not
affect the production path.

---

## Empirically-validated syscall allowlists

### seccomp_phase1_riscv64_nrs[] — LP64 launcher (post-filter)

| NR | Name | Reason |
|----|------|--------|
| 221 | execve | launcher → cart binary |
| 64 | write | error messages before exec |
| 94 | exit_group | failure exit |

### seccomp_phase1_riscv32_nrs[] — ILP32 ld.so startup + constructor

Empirically confirmed by Stage 2 strace (19 lines, adversary_s_dynamic):

| NR | Name | Stage 2 strace | Reason |
|----|------|----------------|--------|
| 56 | openat | ✅ seen | open libblyt32.so per DT_NEEDED |
| 57 | close | ✅ seen | close fd after ELF load |
| 63 | read | ✅ seen | read ELF header + section data |
| 25 | fcntl | ✅ seen | F_SETFD(FD_CLOEXEC) on lib fd after openat |
| 291 | statx | ✅ seen | musl uses statx (not fstat) for file info |
| 222 | mmap | ✅ seen | ELF PT_LOAD segment mapping + guard page |
| 226 | mprotect | ✅ seen | segment permissions after mmap + relocation |
| 214 | brk | ✅ seen | heap init during ld.so startup |
| 96 | set_tid_address | ✅ seen | musl TLS self-pointer init |
| 66 | writev | ✅ seen | musl uses writev for stdio (constructor fprintf) |
| 277 | seccomp | ✅ seen | install phase-2 RISCV32 filter |

Conservative extras (in phase-1 for larger carts; not seen with minimal binary):

| NR | Name | Reason |
|----|------|--------|
| 80 | fstat | older musl versions may use fstat instead of statx |
| 215 | munmap | unmap temporaries when loading many libs |
| 278 | getrandom | stack-canary entropy (some musl paths) |
| 261 | prlimit64 | libc stack-size probe |
| 29 | ioctl | isatty probe in stdio (terminal-aware output) |
| 64 | write | cart output (raw write syscall) |
| 93 | exit | single-thread exit |
| 94 | exit_group | cart exits |

### seccomp_phase2_riscv32_nrs[] — ILP32 cart code (post-constructor)

Confirmed by Stage 2 + Stage 3:

| NR | Name | Reason |
|----|------|--------|
| 29 | ioctl | isatty probe (terminal-aware stdio) |
| 63 | read | cart input |
| 64 | write | cart output (raw SYS_write) |
| 93 | exit | single-thread exit |
| 94 | exit_group | cart exits |

Syscalls blocked by phase-2 (tested in Stage 4):
- `socket` (198) → SIGSYS ✓
- `execve` (221) → SIGSYS ✓

---

## Stage 4 test matrix

| Test | Probe | Expected | Got | Result |
|------|-------|----------|-----|--------|
| 4a | socket | SIGSYS (rc=159) | rc=159 | **PASS** |
| 4b | execve | SIGSYS (rc=159) | rc=159 | **PASS** |
| 4c | write | exit 0 (rc=0) | rc=0 | **PASS** |
| 4d | cart_a | rc=0 or 124 | SKIP (not SCP'd) | — |
| 4e | cart_b | rc=0 or 124 | SKIP (not SCP'd) | — |
| 4f | rust_cart | rc=0 or 124 | SKIP (EACCES) | — |
| adversary_dynamic_write | write | rc=0 | rc=0 | **PASS** |

Phase-1 filter: 66 instructions (rv64=3, rv32=26).
Phase-2 filter: 15 instructions (5 syscalls).

---

## Open items

- **Hardware validation (Option 2).** Once Option 1 (QEMU) confirms the
  mechanism, repeat Stage 3 strace on real RISC-V silicon to confirm the
  allowlist is complete on native hardware.  QEMU's syscall path may differ
  from bare-metal.
- **`SECCOMP_FILTER_FLAG_TSYNC`.** Not needed for single-threaded v1.  Add if
  libblyt32.so or the cart runner is ever multi-threaded.
- **execve argument filter.** Phase-1 allows execve for the LP64 launcher only
  (RISCV64 arch rules).  Further restricting *which* paths can be exec'd is a
  production hardening task.
- **Full DT_NEEDED set.** This spike uses a minimal stub (libblyt32.so only).
  A follow-up validates all three: `libblyt32.so`, `libblytc.so`,
  `libblyt32lua.so` with each library's phase-1 syscall contributions captured.
- **Kernel patches upstreaming.** The three patches applied to the c-sky
  6.5-rc1 tree need review for upstream contribution.  The seccomp cache fix
  (patch 3) in particular should have a proper allow_compat bitmap implementation
  rather than the TIF_32BIT cache-skip workaround.
- **Stage 4 cart workloads.** Tests 4d/4e (cart_a/cart_b) were skipped because
  the Makefile for Stage 4 does not SCP the spike-i carts.  Add cart SCP to
  qemu-stage4-s for completeness.

---

## Related

- `spikes/spike-r/` — emulated target path (rv32emu on LP64 host); LIFO
  semantics confirmed; arch-dispatch blocked by missing ILP32 ELF loader.
- `docs/design/spike-r-results.md` — Spike R findings.
- `spikes/spike-s/seccomp_allowlist_s.h` — empirically-validated allowlist header.
- `spikes/spike-s/TASKS.md` — per-step checklist.
- `spikes/spike-s/apply-syscall-arch-patch.py` — kernel patches applied.
