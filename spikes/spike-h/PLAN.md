# Spike H — implementation plan

**Question (per `docs/design/early-validation-spikes.md` §H):** Can a
RV32IMFC cart ELF run natively on an RV64 Linux kernel, be adequately
isolated from the host system using OS-level mechanisms, and have its CPU
budget capped to match the performance floor of the minimum emulation host?

**Status (2026-05-04):** QEMU full-system runs complete on both Ubuntu 24.04.4
and Fedora 42 RISC-V. All three kernel mechanisms are proven on Fedora 42.
Stages 0–4 pass. One seccomp probe (`uname`) gives SIGSYS instead of 0 due
to a libseccomp gap around ILP32 compat arch handling (deferred to production).
Results in `docs/design/spike-h-results.md`; per-step detail in `TASKS.md`.

The spike has three live stages: Stage 1 (RV32 on RV64 kernel), Stage 3
(seccomp + namespace isolation), and Stage 4 (cgroups v2 CPU quota). Stage 2
(IPC ring buffer) was superseded by ADR-0024's controlled dynamic linking
before the spike ran — see "What has changed" below.

---

## What has changed since the spec was written

**Stage 2 is gone.** The original Stage 2 planned an IPC ring-buffer
mechanism for `libconsole` because `ecall` in a native RISC-V process is a
Linux syscall, not a console API call. ADR-0024 resolves this by providing
`libconsole.so` as a real shared library mapped into the cart process before
execution. Console API calls are direct function calls; no IPC is needed.
Stage 2's success criterion (IPC round-trip ≤ 10 µs) is trivially satisfied
(function-call overhead is single-digit nanoseconds). The `libconsole.so`
load mechanism is validated end-to-end by Spike I, not here.

**Stage 3's seccomp allowlist is simpler.** The original spec listed `futex`
as an expected cart syscall (for the ring buffer). With direct
`libconsole.so` calls, cart code issues no syscalls at runtime. The
allowlist is derivable from strace of the process startup sequence alone,
which is small and well-understood.

**Platform.** Real K230D hardware is the final validation target.
Initial development and mechanism verification uses QEMU full-system
emulation of RISC-V64 (`qemu-system-riscv64`), which correctly exercises
`CONFIG_COMPAT`, seccomp-bpf, Linux namespaces, and cgroups v2 at the kernel
level. QEMU user-mode (`qemu-riscv64`) is unsuitable — it handles syscalls
at the host level and does not exercise any of these kernel mechanisms.

---

## Inputs we already have

- **Spike A CoreMark reference**: the MIPS cap placeholder is 500 MIPS (Pi
  Zero 2 W real-hardware measurement still pending). Stage 4 uses this as
  the reference throughput; the formula works with any value substituted
  once the Pi number lands.
- **Spike B Lua workloads**: `lua_cart_doom_tick.elf` and
  `lua_cart_entity_update.elf` are the Stage 4 validation workloads. They
  are currently bare-metal RV32IMFC ELFs for `rv32emu`. A native-Linux port
  (same Lua source, different C runtime) is needed for running directly as a
  Linux process. See Stage 4.
- **rv32emu**: already integrated in spike-a; available to run the existing
  Spike B ELFs under the emulator for comparison timing in Stage 4.

---

## What we are NOT building

- **Stage 2 (IPC ring buffer)**. Superseded by ADR-0024.
- **`libconsole.so` implementation**. The library is a stub for testing;
  its real implementation and load mechanics are Spike I's scope.
- **Full cart format integration**. Spike H proves the OS mechanisms are
  sound. Spike I runs actual cart ELFs through the full format pipeline.
- **Complete production seccomp allowlist**. The full runtime syscall set is
  unknown until the runtime is implemented. The spike establishes that a
  tight allowlist is sound in principle and confirms the minimal set for
  the test harness.
- **Real K230D hardware numbers**. QEMU validates the mechanism; the
  hardware-specific constants (CoreMark/MHz for the C908, cpu.max quota for
  the K230D) are a follow-on measurement once the board is available.
