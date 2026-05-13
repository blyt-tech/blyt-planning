# Spike S — implementation plan

**Question (per `docs/design/early-validation-spikes.md` §S):** Does the
hardware trusted native-exec path (ADR-0119) work in practice? Concretely:
can a LP64 launcher exec an ILP32 cart binary natively, produce a cart
process with `seccomp_data.arch = AUDIT_ARCH_RISCV32`, enforce an
arch-dispatch seccomp filter across the exec boundary, and have
`libblyt32.so`'s constructor install a restrictive phase-2 filter before
any cart code runs?

**Status:** Stages 0–1 PASS (2026-05-12/13). Stages 2–4 pending.

---

## What we already know from Spike R

Spike R (2026-05-11) on Fedora 42 (kernel 6.16.4, no ILP32 ELF loader)
established:

- Raw BPF seccomp filter construction and installation via
  `seccomp(SECCOMP_SET_MODE_FILTER)` works correctly on RISC-V.
- **LIFO multi-filter semantics confirmed.** A later-installed ALLOW
  passes to an earlier-installed KILL; a later KILL overrides an earlier
  ALLOW. Option B (launcher phase-1, libblyt32.so constructor phase-2) is
  viable.
- `libseccomp` is not usable (no `SCMP_ARCH_RISCV32`; raw BPF is
  required).
- The Fedora 42 kernel **lacks the RISC-V ILP32 ELF binary loader**:
  `execve` of an ILP32 ELF returns ENOEXEC. Stages 2–4 of Spike R used
  rv32emu as a fallback. That path is correct for emulated targets
  (desktop, Pi, WASM) but does not exercise the hardware trusted-exec
  path.
- The production rv32emu LP64 host allowlist (23 syscalls,
  `seccomp_allowlist.h`) is complete for the emulated-target path.

Spike S picks up exactly where Spike R could not go: it requires a
kernel that can natively load ILP32 ELF binaries.

---

## The hardware trusted-exec path (ADR-0119)

The design validated by this spike:

1. **Launcher (LP64 process, AUDIT_ARCH_RISCV64):**
   - Validates cart binary pre-exec (ADR-0112 checks, file-based).
   - Installs a **phase-1** arch-dispatch seccomp filter covering both
     RISCV64 (launcher syscalls) and RISCV32 (ld.so startup syscalls
     needed by the cart process after exec).
   - Sets `LD_LIBRARY_PATH` to the runtime library directory.
   - Calls `execve` on the cart binary.

2. **OS exec + ld.so (ILP32 process, AUDIT_ARCH_RISCV32):**
   - Kernel loads the ILP32 ELF natively via the compat ELF loader.
   - ld.so resolves `libblyt32.so` (and `libblytc.so`, `libblyt32lua.so`
     if declared). These syscalls are permitted by phase-1.

3. **libblyt32.so constructor (still RISCV32, before cart entry):**
   - Installs a **phase-2** seccomp filter covering only RISCV32.
   - Phase-2 blocks `execve`, `socket`, and other post-exec sensitive
     calls.
   - LIFO: phase-2 KILL on those calls overrides phase-1 ALLOW.

4. **Cart entry point:** runs under the combined phase-1 + phase-2
   filter. The cart can only make the syscalls permitted by phase-2's
   RISCV32 allowlist.

---

## Test environment

Spike S requires a RISC-V kernel with the ILP32 ELF binary loader
(`CONFIG_COMPAT` with the RISC-V ILP32 compat ELF loader, not just the
`compat_sys_*` wrappers that Fedora 42 has). Two options:

**Option 1 — Cross-compiled kernel + Fedora 42 rootfs in QEMU (recommended).**
Cross-compile a patched RISC-V kernel on the Apple Silicon host targeting
`riscv64`; boot it with the existing Fedora 42 Cloud rootfs image from
Spike R. Cross-compilation on the host is much faster than building
inside the QEMU guest. The Spike R QEMU boot/SSH infrastructure is
reused unchanged.

