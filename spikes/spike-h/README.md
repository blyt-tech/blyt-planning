# Spike H — RV32 carts on RV64 Linux

This spike tests three OS-level mechanisms on a RV64 Linux kernel, using
RV32IMFC cart binaries:

| Stage | Question | Mechanism |
|-------|----------|-----------|
| 1 | Can RV32 ELFs run natively under RV64 Linux? | `CONFIG_COMPAT` |
| 3 | Can a cart be sealed off from the host system? | `seccomp-bpf` + Linux namespaces |
| 4 | Can the cart's CPU budget be capped? | cgroups v2 `cpu.max` |

Stage 2 (IPC ring buffer) was superseded by ADR-0024's controlled dynamic
linking and is not part of this spike — see `PLAN.md` for the rationale.

The validation surface has two layers:

1. **Host-side build & smoke-test (this directory's Docker image).** Cross-
   compiles all RV32 cart-side artefacts and runs them under `qemu-user-static`
   to verify they're functional. This layer does **not** exercise CONFIG_COMPAT,
   seccomp, namespaces, or cgroups — qemu-user handles syscalls itself.

2. **QEMU full-system run.** Boots an RV64 Linux kernel under
   `qemu-system-riscv64`, runs the launcher and adversary inside the guest.
   This layer does exercise the kernel mechanisms.

The host-side layer is automatic (`make docker-smoke-stage1` etc.). The
QEMU full-system layer is partially scripted: `make qemu-image` downloads
the Ubuntu 24.04 RISC-V image, `make qemu-run` boots it. Once inside the
guest, run the test sequence in `TASKS.md` § "QEMU guest".

---

## Prerequisites

### Host (macOS arm64 / Linux amd64)

- Docker (Apple Silicon: Docker Desktop with Apple Virtualization). The
  build image pulls `linux/arm64` Ubuntu 24.04 and the
  `riscv32-linux-musl` prebuilt cross-toolchain from [musl.cc](https://musl.cc).
- `qemu-system-riscv64` ≥ 8.0 for the Stage 3/4 validation. On Apple
  Silicon: `brew install qemu`. On Linux: `apt install qemu-system-misc`.
- ~10 GB free disk (1 GB Ubuntu image, ~8 GB resized guest disk).

### Inside the QEMU guest (set up once after first boot)

```sh
sudo apt-get update
sudo apt-get install -y build-essential libseccomp-dev strace gcc-multilib
# RV32 cross-toolchain is already shipped via the host's build/ tree;
# no extra cross-compile is needed inside the guest unless you change
# cart sources and want to rebuild there.
```

---

## Quick start — host-side build & smoke

```sh
make docker-build           # Ubuntu 24.04 image with riscv32-linux-musl
make docker-stage1          # cross-compile hello.elf + readelf
make docker-build-carts     # build all RV32 artefacts
make docker-smoke-stage1    # run hello.elf under qemu-user (Stage 1)
make docker-smoke-busy      # run busy_loop.elf under qemu-user
make docker-smoke-lua       # run Lua workloads under qemu-user (Stage 4)
```

## Quick start — automated QEMU full-system test

Exercises all three kernel mechanisms (CONFIG_COMPAT, seccomp, cgroups v2).
Uses SSH for test execution (cloud-init sets ubuntu/ubuntu credentials).

```sh
make qemu-image   # one-time: download Ubuntu 24.04.4 RISC-V (~1 GB)
make qemu-uboot   # one-time: extract U-Boot ELF from Ubuntu package
brew install sshpass   # required for automated password SSH
make qemu-test    # boot guest, SSH in, run tests, kill guest; results in build/results/
```

`qemu-test` creates a fresh copy of the base image, injects SSH credentials
into the CIDATA partition (using `mtools`), boots QEMU with SSH forwarded to
host port 2222, waits for cloud-init, then SSHes in to run
`run-guest-tests.sh`.  Results land in `build/results/guest-run.log`.
The test image copy is deleted after each run; the base image is unchanged.

> **CONFIG_COMPAT note:** Ubuntu 24.04.4 RISC-V kernel 6.17 has
> `CONFIG_COMPAT` explicitly disabled.  Stage 0 (cgroups v2) and the
> seccomp mechanism pass.  Stages 1, 3, 4 require CONFIG_COMPAT and
> UXL=32 hardware support; use Fedora RISC-V or K230D hardware (C908)
> for those stages. Milk-V Duo / Duo S (C906) cannot be used — the
> C906 lacks UXL=32 support required to exec ILP32 binaries.
> See `docs/design/spike-h-results.md` for the full analysis.

The `build/` directory holds:

| Path | Purpose | ELF flags |
|------|---------|-----------|
| `build/hello` | Stage 1 kernel-load test | `0x3 RVC, single-float ABI` (cart spec) |
| `build/busy_loop` | Stage 4 throttle smoke-test | `0x5 RVC, double-float ABI` |
| `build/adversary` | Stage 3 forbidden-syscall probe | `0x5 RVC, double-float ABI` |
| `build/lua/lua_cart_doom_tick.elf` | Stage 4 Lua workload | `0x5 RVC, double-float ABI` |
| `build/lua/lua_cart_entity_update.elf` | Stage 4 Lua workload | `0x5 RVC, double-float ABI` |

ABI divergence note: the cart spec is `rv32imfc / ilp32f`. The musl.cc
prebuilt toolchain ships libgcc + libc only for `rv32gc / ilp32d`, so any
cart linked against musl uses double-float. `hello.elf` has no libc
dependency and matches the cart spec exactly. The Lua workloads diverge —
matching them to the cart-spec ABI requires building a custom toolchain
or using the K230D vendor rv64ilp32 toolchain. That's out of spike scope.

---

## QEMU full-system run

### One-time: download the guest image

```sh
make qemu-image
```

This downloads `ubuntu-24.04.4-preinstalled-server-riscv64.img.xz` (~600 MB
compressed → ~3 GB extracted) and resizes the disk to 8 GB. The image
ships with `CONFIG_COMPAT=y` and cgroups v2 mounted under systemd.

### Boot the guest

```sh
make qemu-run
```

The default invocation:

- `-machine virt -m 4G -smp 4` — 4 CPUs, 4 GB RAM
- `-bios opensbi-riscv64-generic-fw_dynamic.bin` — OpenSBI firmware
- `-kernel uboot-riscv64-generic.bin` — U-Boot bootloader
- `-netdev user … hostfwd=tcp::2222-:22` — host port 2222 → guest sshd
- `-virtfs local,path=$(CURDIR)/build,mount_tag=spike_h,…` — share the
  host's `build/` directory into the guest at mount point `spike_h`.

First boot takes ~5 minutes for cloud-init. The default user is
`ubuntu`/`ubuntu` (set on first boot). After boot:

```sh
# inside guest:
sudo mkdir -p /mnt/spike-h
sudo mount -t 9p -o trans=virtio spike_h /mnt/spike-h
ls /mnt/spike-h          # should show hello, busy_loop, adversary, lua/
```

### Verify kernel features

```sh
# inside guest:
grep CONFIG_COMPAT /boot/config-$(uname -r)
# expect: CONFIG_COMPAT=y

mount | grep cgroup2
# expect: cgroup2 on /sys/fs/cgroup ...

cat /sys/fs/cgroup/cgroup.controllers
# expect: cpu io memory pids ...
```

If `CONFIG_COMPAT` is absent, switch to a Fedora RISC-V image — Fedora's
kernel has shipped CONFIG_COMPAT since F37. See `PLAN.md` § Risk notes.

### Run Stage 1

```sh
# inside guest:
/mnt/spike-h/hello && echo "Stage 1: PASS"
# expect: OK
#         Stage 1: PASS
```

If `Exec format error`: kernel lacks `CONFIG_COMPAT`. Switch images.

### Run Stage 3 (build launcher first)

The launcher runs as an RV64 native process inside the guest. It is not
cross-compiled by the host Docker image because it needs `libseccomp-dev`
matching the guest's `riscv64-linux-gnu` ABI. Build it inside the guest:

```sh
# inside guest:
cd /mnt/spike-h
sudo apt-get install -y libseccomp-dev    # if not done
gcc -O2 -o launcher launcher.c seccomp_filter.c -lseccomp
```

Then run each adversary probe. Each probe should be killed by SIGSYS
(launcher exits with code 159):

```sh
for probe in open socket execve mprotect-exec; do
    /mnt/spike-h/launcher -- /mnt/spike-h/adversary "$probe"
    echo "  exit=$?"
done

# Sanity check: the allowed `uname` probe should exit 0 (or run without
# being killed):
/mnt/spike-h/launcher -- /mnt/spike-h/adversary uname
```

Expected output: each forbidden probe ends with
`spike-h: child killed by signal 31 (Bad system call)` and exit code 159.

### Run Stage 4 (cgroup throttle)

Create a cgroup, set `cpu.max`, and run `busy_loop.elf` inside it:

```sh
# inside guest:
sudo mkdir -p /sys/fs/cgroup/spike-h-test
echo "+cpu" | sudo tee /sys/fs/cgroup/cgroup.subtree_control
echo "500 5000" | sudo tee /sys/fs/cgroup/spike-h-test/cpu.max  # 10% CPU

# Baseline (no cgroup):
time /mnt/spike-h/busy_loop

# Throttled (joined to cgroup before exec):
time /mnt/spike-h/launcher --cgroup /sys/fs/cgroup/spike-h-test \
    --no-seccomp --no-netns --no-mountns \
    -- /mnt/spike-h/busy_loop

# Throttled run should take ~10× longer than baseline.
```

For the calibrated quota run (Spike A reference):

```sh
# 500 MIPS Pi placeholder; substitute Spike A real number when available.
PI_MIPS=500
# CoreMark on QEMU is not hardware-representative — see PLAN.md §17.
# For mechanism validation we use a fixed throttle ratio:
echo "1000 5000" | sudo tee /sys/fs/cgroup/spike-h-test/cpu.max  # 20% CPU

/mnt/spike-h/launcher --cgroup /sys/fs/cgroup/spike-h-test \
    -- /mnt/spike-h/lua/lua_cart_doom_tick.elf
```

---

## Layout

```
spike-h/
├── PLAN.md               # the spike's plan (read first)
├── README.md             # you are here
├── TASKS.md              # per-step pass/fail checklist
├── Dockerfile            # Ubuntu 24.04 + riscv32-linux-musl + qemu-user-static
├── Makefile              # docker-build / docker-build-carts / qemu-run / qemu-test
├── hello.S               # Stage 1: minimal RV32 ELF, raw ECALLs
├── busy_loop.c           # Stage 4: cgroup throttle smoke-test
├── adversary.c           # Stage 3: forbidden-syscall probe
├── launcher.c            # Stage 3/4: fork/unshare/seccomp/exec harness
├── seccomp_filter.c      # Stage 3: libseccomp filter construction
├── spike_h.h             # shared decls
├── run-guest-tests.sh    # automated in-guest test script (Stages 0–4)
├── lua-workloads/
│   ├── Makefile          # cross-compile Lua + benchmark carts
│   ├── lua_cart_linux.c  # native-Linux variant of spike-b's lua_cart.c
│   └── lua_init_libs.c   # linit replacement (omits io/os/package)
├── qemu/
│   ├── cloud-init/
│   │   ├── user-data     # cloud-config: mount virtfs, run tests, poweroff
│   │   └── meta-data     # cloud-init instance-id
│   ├── seed.iso          # cloud-init seed image (built by make qemu-seed)
│   └── ubuntu-24.04.4-preinstalled-server-riscv64.img  (gitignored, ~3 GB)
├── baselines/            # CoreMark / quota numbers (populated as runs land)
└── build/                # output (gitignored)
    └── results/          # guest-run.log written by run-guest-tests.sh
```

Lua benchmark scripts (`*.lua`) are not duplicated; the lua-workloads/Makefile
references `../../spike-b/benchmarks/`.

---

## What this spike does NOT prove

- That K230D (C908) silicon runs RV32 carts the way QEMU does. The
  CONFIG_COMPAT, seccomp, and cgroups behaviour are kernel-level and
  architecture-portable, but performance numbers are not. Real-hardware
  measurement is a follow-on activity once a K230D is in hand. Note:
  Milk-V Duo / Duo S (C906) cannot be used for this validation — the
  C906 lacks the UXL=32 hardware support needed to exec ILP32 binaries.
- That the Pi Zero 2 W MIPS reference (Spike A) is right. Stage 4 uses a
  500-MIPS placeholder; the `quota_us = floor(5000 × Pi_MIPS / measured_MIPS)`
  formula works with any value substituted.
- That the production seccomp allowlist is final. The `allow_names` array
  in `seccomp_filter.c` is sized for the test harness; the runtime's full
  allowlist needs review once the runtime is implemented.

See `PLAN.md` § "What we are NOT building" for the complete deferred list.
