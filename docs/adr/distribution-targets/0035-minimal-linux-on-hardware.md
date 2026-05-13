# ADR-0033: On hardware — minimal Linux (buildroot-based), runtime as PID-1-equivalent

## Status
Accepted

## Context

The hardware target (K230D class SBCs) needs a software stack. Options:
- **Full desktop Linux** (Raspbian, Ubuntu): easy to develop with, but
  exposes a desktop shell — breaks the fantasy console illusion.
- **Bare-metal** (no OS): maximum control, no overhead, but requires writing
  per-board drivers for USB gamepads, audio codecs, display, SD card.
  Driver work is weeks per board.
- **Minimal Linux** (buildroot): leverages Linux driver support while being
  invisible to the end user. Boot goes directly to the runtime.

## Decision

**Minimal Linux via buildroot.** The runtime runs as the primary process
(PID-1-equivalent) under a stripped Linux userspace with no shell, no desktop,
no services beyond what the runtime needs.

- Buildroot image: kernel + minimal userspace + runtime binary.
- Boot time: 3–5 seconds with tuning — acceptable for home-console feel.
- Developer access: SSH / remote debugging available when enabled in a
  development-mode image; disabled in shipping images.
- Linux is invisible to the end user: they see startup chime, console UI,
  cart picker.

**Explicitly not chosen:**
- Full desktop Linux distribution (breaks the fantasy).
- Bare-metal (driver work is disproportionate; future option for a dedicated
  hardware product).

## Consequences

- Linux driver support for USB gamepads, audio codecs, display interfaces
  (HDMI/DSI/SPI LCD), and SD card storage — no custom driver work per board.
- Portable across SBCs: "does it run Linux?" is almost always yes.
- Buildroot images are small (typically 20–50 MB) and fast to build.
- QEMU can run the buildroot image for testing without real hardware.
- Porting to a new SBC is primarily buildroot configuration, not driver
  development.
- Boot time target of 3–5 seconds is achievable with careful kernel config;
  measured and optimized as part of the hardware phase.