The RISC-V ILP32 kernel support exists as a patchset; check the
kernel's `arch/riscv/` tree for the ILP32 compat ELF loader and enable
it in the kernel config alongside `CONFIG_COMPAT=y`.

**Option 2 — Real RISC-V hardware.**
A board whose vendor kernel includes ILP32 support (e.g. VisionFive 2
with StarFive vendor kernel). Simultaneously validates on real silicon.
Higher setup cost; appropriate once Option 1 confirms the mechanism.

The spike proceeds with Option 1 unless a hardware board is immediately
available. Option 2 is a follow-up validation step, not a blocker.

### ILP32 userspace requirement

The ILP32 ELF loader is only half the picture. For dynamic binaries the
kernel also needs an ILP32 dynamic linker at the `PT_INTERP` path the
cart binary specifies (e.g. `/lib/ld-linux-riscv32-ilp32d.so.1` or the
musl equivalent). This is not present in a stock Fedora 42 rootfs.
Stage 0 determines what is needed and how to obtain it (typically:
extract from the cross-toolchain sysroot or install an ILP32 userspace
package).

---

## Approach

### Stage 0 — Environment: confirm native ILP32 exec

1. Boot the ILP32-capable kernel with the Fedora 42 rootfs.

2. Confirm the ILP32 ELF loader is active:
   ```bash
   file spikes/spike-h/build/adversary   # RV32 musl static ilp32d
   ./spikes/spike-h/build/adversary      # must run without ENOEXEC
   ```
   The spike-h adversary is statically linked (no PT_INTERP) — a clean
   baseline that requires only the ELF loader, not a dynamic linker.

3. Confirm `seccomp_data.arch` for an exec'd ILP32 process. Write a
   minimal LP64 test that:
   - Installs a raw BPF filter with two rules: allow all if
     `arch == RISCV32`; KILL if arch != RISCV32.
   - `fork` + `execve` the adversary.
   - Adversary `write` probe → exit 0 (RISCV32 filter allowed it).
   - Confirms `AUDIT_ARCH_RISCV32 = 0x400000F3` is seen for ILP32
     processes.

4. Stage 0 gate: static ILP32 binary executes natively; RISCV32 arch
   confirmed.

---

### Stage 1 — Arch-dispatch filter across the exec boundary

Build `seccomp_raw_test_s.c`, an LP64 test binary that:

5. Constructs a raw BPF arch-dispatch filter:
   ```
   if arch == RISCV64: apply LP64 rules (allow write + exit_group;
                                         block socket + execve)
   if arch == RISCV32: apply ILP32 rules (allow write + exit_group;
                                          block socket + execve)
   else:               KILL_PROCESS
   ```

6. Installs the filter, then `fork` + `execve` the static ILP32
   adversary for each probe:
   - **socket probe** → SIGSYS (RISCV32 rule blocks it). Gate: rc=159.
   - **write probe** → exit 0 (RISCV32 rule allows it). Gate: rc=0.
   - **execve probe** → SIGSYS (RISCV32 rule blocks it). Gate: rc=159.

7. Also test from the LP64 side (before exec) to confirm the RISCV64
   rules apply to the launcher:
   - LP64 write → allowed.
   - LP64 socket → SIGSYS.

8. Stage 1 gate: all six probes produce the expected result. The
   arch-dispatch filter correctly applies different rules to the LP64
   launcher and the ILP32 cart process using a single installed filter.

---

### Stage 2 — ld.so startup syscall characterisation (phase-1 scope)

The phase-1 filter must permit the syscalls ld.so makes during ILP32
dynamic-library resolution. These are distinct from cart-code syscalls
and must be characterised empirically.

9. Obtain or build the ILP32 dynamic linker (ld.so) and place it at the
   path the test binary's PT_INTERP specifies. Extract from the
   cross-toolchain sysroot used by Spike I.

10. Build a minimal ILP32 stub `libblyt32_stub.so`:
    - Declares the expected DT_SONAME (`libblyt32.so`).
    - Exports stub no-op symbols for the subset the test cart imports.
    - No constructor yet (added in Stage 4).

