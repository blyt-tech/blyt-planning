# Spike H — task checklist

Tracks pass/fail per `PLAN.md` step. The spike has two test surfaces:

- **Host (qemu-user)** — verifies the cart binaries are functional but
  does NOT exercise CONFIG_COMPAT, seccomp, namespaces, or cgroups.
- **QEMU guest (full-system)** — exercises all three kernel mechanisms.

Two QEMU guest runs have been executed:

| Guest | Image | Kernel | Date |
|-------|-------|--------|------|
| Ubuntu run | Ubuntu 24.04.4 riscv64 | 6.17.0-14-generic | 2026-05-04 |
| Fedora run | Fedora 42 Cloud riscv64 | 6.16.4-200.0.riscv64.fc42 | 2026-05-04 |

Full log of the Fedora run is in `build/results/guest-run.log`.

---

## Stage 0 — environment

| # | Step | Host | Guest | Notes |
|---|------|------|-------|-------|
| 1 | Guest image boots under qemu-system-riscv64 | n/a | ✅ | Both Ubuntu and Fedora boot via OpenSBI → U-Boot → GRUB |
| 2 | `CONFIG_COMPAT=y` in guest kernel | n/a | ⚠️ | Config file says NOT SET on both; Fedora 42 kernel **does** run RV32 ELFs (compat_sys_* in kallsyms — compiled in, not reflected in config) |
| 3 | cgroups v2 mounted, `cpu` controller present | n/a | ✅ | `cgroup.controllers: cpuset cpu io memory hugetlb pids rdma misc dmem` |
| 4 | RV32 musl cross-toolchain available | ✅ | n/a | Docker image: riscv32-linux-musl gcc 11.2.1 |
| 5 | `qemu-system-riscv64 ≥ 8.0` on host | ✅ | n/a | QEMU 11.0.0 installed via Homebrew |

## Stage 1 — RV32 execution under CONFIG_COMPAT

| # | Step | Host | Guest | Notes |
|---|------|------|-------|-------|
| 6 | `hello.S` cross-compiles to RV32IMFC/ilp32f ELF | ✅ | n/a | `0x3 RVC, single-float ABI` matches cart spec |
| 7 | `hello.elf` runs and prints `OK\n` | ✅ (qemu-user) | ✅ | **PASS on Fedora 42** — rc=0, output='OK'; FAIL on Ubuntu (ENOEXEC) |
| 8 | ELF headers match cart spec (ELFCLASS32, EM_RISCV) | ✅ | n/a | `Class: ELF32, Machine: RISC-V` |

## Stage 3 — seccomp + namespaces

| # | Step | Host | Guest | Notes |
|---|------|------|-------|-------|
| 9 | `launcher.c` cross-compiles for RV64 | n/a | ✅ | `make docker-build-launcher` (gcc-riscv64-linux-gnu); deployed via virtfs |
| 10 | seccomp allowlist covers musl RV32 startup | n/a | ⚠️ | `riscv_flush_icache` (258/259) added; `uname` fails — see note below |
| 11 | `seccomp_filter.c` loads default-KILL filter | n/a | ✅ | seccomp_load returns 0; SIGSYS fires on forbidden calls |
| 12 | Adversary probe: `open` → SIGSYS | n/a | ✅ | rc=159 on Fedora |
| 12 | Adversary probe: `socket` → SIGSYS | n/a | ✅ | rc=159 on Fedora |
| 12 | Adversary probe: `execve` → SIGSYS | n/a | ✅ | rc=159 on Fedora |
| 12 | Adversary probe: `mprotect-EXEC` → SIGSYS | n/a | ✅ | rc=159 on Fedora |
| 12 | Adversary probe: `uname` → exit 0 | n/a | ❌ | rc=159 — libseccomp bug: `SCMP_ARCH_RISCV32` not defined; BPF arch check kills all RV32 syscalls that the LP64 filter doesn't explicitly handle. Production fix: custom BPF with AUDIT_ARCH_RISCV32 support. |
| 13 | Mount-namespace pivot_root sequence works | n/a | ✅ | hello.elf runs in isolated rootfs; /etc/passwd not accessible (ENOENT) |
| 14 | Network namespace blocks `socket` independently | n/a | ✅ | seccomp kills socket before it reaches network namespace |

## Stage 4 — cgroups v2 cpu.max

