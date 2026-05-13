# ADR-0002: Reference hardware — K230D class SBCs

## Status
Accepted — amended 2026-05-13 to replace Milk-V Duo class with K230D class.

Original decision (Milk-V Duo / Duo S) was superseded when investigation
confirmed that the T-Head XuanTie C906 processor — used in both the Milk-V
Duo (SG2002) and Milk-V Duo S (SG2000) — does not support `sstatus.UXL=32`,
the hardware prerequisite for running 32-bit (ILP32) user-mode processes on a
64-bit RISC-V kernel. The Linux compat ELF loader probes for this capability
via `SR_UXL_32`; on C906 hardware the probe fails and ILP32 ELF binaries
receive `ENOEXEC` regardless of kernel configuration. This is a hardware
limitation, not a kernel-config omission.

The C908 introduced RV32 COMPAT mode (UXL=32/64) as a new capability;
T-Head cited it as "the first in the industry" to support this. The K230D
contains dual C908 cores and is the cheapest widely-available hardware that
satisfies the ILP32 execution requirement.

## Context

The console targets physical hardware for anyone wanting a real-device
experience. The hardware choice sets the performance floor, the memory budget,
and the complexity of the hardware software stack. MCU-class hardware (ESP32-C3,
CH32V series) was considered but has RAM 100–1000× smaller than a comfortable
game runtime; accommodating both profiles would compromise the full-fidelity
experience for the larger class.

### Why ILP32 hardware support is required

The native execution path (ADR-0119) runs cart binaries as ILP32 (RV32 ABI)
processes directly on the RV64 host kernel. This requires the host CPU to
support `sstatus.UXL=32` — the mechanism by which the kernel sets a process
into 32-bit user mode. Without this hardware feature, native cart execution
is impossible; the kernel's `compat_elf_check_arch` function rejects ILP32
binaries at exec time.

### Why the Milk-V Duo class does not qualify

Both Milk-V Duo (SG2002, C906 @ 1GHz) and Milk-V Duo S (SG2000, C906 @ 1GHz
+ Cortex-A53) use the C906 core. The C906 is RV64GCV but does not implement
`sstatus.UXL=32`. Real Milk-V hardware cannot exec ILP32 cart binaries.

These boards remain viable as **emulation hosts** (running rv32emu on the LP64
kernel to interpret cart code) but are not viable as native execution targets.

### Emulation-only hosts

Raspberry Pi Zero 2 W (Cortex-A53 quad-core, 1GHz, ARMv8 with NEON) remains
the minimum emulation host. It does not run RISC-V natively but can host
rv32emu to emulate the cart ISA. Pi 1 / Pi Zero W (original) is too slow.

## Decision

**Floor hardware:** Canaan K230D-based board (dual C908 RISC-V64 at 1.6GHz +
800MHz, 128MB integrated LPDDR4, ~$29). Reference board: Banana Pi
BPI-CanMV-K230D-Zero.

**Minimum emulation host:** Raspberry Pi Zero 2 W class (Cortex-A53
quad-core, 1GHz, ARMv8 with NEON).

MCU-class hardware is explicitly not supported. Pi 1 / Pi Zero W (original)
is explicitly not supported. Milk-V Duo / Duo S class (C906) is not supported
as a native execution target.

The K230D vendor SDK ships `k230d_canmv_ilp32_defconfig`, a Linux 6.6-based
kernel with the rv64ilp32 patchset applied and an ILP32 rootfs toolchain
(RuyiSDK rv64ilp32). This is the starting point for the buildroot-based
runtime image (ADR-0035).

## Consequences

- The $29 price point is achievable for hobbyist builds, educators, and
  anyone building a cabinet or handheld; this target is explicitly hobbyist,
  not mass manufacturing.
- The K230D's dual C908 cores (1.6GHz + 800MHz) are substantially faster
  than the former C906 @ 1GHz floor, giving more headroom per the MIPS cap
  model (ADR-0082).
- 128MB integrated LPDDR4 comfortably covers the 32MB runtime budget
  (16MB working memory + 16MB overhead).
- The SBC target means Linux driver support is available for USB gamepads,
  audio codecs, display interfaces, and SD card storage — no per-board
  driver work for the console project.
- The K230D vendor SDK's ILP32 kernel config reduces the kernel-build
  effort compared to maintaining a fully custom patchset.
- Porting to new SBCs is low-friction as long as they run Linux on a CPU
  with UXL=32 support (C908, or any future RISC-V core that implements the
  COMPAT capability); the project starts with K230D only.
- The hardware validation task deferred by Spike H (CoreMark MIPS on real
  silicon) should be performed on K230D hardware, not Milk-V Duo hardware.
