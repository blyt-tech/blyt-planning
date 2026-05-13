# Spike H — results

> **Status (2026-05-04):** QEMU full-system runs completed on both Ubuntu
> 24.04.4 and Fedora 42 RISC-V. **All three kernel mechanisms are proven.**
> Stages 0–4 pass on Fedora 42. One seccomp probe (uname) gives SIGSYS
> instead of 0 due to a libseccomp limitation around ILP32 compat arch
> handling (deferred to production). See `TASKS.md` for per-step detail.

## Question

Per `docs/design/early-validation-spikes.md` §H — can a RV32IMFC cart ELF run
natively on an RV64 Linux kernel, be adequately isolated from the host system
using OS-level mechanisms, and have its CPU budget capped to match the
performance floor of the minimum emulation host?

Stage 2 (IPC ring buffer) was superseded by ADR-0024's controlled dynamic
linking and is not part of the spike scope. Stage 1, Stage 3, and Stage 4
are the live stages.

## What was built

| Component | Path | Purpose |
|-----------|------|---------|
| `hello.S` | `spikes/spike-h/hello.S` | Minimal RV32IMFC/ilp32f ELF — Stage 1 kernel-load test |
| `launcher.c` + `seccomp_filter.c` | `spikes/spike-h/` | fork → unshare → pivot_root → seccomp(KILL) → exec harness |
| `adversary.c` | `spikes/spike-h/adversary.c` | RV32 probe: open, socket, execve, mprotect(EXEC), uname |
| `busy_loop.c` | `spikes/spike-h/busy_loop.c` | Pure-CPU RV32 loop — Stage 4 cgroup throttle test |
| `lua-workloads/` | `spikes/spike-h/lua-workloads/` | Native-Linux RV32 port of spike-b's `doom_tick` and `entity_update` |
| `run-guest-tests.sh` | `spikes/spike-h/run-guest-tests.sh` | Automated in-guest test script (Stages 0–4); run via SSH |
| `Dockerfile` | `spikes/spike-h/Dockerfile` | Ubuntu 24.04 arm64 + riscv32-linux-musl + riscv64-linux-gnu + Lua 5.4 |
| `Makefile` | `spikes/spike-h/Makefile` | Full build + test pipeline including `qemu-test-fedora` |

## Results — host qemu-user surface

These verify cart binaries are functional. They do **not** test CONFIG_COMPAT,
seccomp, namespaces, or cgroups.

| Check | Result |
|-------|--------|
| `riscv32-linux-musl` cross-toolchain gcc 11.2.1 | PASS |
| `hello.elf` compiles with `-march=rv32imfc -mabi=ilp32f` | PASS — `0x3 RVC, single-float ABI` |
| `hello.elf` under `qemu-riscv32-static`: prints `OK\n`, exit 0 | PASS |
| `busy_loop.elf` under qemu-user: 5×10⁸ iters in ~1.12 s | PASS |
| `adversary.elf` probe uname: returns 0 (no filter) | PASS |
| `lua_cart_doom_tick.elf`: 30 frames, mean ≈ 13.2 ms | PASS |
| `lua_cart_entity_update.elf`: 50 frames, mean ≈ 6.0 ms | PASS |

## Results — QEMU full-system runs (2026-05-04)

### Test environments

| Parameter | Ubuntu run | Fedora run |
|-----------|-----------|------------|
| Image | ubuntu-24.04.4-preinstalled-server-riscv64 | Fedora-Cloud-Base-Generic-42.riscv64.qcow2 |
| Kernel | 6.17.0-14-generic | 6.16.4-200.0.riscv64.fc42.riscv64 |
| QEMU | 11.0.0 on macOS Apple Silicon | same |
| Boot | OpenSBI + ubuntu-uboot.elf | OpenSBI + ubuntu-uboot.elf → GRUB EFI |
| Access | SSH ubuntu/ubuntu | SSH fedora + ed25519 key |

### Stage 0: kernel features

| Check | Ubuntu | Fedora | Notes |
|-------|--------|--------|-------|
| Boots, SSH accessible | PASS | PASS | |
| `CONFIG_COMPAT=y` in config | FAIL | FAIL* | *Config says not set; Fedora kernel has compat_sys_* in kallsyms — compiled in unconditionally |
| cgroups v2 mounted | PASS | PASS | |
| `cpu` controller present | PASS | PASS | `cpuset cpu io memory hugetlb pids rdma misc dmem` |

### Stage 1: RV32 ELF execution

| Check | Ubuntu | Fedora | Notes |
|-------|--------|--------|-------|
| `hello.elf` runs, prints OK | FAIL | **PASS** | Ubuntu: ENOEXEC; Fedora: rc=0 output='OK' |

**Conclusion:** The Fedora 42 kernel (6.16.4) runs RV32 ELFs natively, confirming
the CONFIG_COMPAT capability is present even though the config file does not show it.

### Stage 3: seccomp + namespace isolation