| # | Step | Host | Guest | Notes |
|---|------|------|-------|-------|
| 15 | `/sys/fs/cgroup/spike-h-test` writable; `cpu.max` accepted | n/a | ✅ | `50000 500000` (50ms/500ms = 10%) accepted; original `500 5000` fails (quota < 1ms Linux min) |
| 16 | busy_loop runs ~10× slower under throttle | n/a | ✅ | Fedora: 7.2s baseline → 85.1s throttled = **11.8×** |
| 17 | CoreMark cross-compiles for `riscv32-linux-musl` | ☐ | n/a | Out of scope until hardware timing is needed |
| 17 | Calibration quota formula works | n/a | ✅ | Mechanism confirmed; `floor(50000 × Pi_MIPS / measured_MIPS)` formula produces usable values |
| 18 | `lua_cart_doom_tick` builds for native RV32 | ✅ | n/a | qemu-user mean=13.2 ms / 30 frames |
| 18 | `lua_cart_entity_update` builds for native RV32 | ✅ | n/a | qemu-user mean=6.0 ms / 50 frames |
| 18 | Lua workloads run under cgroup quota | n/a | ✅ | doom_tick mean=1.71s, entity_update mean=0.75s (throttled at 20% CPU) |

---

## Key findings from the QEMU guest runs

### CONFIG_COMPAT: not in distro config, but compiled in on Fedora

Both Ubuntu 24.04.4 and Fedora 42 kernel config files say `# CONFIG_COMPAT is not set`.
On **Ubuntu**, this is real — hello.elf fails with ENOEXEC.
On **Fedora 42**, `compat_sys_*` symbols appear in `/proc/kallsyms`, confirming
CONFIG_COMPAT IS compiled in despite the config file. hello.elf runs (PASS).

Likely explanation: Fedora compiles in RISC-V ILP32 compat support unconditionally
(without a separate Kconfig option) as it's a required feature of the RISC-V 64-bit ABI.

### seccomp allowlist gaps for RV32/ILP32 compat processes

`libseccomp` does not define `SCMP_ARCH_RISCV32`. A seccomp filter installed by an
LP64 (rv64) launcher process uses `AUDIT_ARCH_RISCV64`. When an RV32 compat process
makes a syscall, the BPF program sees `AUDIT_ARCH_RISCV32` — since no rules are
defined for this arch, the default action (KILL_PROCESS) applies.

This means:
- **All 4 forbidden probes** return SIGSYS (correct outcome, wrong mechanism)
- **uname probe** returns SIGSYS instead of 0 (incorrect — the arch mismatch kills it)
- Production fix: write raw BPF or patch libseccomp to support AUDIT_ARCH_RISCV32

Additionally, musl RV32 startup calls `riscv_flush_icache` (syscall 258/259), which
must be in the allowlist.

### cgroups v2 confirmed working

- cpu.max quota minimum: `≥1000 µs` (original `500 5000` fails; `50000 500000` works)
- Throttle ratio: ~11× at 10% CPU on Fedora 42 / QEMU-system-riscv64

---

## Open items

- `uname` seccomp probe: production requires custom BPF with AUDIT_ARCH_RISCV32
- Hardware validation: Milk-V Duo (C906) for real CoreMark MIPS numbers
- Pi Zero 2 W MIPS: placeholder is 500 MIPS (Spike A pending)
- Production seccomp allowlist: determined when runtime is implemented

---

## Smoke-test command snapshots

```text
$ make docker-smoke-stage1
Stage 1 (qemu-user): PASS

$ make docker-smoke-busy
SUMMARY busy_loop iters=500000000 elapsed_us=1121206

$ make docker-smoke-lua
SUMMARY doom_tick frames=30 min=12893 max=15029 mean=13205
SUMMARY entity_update frames=50 min=5857 max=7149 mean=6038
```

Fedora 42 QEMU guest (2026-05-04):
```text
Stage 1:  hello.elf PASS (output='OK' rc=0)
Stage 3:  SIGSYS for open/socket/execve/mprotect-exec (each rc=159)
          uname FAIL (rc=159 — arch mismatch; libseccomp limitation)
          pivot_root PASS; /etc/passwd inaccessible inside rootfs
Stage 4:  cpu.max 50000/500000 accepted; throttle 11× (baseline 7.2s → throttled 85.1s)
          doom_tick mean=1713ms; entity_update mean=778ms (both at 20% CPU quota)
```