11. Build a minimal ILP32 test cart binary with `PT_INTERP` and
    `DT_NEEDED libblyt32.so`. Use the spike-h adversary source as a base;
    add `-dynamic` and link against the stub.

12. Run under `strace` from `execve` through `main()` entry, capturing
    all syscalls made during dynamic linking:
    ```bash
    strace -f -e trace=all -o ld_syscalls.txt \
        /path/to/test_cart_dynamic socket
    ```

13. Separate ld.so-phase syscalls from post-main syscalls (using
    timestamp or marker call). Produce `phase1_ld_syscalls.txt` — the
    set the phase-1 filter must permit for RISCV32.

14. Stage 2 gate: dynamic ILP32 cart binary starts and reaches `main()`
    without SIGSYS; ld.so syscall set characterised.

---

### Stage 3 — ILP32 native cart syscall allowlist (phase-2 scope)

15. Run each spike-i and spike-q cart workload natively under `strace`,
    using the stub `libblyt32.so`. Run each for at least 5 seconds:
    ```bash
    strace -f -e trace=all -o strace_cart_a.txt ./cart_a
    strace -f -e trace=all -o strace_cart_b.txt ./cart_b
    strace -f -e trace=all -o strace_rust_q.txt ./rust_cart.elf
    ```

16. Subtract the ld.so-phase syscalls (Stage 2) from each trace.
    Collect the union of remaining syscalls: these are the syscalls
    native ILP32 cart code requires.

17. Verify empirically that RISC-V ILP32 and LP64 use the same syscall
    NRs (expected: yes — RISC-V uses a unified syscall table with no
    ILP32-specific compat NRs like `mmap2` or `clock_gettime64`). If any
    ILP32-specific NRs appear, document and allow them explicitly.

18. Write `seccomp_allowlist_s.h` defining:
    - `seccomp_phase1_riscv32_nrs[]` — ld.so startup syscalls (Stage 2)
    - `seccomp_phase2_riscv32_nrs[]` — cart-code syscalls (Stage 3)
    - `seccomp_phase1_riscv64_nrs[]` — launcher syscalls (from existing
      `seccomp_allowlist.h`, Spike R Stage 3)

19. Stage 3 gate: `seccomp_allowlist_s.h` committed; per-syscall
    comments explain why each NR is needed.

---

### Stage 4 — Option B end-to-end: libblyt32.so constructor phase-2

20. Update `libblyt32_stub.so` to add a constructor that installs the
    phase-2 RISCV32 filter:
    ```c
    __attribute__((constructor))
    static void install_phase2(void) {
        /* Build RISCV32 filter from seccomp_phase2_riscv32_nrs[].
         * This runs after ld.so has resolved all symbols.
         * Phase-1 is already installed; LIFO means phase-2 KILL
         * takes precedence over phase-1 ALLOW for RISCV32 syscalls
         * not in the phase-2 allowlist. */
        install_raw_bpf_filter_riscv32(phase2_filter, phase2_len);
    }
    ```

21. Build `launcher_s.c` (LP64):
    - Installs arch-dispatch phase-1 filter: RISCV64 rules for launcher;
      RISCV32 rules permissive for ld.so (Stage 2 set).
    - Sets `LD_LIBRARY_PATH` and `execve`s the cart binary.

22. Run the complete adversary verification through `launcher_s`:
    - **socket probe (RISCV32)** → SIGSYS. Gate: rc=159.
    - **execve probe (RISCV32)** → SIGSYS. Gate: rc=159.
    - **write probe (RISCV32)** → exit 0. Gate: rc=0.

23. Run full cart workloads (spike-i case_a, case_b; spike-q rust_cart):
    - Cart executes without unexpected SIGSYS.
    - strace confirms no syscalls outside the allowlist are attempted.
    - Gate: rc=0 (clean exit) or rc=124 (timeout, still running — no
      SIGSYS). Same gate as Spike R Stage 4.

