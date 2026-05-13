# Spike S Results

**Question:** Does the hardware trusted native-exec path (ADR-0119) work in
practice? Concretely: can a LP64 launcher exec an ILP32 cart binary natively,
produce a cart process with `seccomp_data.arch = AUDIT_ARCH_RISCV32`, enforce
an arch-dispatch seccomp filter across the exec boundary, and have
`libblyt32.so`'s constructor install a restrictive phase-2 filter before any
cart code runs?

**Status: Stages 0 and 1 PASS. Stages 2–4 pending.**

Stages 0 and 1 ran on 2026-05-12/13 using:
- **Kernel:** c-sky/csky-linux `sg2042-master-qspinlock-64ilp32_v4` (Linux 6.5-rc1),
  plus three kernel patches (see `apply-syscall-arch-patch.py` and
  `0001-riscv-compat-fix-syscall_get_arch-for-ILP32-processes.patch`)
- **QEMU:** Fedora 42 Cloud rootfs with custom kernel via direct `-kernel` boot
- **Host:** Apple Silicon (macOS)

---

## Per-stage results

| Stage | Result | Date | Notes |
|-------|--------|------|-------|
| 0 — environment | **PASS** | 2026-05-12 | ILP32 ELF loader present; static ILP32 binary runs |
| 1 — arch-dispatch filter | **PASS** | 2026-05-13 | All 7 tests pass; AUDIT_ARCH_RISCV32 confirmed |
| 2 — ld.so syscall characterisation | **PENDING** | — | Requires dynamic ILP32 binary |
| 3 — ILP32 cart allowlist derivation | **PENDING** | — | Requires Stage 2 |
| 4 — Option B end-to-end | **PENDING** | — | Requires Stage 3 + updated allowlist |

---

## Environment

| Item | Value |
|------|-------|
| Host | Apple Silicon (macOS), QEMU 9.x |
| Guest kernel | c-sky/csky-linux `sg2042-master-qspinlock-64ilp32_v4` (6.5-rc1), commit `ee021e57f`, + 3 patches |
| Kernel config | `CONFIG_COMPAT=y`, `CONFIG_COMPAT_BINFMT_ELF=y`, VirtIO MMIO, 9p disabled |
| Fedora rootfs | Fedora 42 Cloud (same qcow2 as Spike R), btrfs root, `rootflags=subvol=root` |
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

### Finding 3: ILP32 syscall NR unification

*(Pending Stage 3 strace — empirical confirmation still needed.)*  
Consistent with RISC-V spec: unified syscall table, no ILP32-specific compat NRs.

### Finding 4: Phase-2 constructor ordering

*(Pending Stage 4.)*

### Finding 5: LIFO seccomp across exec

Filters installed by the LP64 parent before exec ARE inherited by the ILP32 child
after exec (confirmed by the arch-dispatch filter applying RISCV32 rules to the
ILP32 process).  The LIFO property is inherited unchanged from Spike R Stage 1.

---

## Syscall allowlists (to be filled in after Stage 3)

### seccomp_phase1_riscv64_nrs[] — LP64 launcher (post-filter)

*(Using placeholder from seccomp_allowlist_s.h — to be confirmed as correct
since the LP64 launcher calls very few syscalls after filter install.)*

| NR | Name | Reason |
|----|------|--------|
| 221 | execve | launcher → cart binary |
| 64 | write | error output |
| 94 | exit_group | failure exit |

### seccomp_phase1_riscv32_nrs[] — ILP32 ld.so startup

*(Placeholder — requires Stage 2 strace to validate.)*

See `spikes/spike-s/seccomp_allowlist_s.h` for the current placeholder list.
After Stage 2, replace with empirically-derived values and note any
differences from the placeholder.

### seccomp_phase2_riscv32_nrs[] — ILP32 cart code

*(Placeholder — requires Stage 3 strace to validate.)*

See `spikes/spike-s/seccomp_allowlist_s.h` for the current placeholder list.
After Stage 3, replace with empirically-derived values.

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

---

## Related

- `spikes/spike-r/` — emulated target path (rv32emu on LP64 host); LIFO
  semantics confirmed; arch-dispatch blocked by missing ILP32 ELF loader.
- `docs/design/spike-r-results.md` — Spike R findings.
- `spikes/spike-s/seccomp_allowlist_s.h` — allowlist header (update after Stage 3).
- `spikes/spike-s/TASKS.md` — per-step checklist.
