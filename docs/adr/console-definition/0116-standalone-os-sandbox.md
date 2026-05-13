# ADR-0116: OS-level sandbox for the standalone deployment

## Status

Accepted (Spike R required for raw-BPF seccomp implementation; see below)

## Context

ADR-0038 specifies two-layer sandboxing: a RISC-V ABI layer (enforced by
the emulator) and, on native hardware, an OS-level process isolation
layer. Spike H proved all three OS mechanisms — CONFIG_COMPAT RV32
execution, seccomp-bpf, namespace isolation, and cgroups v2 CPU
throttling — work on a Fedora 42 RISC-V kernel (6.16.4). This ADR
captures the production design for the standalone `blyt` runner on Linux.

The OS-level sandbox is **not claimed for the libretro deployment**. See
the libretro section below.

## Decision

### Fork-per-invocation

Each cart execution runs in a fresh child process. The parent forks,
applies all isolation steps described below in the child before exec, and
supervises the child. No mutable state is shared between invocations.

### Two-phase seccomp

**Phase 1 (pre-exec filter):** loaded in the child after fork, before
`execve`. Must allow `execve` for the launcher. Default action:
`SCMP_ACT_KILL_PROCESS`.

**Phase 2 (post-exec filter):** loaded by the cart runner on startup,
before any cart code executes. Removes `execve` from the allowlist.
Default action: `SCMP_ACT_KILL_PROCESS`.

**Implementation requirement:** `libseccomp` does not define
`SCMP_ARCH_RISCV32` (gap identified in Spike H). Phase 2 must be
implemented as a hand-written raw BPF program so that it can explicitly
check for both `AUDIT_ARCH_RISCV64` (LP64 launcher) and
`AUDIT_ARCH_RISCV32` (ILP32 cart process after exec). Spike R proves
this mechanism and derives the production allowlist empirically before
the implementation of this ADR begins.

The phase 2 syscall allowlist is derived by running rv32emu on
representative cart workloads under `strace` and culling to the minimum
set. It includes at minimum the syscalls identified in Spike H
(`mmap2`, `brk`, `read`, `write`, `exit_group`, `rt_sigreturn`,
`clock_gettime64`, `riscv_flush_icache`, `futex`, and compat-layer
variants) and excludes all others. The final list is committed as a
source constant.

### Namespace isolation (proven in Spike H Stage 3)

In the child before exec:
```c
unshare(CLONE_NEWUSER | CLONE_NEWNS | CLONE_NEWPID |
        CLONE_NEWNET | CLONE_NEWIPC | CLONE_NEWUTS);
```
A `pivot_root` into an empty or read-only mount namespace follows. After
isolation the cart cannot access the host filesystem, network, or IPC
objects.

### cgroups v2 cpu.max (proven in Spike H Stage 4)

Before exec the parent writes `<quota_us> <period_us>` to the child's
cgroup `cpu.max`. The minimum kernel-accepted period is 1 ms; use a
period of at least 500 000 µs (500 ms) to ensure quota values are
nonzero. Quota formula:

```
quota_us = floor(period_us × Pi_MIPS / measured_host_MIPS)
```

`Pi_MIPS` is the Spike A figure for the minimum emulation host (Pi Zero
2 W). `measured_host_MIPS` is the emulator throughput on the current
hardware measured at runtime startup. Hardware constants are finalised
from Spike A and the K230D calibration deferred by Spike H.

### rlimits

Set in the child before exec: `RLIMIT_CPU` (belt-and-braces fallback
matching the cgroups quota), `RLIMIT_AS` (guest address space cap),
`RLIMIT_FSIZE` (0 — the empty mount namespace also prevents file
creation), `RLIMIT_NOFILE` (minimal fd count).

### Wall-clock timeout

The parent process supervises the child with a wall-clock watchdog. If
the child does not exit within a configurable deadline, the parent sends
`SIGKILL`. The deadline is not adjustable by the cart.

### Guest memory budget

The guest address space is allocated at load time with `mmap` at a fixed
cap determined by the cart's declared size class (ADR-0030).

## Libretro deployment

The libretro core is loaded in-process by a frontend (RetroArch etc.)
on Linux, Windows, macOS, and web. Cross-platform in-process OS-level
sandboxing is not practical: Linux seccomp does not generalise to Windows
or macOS; per-thread syscall filtering requires non-trivial host
cooperation; and IPC-based isolation is a substantial engineering
investment with its own attack surface.

**The libretro deployment is for trusted and curated content only. It is
not claimed to contain hostile input.**

All in-emulator hardening (ADR-0112 static checks, ADR-0113 handle
validation, ADR-0114 argument validation, ADR-0115 memory protection and
ECALL trap) still applies in the libretro deployment.

**CPU budget enforcement in libretro** is implemented via the interpreter's
existing instruction counter rather than a watchdog thread. The interpreter
already checks a per-frame instruction budget for the MIPS cap (ADR-0082);
the wall-clock budget is a second threshold check in the same hot-path
counter. This is portable across all libretro platforms including
single-threaded WASM (which cannot use pthreads without
`SharedArrayBuffer`/COOP+COEP headers that many RetroArch Web deployments
do not set). A watchdog thread is not used in the libretro core.

## Consequences

- The standalone deployment provides defence in depth: OS sandbox as the
  outer layer, in-emulator checks as the inner layer.
- The libretro deployment's weaker threat model is explicitly documented.
  Frontend authors who need hostile-input isolation should use the
  standalone runner, not the libretro core.
- Two-phase seccomp with raw BPF is blocked on Spike R; until Spike R
  completes this ADR's seccomp section is a design target.
- The cgroups CPU quota formula produces accurate numbers once Spike A's
  Pi Zero 2 W measurement is finalised.
- Using the interpreter's instruction counter for libretro CPU budget
  enforcement requires no threading and works identically on WASM,
  mobile, and desktop libretro targets.