24. Commit `seccomp_allowlist_s.h`. Write results to
    `docs/design/spike-s-results.md`.

---

## Risk notes

- **Kernel availability.** The RISC-V ILP32 compat ELF loader is a
  patchset that may not be in a readily available kernel image. Building
  a cross-compiled kernel is the recommended path (avoids the multi-hour
  in-guest build). Confirm the patchset's current upstream status before
  starting.

- **ILP32 dynamic linker in rootfs.** The Fedora 42 rootfs almost
  certainly does not include an ILP32 ld.so. It must be sourced from the
  cross-toolchain sysroot (the spike-i Docker image has the ILP32
  toolchain; the ld.so can be extracted from there). Confirm the
  `PT_INTERP` path matches what the cross-toolchain embeds in ILP32
  binaries.

- **Syscall NR unification.** RISC-V is designed with a unified syscall
  table; ILP32 should use the same NRs as LP64. Verify this empirically
  in Stage 3 — if any ILP32-specific compat NRs appear they change the
  allowlist scope.

- **Phase-2 constructor ordering.** The libblyt32.so constructor must
  run before any cart-code constructors. ELF constructor order within a
  process is: dynamic dependencies' constructors before the binary's own
  constructors, in DT_NEEDED order. Since the cart DT_NEEDs libblyt32.so,
  libblyt32.so's constructor runs first. Verify this empirically.

- **PR_SET_NO_NEW_PRIVS.** The launcher must call
  `prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)` before installing phase-1.
  Confirmed necessary in Spike R; repeat here.

- **LIFO across exec.** Spike R confirmed LIFO semantics on the same
  process. Spike S introduces a filter installed before exec (phase-1,
  by LP64 launcher) and a filter installed after exec (phase-2, by ILP32
  constructor). The kernel preserves installed filters across exec
  (`seccomp(2)`: "filters are inherited across `execve(2)`"). Verify
  this explicitly in Stage 4 before trusting the end-to-end result.

---

## Deliverables

- `spikes/spike-s/seccomp_raw_test_s.c` — Stage 1: arch-dispatch filter
  proof of concept across the exec boundary.
- `spikes/spike-s/launcher_s.c` — Stage 4: LP64 launcher installing
  phase-1 and exec'ing the cart.
- `spikes/spike-s/libblyt32_stub_s.c` → `libblyt32_stub.so` — ILP32
  stub with phase-2 constructor.
- `spikes/spike-s/seccomp_allowlist_s.h` — Stage 3: RISCV32 phase-1
  (ld.so), phase-2 (cart-code), and RISCV64 launcher allowlists. Primary
  deliverable.
- `spikes/spike-s/Makefile` — cross-compile LP64 and ILP32 binaries;
  in-guest test targets.
- `spikes/spike-s/run-guest-stage*.sh` — in-guest scripts for each
  stage (mirroring Spike R's structure).
- `spikes/spike-s/TASKS.md` — per-step checklist.
- `docs/design/spike-s-results.md` — per-stage pass/fail, syscall
  findings, committed allowlist, open items.

---

## Open items deferred

- **Hardware validation.** Once the mechanism is confirmed under QEMU
  (Option 1), repeat Stage 3 strace on real RISC-V silicon (Option 2)
  to confirm the allowlist is complete on native hardware. The QEMU host
  syscall path may differ from the bare-metal path.
- **`SECCOMP_FILTER_FLAG_TSYNC`.** Needed if libblyt32.so or the cart
  runner ever uses multiple threads. Not needed for the single-threaded
  v1 path.
- **execve argument filter.** Phase-1 allows `execve` for the LP64
  launcher only (RISCV64 arch rules). Further restricting which paths
  are exec-able is a production hardening task, not a spike concern.
- **libblytc.so + libblyt32lua.so in the stub.** This spike uses a
  minimal stub. A follow-up validates the full DT_NEEDED set
  (`libblyt32.so`, `libblytc.so`, `libblyt32lua.so`) with all three
  libraries present and their phase-1 syscall contributions captured.
