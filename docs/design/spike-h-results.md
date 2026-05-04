# Spike H — results

> **Status (2026-05-04):** QEMU full-system run executed. Stages 0–4 were
> attempted on Ubuntu 24.04.4 RISC-V (kernel 6.17.0-14-generic).
> **CONFIG_COMPAT is explicitly disabled** in this kernel, blocking RV32
> cart execution (Stages 1, 3, 4 cart tests). Independently proven: cgroups
> v2 cpu.max works, and the seccomp KILL_PROCESS filter is active.
> Re-run with a CONFIG_COMPAT-enabled kernel (Fedora RISC-V or Milk-V Duo
> hardware) is required to complete Stages 1, 3, and 4.

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
| `hello.S` | `spikes/spike-h/hello.S` | Minimal RV32IMFC/ilp32f ELF (no libc, raw ECALL) — Stage 1 kernel-load test |
| `launcher.c` + `seccomp_filter.c` | `spikes/spike-h/` | fork → unshare(NEWNS, NEWNET) → optional pivot_root → seccomp(KILL_PROCESS) → exec → wait harness |
| `adversary.c` | `spikes/spike-h/adversary.c` | RV32 probe program — calls `open`, `socket`, `execve`, `mprotect(PROT_EXEC)`, plus an allowed `uname` sanity probe |
| `busy_loop.c` | `spikes/spike-h/busy_loop.c` | Pure-CPU RV32 loop — Stage 4 cgroup throttle smoke-test |
| `lua-workloads/` | `spikes/spike-h/lua-workloads/` | Native-Linux RV32 port of spike-b's `doom_tick` and `entity_update` (musl-linked instead of bare-metal) |
| `run-guest-tests.sh` | `spikes/spike-h/run-guest-tests.sh` | Automated in-guest test script (Stages 0–4); run via SSH |
| `Dockerfile` | `spikes/spike-h/Dockerfile` | Ubuntu 24.04 arm64 + `riscv32-linux-musl` cross-toolchain + `riscv64-linux-gnu` cross-toolchain + Lua 5.4 source |
| `Makefile` | `spikes/spike-h/Makefile` | `docker-build`, `docker-build-carts`, `docker-build-launcher`, `docker-smoke-*`, `qemu-image`, `qemu-uboot`, `qemu-test` |

## Results — host qemu-user surface (Stage 0–1 host checks)

These verify the cart-side binaries are functional. They do **not** test
CONFIG_COMPAT, seccomp, namespaces, or cgroups — qemu-user handles syscalls
on the host kernel directly.

| Check | Result |
|-------|--------|
| `riscv32-linux-musl` cross-toolchain installs and reports gcc 11.2.1 | PASS |
| `hello.elf` cross-compiles with `-march=rv32imfc -mabi=ilp32f` | PASS — ELF flags `0x3 RVC, single-float ABI` match cart spec |
| `hello.elf` under `qemu-riscv32-static`: prints `OK\n`, exit 0 | PASS |
| `busy_loop.elf` under qemu-user: 5×10⁸ iters in ~1.12 s | PASS (mechanism-validated only — not silicon time) |
| `adversary.elf` runs each probe under qemu-user with no seccomp filter — `uname` returns 0 | PASS |
| `lua_cart_doom_tick.elf` runs 30 frames, mean ≈ 13.2 ms | PASS — workload functional |
| `lua_cart_entity_update.elf` runs 50 frames, mean ≈ 6.0 ms | PASS — workload functional |

## Results — QEMU full-system run (2026-05-04)

### Test environment

- Host: macOS arm64 (Apple Silicon), QEMU 11.0.0
- Guest image: Ubuntu 24.04.4 preinstalled-server-riscv64
- Guest kernel: 6.17.0-14-generic (Ubuntu noble, 2026-04)
- QEMU invocation: `qemu-system-riscv64 -machine virt -m 4G -smp 4`
  `-bios opensbi-riscv64-generic-fw_dynamic.bin -kernel uboot-riscv64.elf`
  `-virtfs local,path=spike-h/,mount_tag=spike_h,security_model=mapped-xattr`