- **Pi Zero 2 W numbers**. Still pending from Spike A; Stage 4 uses the
  500 MIPS placeholder and is designed to substitute the real number when
  it arrives.

---

## Approach

### Stage 0 — QEMU RV64 environment

The goal is a `qemu-system-riscv64` VM running a Linux kernel with
`CONFIG_COMPAT` enabled and cgroups v2 mounted.

**Chosen image: Fedora 42 Cloud RISC-V.** Ubuntu 24.04.4's kernel
(6.17.0-14-generic) has `CONFIG_COMPAT` explicitly disabled — RV32 ELFs
fail with `ENOEXEC`. Fedora 42's kernel (6.16.4-200.0.riscv64.fc42)
compiles in ILP32 compat support unconditionally; `compat_sys_*` symbols
are present in `/proc/kallsyms` even though the config file shows
`# CONFIG_COMPAT is not set`. Fedora is the correct QEMU guest for all
mechanism validation.

1. Download the Fedora 42 Cloud RISC-V image. Boot under
   `qemu-system-riscv64` with OpenSBI firmware and the Ubuntu U-Boot ELF
   (compatible, extracted from the ubuntu-riscv64 package). Fedora boots
   via GRUB EFI.

2. Inside the guest, verify compat support:
   ```
   grep compat_sys /proc/kallsyms | head -5
   # expect: non-empty output confirming compat_sys_* symbols present
   ```
   Fedora's config file reports `# CONFIG_COMPAT is not set` but the
   kernel has it compiled in — trust kallsyms, not the config file.

3. Verify cgroups v2:
   ```
   mount | grep cgroup2
   cat /sys/fs/cgroup/cgroup.controllers
   # expect: cpuset cpu io memory hugetlb pids rdma misc dmem
   ```
   Both Fedora 42 and Ubuntu 24.04.4 have cgroups v2 mounted under systemd.

4. Install the RV32 Linux cross-toolchain on the host build machine (not
   inside the guest). The target is `riscv32-linux-musl` (musl is lighter
   and avoids glibc ABI versioning concerns). The musl.cc prebuilt
   (`riscv32-linux-musl-cross.tgz`) is the reliable source; it is
   installed into the Docker build image.

   Note: the musl.cc prebuilt ships libgcc + libc only for `rv32gc/ilp32d`
   (double-float ABI), not `rv32imfc/ilp32f` (single-float). `hello.S`
   has no libc dependency and can target `ilp32f` exactly, matching the
   cart spec. All other binaries (adversary, busy_loop, Lua workloads)
   that link against musl use `ilp32d` — ABI alignment with the cart spec
   requires a custom toolchain or the K230D vendor rv64ilp32 toolchain.
   Document this divergence; it is out of spike scope.

5. Confirm host toolchain versions:
   - `qemu-system-riscv64` ≥ 8.0 (tested with 11.0.0 via Homebrew)
   - Docker image: Ubuntu 24.04 arm64 base, `riscv32-linux-musl` gcc 11.2.1,
     `gcc-riscv64-linux-gnu` for the RV64 launcher

---

### Stage 1 — RV32 execution on RV64 kernel

Build a minimal RV32IMFC Linux ELF and confirm it runs natively under the
Fedora 42 kernel.

6. Write `hello.S`: a bare RV32 assembly program that writes `"OK\n"` to
   stdout and calls `exit_group`. Using raw syscalls (ECALL) avoids any
   C runtime dependency and keeps the binary trivially small:

   ```asm
   .section .text
   .globl _start
   _start:
       li   a7, 64          # __NR_write
       li   a0, 1           # fd = stdout
       la   a1, msg
       li   a2, 3           # len
       ecall
       li   a7, 94          # __NR_exit_group
       li   a0, 0
       ecall

   .section .rodata
   msg:
       .ascii "OK\n"
   ```

   Assemble and link as a static RV32IMFC ELF:
   ```
   riscv32-linux-musl-gcc -nostdlib -march=rv32imfc_zicsr -mabi=ilp32f \
       -static -o hello hello.S
   file hello       # confirm: ELF 32-bit LSB executable, RISC-V
   ```

