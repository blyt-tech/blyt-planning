# Spike H — results

> **Status (2026-05-04):** Stage 0 (environment) and Stage 1 (RV32 execution
> on RV64 kernel — qemu-user portion) complete. Stage 1 (full-system QEMU),
> Stage 3 (seccomp + namespaces), and Stage 4 (cgroups v2 cpu.max) infrastructure
> is in place and ready to run; the actual QEMU full-system pass is gated on
> downloading the Ubuntu RISC-V image and is documented in `spikes/spike-h/README.md`.

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
| `Dockerfile` | `spikes/spike-h/Dockerfile` | Ubuntu 24.04 arm64 + `riscv32-linux-musl` cross-toolchain (musl.cc) + `qemu-user-static` + Lua 5.4 source |
| `Makefile` | `spikes/spike-h/Makefile` | `docker-build`, `docker-build-carts`, `docker-smoke-*`, `qemu-image`, `qemu-run` |

## Results so far (host qemu-user surface)

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

## Open items — full-system QEMU run

Documented procedure in `spikes/spike-h/README.md` § "QEMU full-system run".
Not yet executed because the Ubuntu 24.04 RISC-V image download (~600 MB
compressed → ~3 GB extracted) is left as an explicit `make qemu-image`
step. Once run, results land in `spike-h/baselines/coremark-qemu.txt` and
this document's table is updated. The expected outcomes per Plan are:

- Stage 1 in guest: `hello.elf` prints `OK\n` and exits 0; `readelf -h`
  matches host. Confirms `CONFIG_COMPAT` is on.
- Stage 3: each forbidden adversary probe terminates with SIGSYS; the
  allowed `uname` probe returns 0. Confirms `seccomp-bpf` filter is
  enforced. Mount and network namespaces are unshared; `pivot_root` to
  an empty rootfs hides host filesystem.
- Stage 4: `busy_loop.elf` inside a cgroup with `cpu.max "500 5000"` takes
  ≈ 10× longer than baseline. CoreMark cross-compiles. The quota formula
  `floor(5000 × Pi_MIPS / measured_MIPS)` produces a usable number. Lua
  workloads inside the calibrated cgroup show meaningful budget pressure
  vs. uncapped.

## What this spike does NOT prove

- That Milk-V Duo (C906) silicon runs RV32 carts the way QEMU does. The
  CONFIG_COMPAT, seccomp, and cgroups behaviour are kernel-level and
  architecture-portable, but performance numbers are not. Real-hardware
  measurement is a follow-on activity.
- That the Pi Zero 2 W reference MIPS (Spike A) is final. Stage 4 uses a
  500-MIPS placeholder per ADR-0082; the formula works with any value.
- That the production seccomp allowlist is final. The `allow_names` array
  in `seccomp_filter.c` is sized for the test harness; the runtime's full
  allowlist is determined when the runtime is implemented.
- That the cart-spec `ilp32f` ABI Lua workloads run identically to the
  `ilp32d` versions used here. The musl.cc prebuilt toolchain ships
  libgcc/libc only for `rv32gc/ilp32d`. The kernel mechanisms under test
  (CONFIG_COMPAT, seccomp, cgroups) are FP-ABI-independent, so this
  divergence does not affect the spike's conclusions; the cart-spec build
  is verified separately on Milk-V Duo silicon.

## Recommendation

Once the QEMU guest run is complete, the conclusion will be one of:

- All three stages PASS → the OS mechanism story is sound; proceed with
  cart format implementation in Spike I and the runtime build using
  CONFIG_COMPAT + seccomp + cgroups as described in the cart format spec.
- Stage 1 fails (`Exec format error`) → the chosen guest distro's kernel
  lacks CONFIG_COMPAT. Switch to Fedora RISC-V; document in the design.
- Stage 3 fails (any forbidden probe survives) → the seccomp filter or
  filter-install ordering is wrong. Iterate on `seccomp_filter.c`'s
  allowlist using `strace` of the cart's startup sequence inside the
  guest.
- Stage 4 fails (no observable throttle) → cgroups v2 isn't actually
  active. Verify with `mount | grep cgroup2` and the kernel config bits
  listed in `PLAN.md` § Risk notes.

The infrastructure is positioned so the QEMU run is a one-shot validation:
download → boot → run the Stage 1/3/4 commands from `README.md` →
fill in `baselines/coremark-qemu.txt` and the table above.