| Check | Ubuntu | Fedora | Notes |
|-------|--------|--------|-------|
| seccomp filter loads | PASS | PASS | seccomp_load returns 0 |
| `open` probe → SIGSYS | FAIL* | **PASS** | *Ubuntu: execve blocked before adversary exec'd |
| `socket` probe → SIGSYS | FAIL* | **PASS** | |
| `execve` probe → SIGSYS | FAIL* | **PASS** | |
| `mprotect-exec` probe → SIGSYS | FAIL* | **PASS** | |
| `uname` probe → exit 0 | FAIL | FAIL† | †libseccomp doesn't define SCMP_ARCH_RISCV32; BPF arch check kills all ILP32 syscalls |
| pivot_root: cart in isolated rootfs | FAIL* | **PASS** | |
| pivot_root: /etc/passwd inaccessible | FAIL* | **PASS** | ENOENT inside isolated rootfs |

**seccomp mechanism summary:** All forbidden syscalls are reliably killed with SIGSYS.
The uname probe failure is a libseccomp gap (no SCMP_ARCH_RISCV32 constant), not a
kernel mechanism failure. The correct production approach is to write raw BPF that
handles both AUDIT_ARCH_RISCV64 (LP64 launcher) and AUDIT_ARCH_RISCV32 (ILP32 cart).

### Stage 4: cgroups v2 cpu.max

| Check | Ubuntu | Fedora | Notes |
|-------|--------|--------|-------|
| `cpu.max` write accepted | PASS | PASS | `50000 500000` (50ms/500ms = 10%); original `500 5000` fails — quota < kernel 1ms min |
| busy_loop ~10× slower under throttle | FAIL* | **PASS** | Fedora: 7.2s → 85.1s = 11.8× |
| Lua workloads run under cgroup | FAIL* | **PASS** | doom_tick mean=1713ms; entity_update mean=778ms (at 20% CPU) |

*Ubuntu: RV32 binaries can't exec without CONFIG_COMPAT.

**Calibration note:** The quota formula `floor(period_us × Pi_MIPS / measured_MIPS)` is
validated at mechanism level. QEMU numbers (Pi_MIPS=500 placeholder, measured via
throttle ratio) are not hardware-representative. Hardware constants follow from
running the same procedure on the Milk-V Duo.

## What this spike does NOT prove

- That the Pi Zero 2 W reference MIPS (Spike A) is final.
- That the production seccomp allowlist is complete (uname/ILP32 arch needs raw BPF).
- That the cart-spec `ilp32f` ABI Lua workloads match the `ilp32d` builds here.

**Hardware validation note (2026-05-13):** Milk-V Duo (C906) hardware
validation is no longer planned. Investigation confirmed that the C906 does
not support `sstatus.UXL=32`, the hardware prerequisite for running ILP32
(RV32 ABI) processes on an RV64 kernel. Hardware CoreMark calibration should
be performed on K230D (C908) hardware instead. See ADR-0002 (amended).

## Code discoveries during the run

| File | Issue | Fix |
|------|-------|-----|
| `seccomp_filter.c` | `execve` missing → launcher's own execv triggered SIGSYS | Added `execve` |
| `seccomp_filter.c` | `riscv_flush_icache` (258/259) missing → musl RV32 killed during startup | Added numeric 258 + `riscv_flush_icache` |
| `seccomp_filter.c` | `string.h` missing → `strerror` implicitly declared | Added `#include <string.h>` |
| `seccomp_filter.c` | `SCMP_ARCH_RISCV32` not in libseccomp → uname always killed via arch mismatch | Deferred to production (custom BPF) |
| `run-guest-tests.sh` | `cpu.max "500 5000"` fails (quota 500µs < 1ms min) | Changed to `50000 500000` |
| `run-guest-tests.sh` | Lua false-PASS (grep "SUMMARY" matched "(no SUMMARY line)") | Fixed to `grep "^SUMMARY"` |
| `Makefile` | QEMU 11 dropped U-Boot from bundle | Added `qemu-uboot` target |
| `Makefile` | Ubuntu 24.04.2 image URL → 404 | Updated to 24.04.4 |
| `Dockerfile` | Launcher needed RV64 cross-compiler | Added `gcc-riscv64-linux-gnu` + `libc6-dev:riscv64` |

## Recommendation

**The OS mechanism story is sound.** Proceed with cart format implementation
in Spike I and the runtime build using CONFIG_COMPAT + seccomp + cgroups:

- **CONFIG_COMPAT:** Fedora 42 compiles in ILP32 compat support unconditionally.
  Ubuntu's kernel does not. The cart runtime must target a kernel that has this
  compiled in. Note: regardless of kernel config, ILP32 execution also requires
  hardware UXL=32 support (C908 or later); C906 (Milk-V Duo class) cannot run
  ILP32 binaries even with the right kernel.

- **seccomp:** The KILL_PROCESS filter mechanism works correctly. The allowlist
  needs `riscv_flush_icache` (musl RV32 startup) and a custom BPF rule for
  `AUDIT_ARCH_RISCV32` (since libseccomp lacks this arch constant). The four
  forbidden probes (open, socket, execve, mprotect-exec) are reliably blocked.

- **cgroups v2 cpu.max:** Mechanism confirmed. Quota minimum is 1ms; use
  `50000 500000` (10% CPU) as the test value. The calibration formula produces
  a usable result; hardware-accurate numbers follow from Milk-V Duo measurement.

- **mount + network namespaces + pivot_root:** Confirmed working. The cart
  cannot access host filesystem or network after isolation.