7. Copy `hello` to the QEMU guest (via virtfs or scp) and run it:
   ```
   ./hello && echo "Stage 1: PASS"
   ```
   On Fedora 42: output `OK`, exit 0 — PASS.
   On Ubuntu 24.04.4: `Exec format error` (ENOEXEC) — FAIL. Use Fedora.

8. Verify the ELF headers match cart spec:
   ```
   readelf -h hello | grep -E 'Class|Machine|Flags'
   # ELFCLASS32, EM_RISCV, EF_RISCV_RVC|EF_RISCV_FLOAT_ABI_SINGLE
   ```

---

### Stage 3 — seccomp + namespace isolation

A C launcher program forks a child, applies namespaces and a seccomp-bpf
filter, then runs a test ELF in the child. The test ELF probes both the
permitted and forbidden syscall surfaces.

The launcher runs as an RV64 native process inside the guest (not
cross-compiled from the host Docker image, since it needs `libseccomp-dev`
matching the guest's riscv64-linux-gnu ABI). It is built inside the guest
or via `make docker-build-launcher` (a separate Docker stage with
`gcc-riscv64-linux-gnu`).

9. Write `launcher.c`. The launcher:
   - Forks.
   - In the child:
     1. Calls `unshare(CLONE_NEWNS | CLONE_NEWNET)` to enter new mount and
        network namespaces.
     2. Runs the `pivot_root` sequence to isolate the filesystem (see
        step 13 and risk notes).
     3. Installs a seccomp-bpf filter (see step 10).
     4. Execs the test ELF.
   - In the parent: `waitpid`, report exit status and signal.

   The `unshare` calls happen before seccomp so the filter does not need to
   allow `unshare` itself.

10. Derive the allowlist. Before writing the filter, strace `hello` inside
    the guest to see what syscalls process startup requires:
    ```
    strace -e trace=all ./hello 2>&1 | grep -v 'OK'
    ```
    On a minimal static musl binary this typically includes:
    `brk`, `mmap2`, `write`, `exit_group`, and `riscv_flush_icache`
    (syscall number 258/259 — musl RV32 startup flushes the icache;
    this must be in the allowlist or the process is killed during startup).

    The allowlist (from actual strace output on Fedora 42):
    - `exit_group` — process termination
    - `rt_sigreturn` — signal frame unwinding
    - `brk` — heap initialisation
    - `mmap2` / `mmap` — initial stack/heap setup
    - `write` — console output
    - `riscv_flush_icache` (258 + 259) — musl RV32 startup; must be present
    - `clock_gettime` — if not served by vDSO
    - `uname` — allowed for the sanity-check probe

11. Write the seccomp filter using `libseccomp` (`seccomp_init` /
    `seccomp_rule_add` / `seccomp_load`). Default action `SCMP_ACT_KILL`
    (SIGSYS); add `SCMP_ACT_ALLOW` rules for the allowlist.

    **ILP32 compat arch limitation:** `libseccomp` does not define
    `SCMP_ARCH_RISCV32`. A filter installed by an LP64 launcher uses
    `AUDIT_ARCH_RISCV64`. When an RV32 compat process makes a syscall, the
    BPF program sees `AUDIT_ARCH_RISCV32` — since no rules match this arch,
    the default action (KILL_PROCESS) applies to all RV32 syscalls. This
    means:
    - All forbidden probes (open, socket, execve, mprotect-exec) return
      SIGSYS — correct outcome, wrong mechanism (arch mismatch, not rule).
    - The `uname` allowed probe also returns SIGSYS — incorrect.

    Production fix: write raw BPF that handles both `AUDIT_ARCH_RISCV64`
    (LP64 launcher side) and `AUDIT_ARCH_RISCV32` (ILP32 cart side). The
    spike establishes the allowlist; the production implementation requires
    a custom BPF filter or a libseccomp patch to add `SCMP_ARCH_RISCV32`.

12. Write `adversary.c`: a small RV32 program that attempts each blocked
    syscall in sequence and reports whether it received SIGSYS or ENOSYS:
    - `open("/etc/passwd", O_RDONLY)` — blocked (filesystem access)
    - `socket(AF_INET, SOCK_STREAM, 0)` — blocked (networking)
    - `execve("/bin/sh", NULL, NULL)` — blocked (code injection)
    - `mprotect(addr, len, PROT_EXEC)` — blocked (code injection via new
      exec pages)
    - `uname(buf)` — allowed (sanity check)

    Run `adversary` through the launcher. Each forbidden syscall should
    produce SIGSYS (exit code 159 on Fedora 42). The `uname` probe should
    exit 0; on the QEMU run it exits 159 due to the arch mismatch noted
    above — document this as a production gap, not a mechanism failure.

13. Verify mount namespace isolation. `CLONE_NEWNS` alone does not hide the
    host filesystem — the child inherits the parent's mount tree. A full
    namespace requires:
    1. Mount a tmpfs at a temp directory.
    2. Bind-mount the essential binaries needed for exec into the tmpfs.
    3. Call `pivot_root(new_root, put_old)`.
    4. Unmount the old root with `umount2(put_old, MNT_DETACH)`.

    After this sequence, the cart process cannot see `/etc/passwd` or any
    other host path. Verify:
    ```
    # inside the isolated rootfs, attempting open("/etc/passwd") returns ENOENT
    ```

14. Verify network namespace isolation: `unshare(CLONE_NEWNET)` puts the
    child in a namespace with no interfaces. Combined with the seccomp
    filter blocking `socket`, two independent mechanisms block networking.

---

### Stage 4 — cgroups v2 CPU quota

Verify that cgroups v2 is available, that `cpu.max` throttles the cart
process proportionally, and that the CoreMark-based calibration formula
produces a reasonable quota value.

15. Verify cgroups v2 is writable:
    ```
    mkdir /sys/fs/cgroup/spike-h-test
    echo "+cpu" | tee /sys/fs/cgroup/cgroup.subtree_control
    echo "50000 500000" > /sys/fs/cgroup/spike-h-test/cpu.max
    ```
    Note: the CFS bandwidth quota minimum is 1 ms. The original plan used
    `500 5000` (100 µs quota) which is rejected by the kernel. Use
    `50000 500000` (50 ms in a 500 ms period = 10% CPU) or larger.

16. Confirm the throttle works at all. Write a simple RV32 busy-loop
    (`busy_loop.c`). Run it outside a cgroup (measure wall time via `time`).
    Then run it inside a cgroup with `cpu.max "50000 500000"` (10% CPU).
    The throttled run should take ~10× longer.

    Observed on Fedora 42 / QEMU: baseline 7.2 s → throttled 85.1 s = 11.8×
    (close to 10× expected). Mechanism confirmed.

17. Calibrate against Spike A. Compile CoreMark for RV32 Linux:
    ```
    riscv32-linux-musl-gcc -O2 -march=rv32imfc_zicsr -mabi=ilp32f \
        -static -o coremark_rv32 [coremark sources]
    ```
    Run in the QEMU guest (no cgroup), record the `CoreMark/MHz` score and
    derive `measured_MIPS`. Compute the quota:
    ```
    quota_us = floor(period_us × Pi_MIPS / measured_MIPS)
    # Pi_MIPS = 500 (placeholder; substitute Spike A real number when available)
    # period_us = 50000 (50 ms period for the CFS bandwidth throttle)
    ```
    Write `cpu.max "quota_us 50000"` to the test cgroup.

    Note: on QEMU, `measured_MIPS` reflects QEMU's emulation speed on the
    host machine, which is not representative of real C908 silicon. The QEMU
    run validates that the calibration formula and quota write work correctly;
    the actual Pi_MIPS / C908_MIPS ratio and the resulting K230D constant
    are hardware measurements deferred to when the board is available.

    The three calibration options from the spec (in ascending order of
    simplicity): per-boot measurement, per-installation measurement, baked
    constant per known hardware target. For the K230D a baked constant
    is the simplest and is the recommended production approach — run once on
    real hardware, publish the `cpu.max` pair in the image config.

18. Port the Spike B Lua workloads to native Linux RV32. The Spike B ELFs
    are bare-metal (custom crt0, ECALL I/O, freestanding Lua). For Stage 4
    they need to run as native Linux processes:
    - Replace `syscalls.c`'s raw ECALL write/exit with musl libc equivalents.
    - Replace the ECALL `clock_gettime` with the musl `clock_gettime`.
    - Keep the rest of the Lua VM and benchmark scripts unchanged.
    - The build lives in `spikes/spike-h/lua-workloads/` and does not
      modify spike-b's directory.

    Run `doom_tick` and `entity_update` (as native Linux processes) inside
    the calibrated cgroup. Compare their per-frame mean times against the
    Spike B Docker arm64 baseline (used as Pi proxy in the G-series spikes).
    The throttled native-Linux times should be in the same ballpark as the
    rv32emu-under-Docker times, confirming the mechanism delivers meaningful
    budget pressure.

    Observed at 20% CPU quota on Fedora 42 / QEMU:
    - `doom_tick` mean = 1,713 ms (vs 13.2 ms unthrottled under qemu-user)
    - `entity_update` mean = 778 ms (vs 6.0 ms unthrottled)

    Exact numeric matching against Pi is not required — QEMU calibration
    does not predict real C908 performance. Hardware-accurate numbers follow
    from running the same procedure on the K230D.

---

## Risk notes

- **`CONFIG_COMPAT` absent in the Ubuntu image.** Confirmed: Ubuntu 24.04.4
  kernel 6.17.0-14-generic has `CONFIG_COMPAT` explicitly disabled. Fedora 42
  kernel 6.16.4 has it compiled in unconditionally (compat_sys_* present in
  kallsyms despite config saying `not set`). The cart runtime should target
  Fedora or a buildroot image that explicitly enables `CONFIG_COMPAT`.

- **RV32 Linux toolchain ABI mismatch.** The musl.cc prebuilt targets
  `rv32gc/ilp32d` (double-float ABI). Only `hello.S` (which has no libc
  dependency) can be built with the cart-spec `rv32imfc/ilp32f`. All other
  spike binaries use `ilp32d`. Full ABI alignment requires a custom
  toolchain build or the K230D vendor rv64ilp32 toolchain. Out of spike scope.

- **seccomp compat syscalls on RV32 (RESOLVED IN SPIKE).** The compat layer
  introduces `riscv_flush_icache` (syscall 258/259) which musl RV32 calls
  during startup. It must be in the allowlist. Confirmed and fixed in
  `seccomp_filter.c`.

- **`SCMP_ARCH_RISCV32` not in libseccomp.** The LP64 seccomp filter sees
  all ILP32 syscalls as having an unknown arch and kills them via the default
  action. This makes the filter over-permissive from one angle (forbidden
  syscalls are blocked but by arch mismatch, not by explicit deny rule) and
  under-permissive from another (allowed syscalls like `uname` are also
  killed). Production fix: custom BPF filter with explicit `AUDIT_ARCH_RISCV32`
  handling. Document this as a deferred production item, not a spike blocker.

- **Mount namespace `pivot_root` complexity.** `CLONE_NEWNS` alone does not
  hide the host filesystem. A full namespace requires `pivot_root` to a tmpfs
  plus `umount2(MNT_DETACH)`. Implemented and confirmed working in the QEMU
  run: `/etc/passwd` returns `ENOENT` inside the isolated rootfs.

- **cgroups v2 quota minimum.** The kernel enforces a 1 ms minimum quota in
  the CFS bandwidth controller. `cpu.max "500 5000"` (100 µs in 1 ms) is
  rejected. Use `50000 500000` (50 ms in 500 ms) or equivalent. Confirmed.

- **QEMU cgroup numbers are not hardware-representative.** The cpu.max quota
  derived from QEMU CoreMark does not predict anything about a real C908.
  Stage 4 validates the mechanism; record the QEMU numbers with a clear
  note that hardware-accurate constants require K230D measurement.

- **Spike B bare-metal port to Linux ABI.** The Lua VM in spike-b uses a
  custom `malloc`/`free` over a static heap, a hand-rolled `snprintf`, and
  ECALL I/O. Porting to musl requires replacing the I/O and `clock_gettime`
  shims. Keep the port minimal — the goal is a working timing run, not a
  clean integration.

---

## Deliverables

- `spikes/spike-h/hello.S` — Stage 1: minimal RV32IMFC Linux ELF.
- `spikes/spike-h/adversary.c` — Stage 3: forbidden-syscall probe program.
- `spikes/spike-h/launcher.c` — Stage 3/4: the fork/unshare/seccomp/exec
  harness.
- `spikes/spike-h/seccomp_filter.c` — seccomp-bpf filter construction
  (linked into launcher).
- `spikes/spike-h/busy_loop.c` — Stage 4: pure-CPU busy-loop for throttle
  validation.
- `spikes/spike-h/lua-workloads/` — Stage 4: `doom_tick` and
  `entity_update` ported to native Linux RV32.
- `spikes/spike-h/run-guest-tests.sh` — automated in-guest test script
  (Stages 0–4); executed via SSH by `make qemu-test`.
- `spikes/spike-h/Makefile` — builds all of the above; targets for each
  stage, plus `qemu-test-fedora` for the automated Fedora 42 run.
- `spikes/spike-h/README.md` — QEMU setup instructions, quick-start targets,
  key findings and CONFIG_COMPAT note.
- `spikes/spike-h/baselines/coremark-qemu.txt` — Stage 4 CoreMark result
  and derived quota value; annotated as QEMU-only. (Deferred until hardware
  timing is needed.)
- `docs/design/spike-h-results.md` — write-up: per-stage pass/fail, the
  seccomp allowlist that worked, the mount namespace sequence, the cgroups
  formula, and the open items deferred to real hardware.
- `spikes/spike-h/TASKS.md` — checklist, updated as work proceeds.

---

## Open items deferred to real hardware

The following require a K230D (C908) or equivalent physical RISC-V board with
UXL=32 hardware support. Note: Milk-V Duo / Duo S (C906) cannot substitute —
the C906 lacks UXL=32 and cannot exec ILP32 binaries.

1. **C908 CoreMark/MHz measurement.** The QEMU calibration run validates the
   formula; the actual quota constant for the K230D requires a real
   CoreMark run on the C908 silicon.

2. **`cpu.max` constant for the K230D image.** Once the C908 CoreMark
   and Spike A's Pi Zero 2 W MIPS are known, compute and bake
   `quota_us = floor(50000 × Pi_MIPS / C908_MIPS)` into the buildroot image.

3. **Production seccomp allowlist.** The full runtime syscall set is
   determined once the runtime is implemented. The spike confirms the approach
   is sound; the complete allowlist is an implementation task.

4. **Raw BPF filter for `AUDIT_ARCH_RISCV32`.** libseccomp lacks
   `SCMP_ARCH_RISCV32`; the production filter must be written as raw BPF
   bytecode (or libseccomp must be patched) to handle both the LP64 launcher
   and the ILP32 cart process correctly.

5. **Cart-spec ABI (`ilp32f`) Lua workloads.** The spike's Lua binaries use
   `ilp32d` due to toolchain constraints. Running the benchmark suite with
   a properly spec-aligned `ilp32f` binary requires either a custom toolchain
   or the K230D vendor rv64ilp32 toolchain.
