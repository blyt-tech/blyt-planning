# ADR-0002: Reference hardware — Milk-V Duo class SBCs

## Status
Accepted

## Context

The console targets physical hardware for anyone wanting a real-device
experience. The hardware choice sets the performance floor, the memory budget,
and the complexity of the hardware software stack. MCU-class hardware (ESP32-C3,
CH32V series) was considered but has RAM 100–1000× smaller than a comfortable
game runtime; accommodating both profiles would compromise the full-fidelity
experience for the larger class.

### Emulation
Raspberry Pi was considered as a baseline. Pi 1 / Pi Zero W (original) is too
slow for the emulator under ambitious cart workloads. Pi Zero 2 W (Cortex-A53
quad at 1 GHz) is the minimum emulation host, but does not run RISC-V natively.

## Decision

**Floor hardware:** Milk-V Duo (C906 RISC-V64 at 1 GHz, 64 MB RAM, ~$9).
**Reference target:** Milk-V Duo S (SG2000, 512 MB RAM, ~$13).
**Minimum emulation host:** Raspberry Pi Zero 2 W class (Cortex-A53 quad-core,
1 GHz, ARMv8 with NEON).

MCU-class hardware is explicitly not supported. Pi 1 / Pi Zero W (original)
is explicitly not supported.

## Consequences

- The $9–$13 price point is achievable for hobbyists, educators, and
  anyone building a cabinet or handheld.
- The SBC target means Linux driver support is available for USB gamepads,
  audio codecs, display interfaces, and SD card storage — no per-board
  driver work for the console project.
- Native RISC-V execution on the Duo means cart code runs without
  interpretation overhead on the cheapest supported device.
- The 64 MB RAM floor (Duo) and 512 MB reference (Duo S) comfortably cover
  the 32 MB runtime budget (16 MB working memory + 16 MB overhead).
- Porting to new SBCs is low-friction as long as they run Linux; the project
  starts with Milk-V Duo only and adds others as community demand emerges.