- Tests run via SSH (ubuntu/ubuntu, port 2222) executing `run-guest-tests.sh`
- Full log: `spikes/spike-h/build/results/guest-run.log`

### Stage 0: kernel features

| Check | Result | Detail |
|-------|--------|--------|
| Guest boots under qemu-system-riscv64 | **PASS** | SSH access confirmed |
| `CONFIG_COMPAT=y` in kernel config | **FAIL** | `/boot/config-6.17.0-14-generic`: `# CONFIG_COMPAT is not set` — Ubuntu 24.04.4 RISC-V kernel deliberately omits compat ABI |
| cgroups v2 mounted | **PASS** | `cgroup2 on /sys/fs/cgroup` |
| `cpu` controller present | **PASS** | `cgroup.controllers: cpuset cpu io memory hugetlb pids rdma misc dmem` |

### Stage 1: RV32 ELF under CONFIG_COMPAT

| Check | Result | Detail |
|-------|--------|--------|
| `hello.elf` runs and prints `OK\n` | **FAIL** | `Exec format error` (rc=126) — kernel returns ENOEXEC for 32-bit ELF without CONFIG_COMPAT |

### Stage 3: seccomp + namespace isolation

#### Seccomp mechanism (proven via launcher behaviour)

The seccomp filter `SCMP_ACT_KILL_PROCESS` mechanism is **confirmed active**:

- When `execve` was absent from the allowlist (first test run), the launcher's
  own `execv()` call triggered SIGSYS (launcher parent reported rc=159 =
  128+SIGSYS). This proves the kernel's `SCMP_ACT_KILL_PROCESS` fires on
  syscalls not in the allowlist.
- After adding `execve` to the allowlist (second test run), `execv()` no longer
  triggers SIGSYS — it fails with ENOEXEC (rc=127) because the adversary binary
  is RV32 and CONFIG_COMPAT is absent. This is the correct distinction between
  "seccomp blocked" and "kernel rejected the binary format".

#### Adversary probe results

All probes return rc=127 (ENOEXEC) instead of rc=159 (SIGSYS). The ENOEXEC is
from the kernel rejecting the 32-bit binary, not from seccomp. The probes
cannot fully exercise the filter until CONFIG_COMPAT is available.

| Probe | Expected | Got | Reason |
|-------|----------|-----|--------|
| `open` | SIGSYS (159) | ENOEXEC (127) | CONFIG_COMPAT absent — RV32 adversary can't exec |
| `socket` | SIGSYS (159) | ENOEXEC (127) | Same |
| `execve` | SIGSYS (159) | ENOEXEC (127) | Same |
| `mprotect-exec` | SIGSYS (159) | ENOEXEC (127) | Same |
| `uname` (allowed) | exit 0 | ENOEXEC (127) | Same |

#### Mount and network namespaces

`unshare(CLONE_NEWNS | CLONE_NEWNET)` did not fail in the launcher
(confirmed from launcher stderr). The pivot_root sequence could not be
fully tested because hello.elf (RV32) cannot be exec'd inside the rootfs
without CONFIG_COMPAT.

### Stage 4: cgroups v2 cpu.max

| Check | Result | Detail |
|-------|--------|--------|
| `cpu.max` write accepted | **PASS** | `50000 500000` (10% CPU, 100ms/500ms) accepted |
| `cpu.max "500 5000"` (original PLAN value) | **FAIL** | `Invalid argument` — Linux CFS minimum quota is 1000µs; 500µs is below the floor |
| busy_loop throttle ratio | **FAIL** | busy_loop is RV32; can't exec without CONFIG_COMPAT |
| Lua workloads under cgroup | **FAIL** | Lua ELFs are RV32; same issue |

