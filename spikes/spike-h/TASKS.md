# Spike H — task checklist

Tracks pass/fail per `PLAN.md` step. The spike has two test surfaces:

- **Host (qemu-user)** — verifies the cart binaries are functional but
  does NOT exercise CONFIG_COMPAT, seccomp, namespaces, or cgroups.
- **QEMU guest (full-system)** — exercises all three kernel mechanisms.

A check in the "Host" column means the binary builds and behaves correctly
under qemu-user on Apple Silicon Docker. A check in the "Guest" column
means the kernel mechanism it tests is confirmed working under
`qemu-system-riscv64` running an Ubuntu 24.04 RV64 guest.

Guest run executed 2026-05-04 via SSH into Ubuntu 24.04.4 (kernel
6.17.0-14-generic).  Results logged in `build/results/guest-run.log`.

---

## Stage 0 — environment

| # | Step | Host | Guest | Notes |
|---|------|------|-------|-------|
| 1 | Ubuntu 24.04 RISC-V image boots under qemu-system-riscv64 | n/a | ✅ | `make qemu-image` + SSH access confirmed |
| 2 | `CONFIG_COMPAT=y` in guest kernel | n/a | ❌ | Ubuntu 24.04.4 kernel 6.17.0-14-generic has `# CONFIG_COMPAT is not set`; see PLAN.md §Risk — switch to Fedora RISC-V |
| 3 | cgroups v2 mounted, `cpu` controller present | n/a | ✅ | `cgroup.controllers: cpuset cpu io memory hugetlb pids rdma misc dmem` |
| 4 | RV32 musl cross-toolchain available | ✅ | n/a | Docker image pulls `riscv32-linux-musl-cross.tgz` from musl.cc; reports gcc 11.2.1 |
| 5 | `qemu-system-riscv64 ≥ 8.0` on host | n/a | n/a | QEMU 11.0.0 installed via Homebrew |

## Stage 1 — RV32 execution under CONFIG_COMPAT

| # | Step | Host | Guest | Notes |
|---|------|------|-------|-------|
| 6 | `hello.S` cross-compiles to RV32IMFC/ilp32f ELF | ✅ | n/a | `0x3 RVC, single-float ABI` matches cart spec exactly |
| 7 | `hello.elf` runs and prints `OK\n` | ✅ (qemu-user) | ❌ | rc=126 `Exec format error` — kernel lacks CONFIG_COMPAT |
| 8 | ELF headers match cart spec (ELFCLASS32, EM_RISCV) | ✅ | n/a | `Class: ELF32, Machine: RISC-V` |

## Stage 3 — seccomp + namespaces

| # | Step | Host | Guest | Notes |
|---|------|------|-------|-------|
| 9 | `launcher.c` cross-compiles for RV64 | n/a | ✅ | Cross-compiled by `make docker-build-launcher` (gcc-riscv64-linux-gnu); deployed via virtfs |
| 10 | seccomp allowlist covers musl static startup | ✅ (qemu-user) | ⚠️ | Filter loads and kills forbidden syscalls; execve added to allowlist to allow launcher→cart exec; `uname`/`newuname` both included |
| 11 | `seccomp_filter.c` loads default-KILL filter | n/a | ✅ | Confirmed: execve NOT in original allowlist triggered SIGSYS on launcher's own execv call — proves SCMP_ACT_KILL_PROCESS is active |
| 12 | Adversary probe: `open` → SIGSYS | n/a | ❌ | rc=127 (ENOEXEC) — adversary is RV32, can't exec without CONFIG_COMPAT; mechanism blocked before seccomp can fire |
| 12 | Adversary probe: `socket` → SIGSYS | n/a | ❌ | Same — CONFIG_COMPAT needed |
| 12 | Adversary probe: `execve` → SIGSYS | n/a | ❌ | Same |
| 12 | Adversary probe: `mprotect-EXEC` → SIGSYS | n/a | ❌ | Same |
| 13 | Mount-namespace pivot_root sequence works | n/a | ❌ | rc=127 — hello.elf in rootfs is RV32; pivot_root itself untestable without CONFIG_COMPAT |
| 14 | Network namespace blocks `socket` independently | n/a | ❌ | Blocked by same CONFIG_COMPAT issue |

## Stage 4 — cgroups v2 cpu.max