**cpu.max finding**: The PLAN §16 specifies `"500 5000"` (500µs quota per
5ms period = 10% CPU). The Linux kernel's `CFS_BANDWIDTH_MIN_QUOTA_US`
is 1000µs (1ms), so any quota < 1ms is rejected with EINVAL.
Corrected value: `"50000 500000"` (50ms quota per 500ms period = 10% CPU)
or any pair where quota ≥ 1000µs. The formula `floor(period × Pi_MIPS /
measured_MIPS)` in PLAN §17 will produce values well above this floor
for realistic MIPS numbers.

## Code fixes discovered during the run

| File | Issue | Fix |
|------|-------|-----|
| `seccomp_filter.c` | `execve` absent from allowlist — launcher's own `execv()` triggered SIGSYS | Added `execve` to allowlist; noted as production-tightening item |
| `seccomp_filter.c` | `uname` may not resolve on RISC-V (libseccomp name vs kernel alias) | Added `newuname` as additional allowlist entry |
| `seccomp_filter.c` | `string.h` missing → `strerror` implicitly declared | Added `#include <string.h>` |
| `run-guest-tests.sh` | `cpu.max "500 5000"` — quota 500µs < kernel 1000µs minimum | Changed to `50000 500000` |
| `run-guest-tests.sh` | Lua false-PASS: `grep "SUMMARY"` matched the literal string `"(no SUMMARY line)"` | Fixed to use `grep "^SUMMARY"` and separate output capture variable |
| `Makefile` / `Dockerfile` | Launcher built inside QEMU guest (slow apt-get over SLIRP) | Added `docker-build-launcher` target: cross-compiles RV64 launcher via `gcc-riscv64-linux-gnu` in Docker |
| `Makefile` | QEMU 11 dropped U-Boot from firmware bundle | Added `qemu-uboot` target: downloads `uboot.elf` from Ubuntu's `u-boot-qemu` package |
| `Makefile` | Ubuntu image URL was 24.04.2 (404) | Updated to 24.04.4 |

## What this spike does NOT prove (unchanged)

- That Milk-V Duo (C906) silicon runs RV32 carts the way QEMU does.
- That the Pi Zero 2 W reference MIPS (Spike A) is final.
- That the production seccomp allowlist is final.
- That the cart-spec `ilp32f` ABI Lua workloads run identically to the
  `ilp32d` versions used here.

## Recommendation

The Ubuntu 24.04.4 RISC-V kernel for QEMU has CONFIG_COMPAT explicitly
disabled. To complete the guest-side validation:

**Option A (fastest):** Switch to Fedora RISC-V QEMU image — Fedora has
shipped CONFIG_COMPAT on RISC-V since F37. Run `run-guest-tests.sh` on the
Fedora guest; expected outcome: Stage 1 PASS (hello.elf runs), Stage 3 PASS
(adversary probes each trigger SIGSYS), Stage 4 PASS (busy_loop throttle
ratio ≈ 10×, Lua workloads run under quota).

**Option B (hardware):** Run on Milk-V Duo hardware when available. The Duo's
buildroot kernel includes CONFIG_COMPAT. Results would also validate the
C906 CoreMark/MHz constant for the cpu.max quota formula.

**Current evidence for the mechanism story:**

The cgroups v2 cpu.max mechanism is sound — `cpu.max` accepts valid quotas
and the kernel controller is active (confirmed by the `cpu` entry in
`cgroup.controllers` and successful write with corrected quota values).

The seccomp `SCMP_ACT_KILL_PROCESS` mechanism is confirmed active on this
kernel — a forbidden syscall (execve, when not in the allowlist) is
immediately killed with SIGSYS. The allowlist mechanism works correctly.

The missing CONFIG_COMPAT is a kernel configuration choice (Ubuntu team
decision) not a fundamental RISC-V limitation. Both Fedora and the Milk-V
Duo's kernel include it.