| # | Step | Host | Guest | Notes |
|---|------|------|-------|-------|
| 15 | `/sys/fs/cgroup/spike-h-test` writable; `cpu.max` accepts quota | n/a | ✅ | `50000 500000` (10% CPU) accepted; `500 5000` failed — Linux min cfs_quota_us is 1000µs, not 500µs |
| 16 | busy_loop runs ~10× slower under throttle | n/a | ❌ | busy_loop is RV32 — fails with ENOEXEC without CONFIG_COMPAT |
| 17 | CoreMark cross-compiles for `riscv32-linux-musl` | ☐ | n/a | Out of scope until CONFIG_COMPAT is confirmed |
| 17 | Calibration quota formula works | n/a | ❌ | Blocked by CONFIG_COMPAT |
| 18 | `lua_cart_doom_tick` builds for native RV32 | ✅ | n/a | qemu-user mean=13.2 ms / 30 frames (Apple Silicon Docker) |
| 18 | `lua_cart_entity_update` builds for native RV32 | ✅ | n/a | qemu-user mean=6.0 ms / 50 frames |
| 18 | Lua workloads run under cgroup quota | n/a | ❌ | RV32 ELFs, blocked by CONFIG_COMPAT |

---

## Key findings from the QEMU guest run

### Confirmed working mechanisms

- **cgroups v2**: Present, `cpu` controller enabled, `cpu.max` writable.
  Correct quota format: `<quota_us> <period_us>` where quota ≥ 1000µs.
  `50000 500000` (10% CPU) works; `500 5000` does not (quota < kernel minimum).
- **seccomp-bpf KILL_PROCESS**: Confirmed active — when `execve` was absent
  from the allowlist, the launcher's own `execv()` call triggered SIGSYS
  (rc=159 from `WTERMSIG`), proving the kill mechanism fires on forbidden
  syscalls. After adding `execve` to the allowlist, the call proceeds and
  fails with ENOEXEC (rc=127) instead — correct behaviour.
- **virtfs (9p)**: Works correctly via SSH; the host `spikes/spike-h/`
  tree is visible inside the guest at `/mnt/spike-h/`.

### CONFIG_COMPAT absent in Ubuntu 24.04.4

`/boot/config-6.17.0-14-generic` contains:
```
# CONFIG_COMPAT is not set
# CONFIG_COMPAT_32BIT_TIME is not set
```
This blocks all RV32 ELF execution (Stages 1, 3, 4). Per PLAN.md §Risk
notes: **switch to Fedora RISC-V** (ships CONFIG_COMPAT since F37) or
the Milk-V Duo hardware for the full validation run.

### Code fixes applied during this run

- `seccomp_filter.c`: Added `execve` (launcher needs it for cart exec)
  and `newuname` (RISC-V alias for `uname`).
- `run-guest-tests.sh`: Corrected cpu.max period to `50000 500000`.
- `run-guest-tests.sh`: Fixed Lua workload false-PASS logic bug.

---

## Open items deferred to real hardware / CONFIG_COMPAT kernel

- CONFIG_COMPAT — Ubuntu 24.04.4 QEMU kernel lacks it; repeat run
  with Fedora RISC-V or Milk-V Duo.
- Adversary seccomp probes — need RV32 cart ELFs running under compat.
- pivot_root mount isolation — same dependency.
- busy_loop cgroup throttle ratio — RV32 busy_loop can't execute yet.
- Milk-V Duo (C906) CoreMark/MHz baseline — feeds the cgroup quota constant.
- Pi Zero 2 W (Cortex-A53) MIPS measurement — feeds the `Pi_MIPS` term.
- Cart-spec ABI (`rv32imfc / ilp32f`) Lua build — needs custom toolchain.
- Production seccomp allowlist — final set deferred to runtime.

---

## Smoke-test command snapshots

Updated when re-run; treat as "last successful invocation" rather than CI
output.

```text
$ make docker-smoke-stage1
=== hello.elf under qemu-user ===
stdout=[OK] rc=0
Stage 1 (qemu-user): PASS

$ make docker-smoke-busy
=== busy_loop.elf under qemu-user (no cgroup) ===
SUMMARY busy_loop iters=500000000 elapsed_us=1121206

$ make docker-smoke-lua
=== lua_cart_doom_tick under qemu-user ===
SUMMARY doom_tick frames=30 min=12893 max=15029 mean=13205
=== lua_cart_entity_update under qemu-user ===
SUMMARY entity_update frames=50 min=5857 max=7149 mean=6038
```

QEMU emulation under arm64 Docker on Apple Silicon — not representative of
target hardware; serves as an "is the binary functional" baseline only.
