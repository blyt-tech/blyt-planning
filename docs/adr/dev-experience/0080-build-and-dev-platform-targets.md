# ADR-0080: Build and development platform targets

## Status
Accepted.
Amended by ADR-0127 (macOS SDK is a universal binary covering ARM64 + x86-64;
Intel Mac is now a supported platform).

## Context

Two distinct platform questions need answering: where can the toolchain
*build* a cart, and where can a developer *work* interactively (edit, run,
iterate). These have different requirements and different support tiers.

## Decision

### Build platforms

**A build platform is any host capable of running the Rust-based packer, the
fc32 emulator (used to run the RV32IMAFC-native luac at build time — see
ADR-0109), and a cross-compiler targeting RV32IMAFC.**

No display is required. The packer is a CLI tool; the runtime is not
invoked during a build. This means:

- Headless CI environments (GitHub Actions, self-hosted runners) are
  first-class build targets.
- Servers and Chromebook/RPi devices in headless configurations can build
  carts without a desktop environment.
- The cross-compilation toolchain (RISC-V GCC or Clang targeting RV32IMAFC)
  must be installable on the build platform; pre-built toolchain binaries
  are distributed for supported platforms.

In practice, any platform where the Rust toolchain runs is a build
platform. No explicit build-platform exclusions are made beyond what the
Rust toolchain itself supports.

### Development platforms (v1)

A development platform supports the full interactive loop: editor, packer
watch mode, runtime with display, debugger.

**Supported dev platforms for v1:**

| Platform | Architecture | Notes |
|----------|-------------|-------|
| macOS | Universal (ARM64 + x86-64) | Single universal binary download covers both Apple Silicon and Intel Macs. |
| Windows | x86-64 | Standard desktop/laptop Windows. |
| Linux | x86-64 | Standard desktop distributions (Ubuntu, Fedora, Debian, Arch). |
| Linux | ARM64 | See explicitly targeted hardware below. |

**Explicitly targeted dev hardware:**

- **Raspberry Pi 4 / Pi 5**: ARM64 Cortex-A72/A76, runs desktop Linux.
  A developer can write and test carts on the same hardware class as the
  target device. Pi 400 and Pi 500 (integrated keyboard form factors) are
  included.
- **Chromebook (via ChromeOS Linux / Crostini)**: budget and
  student-friendly development machines. ChromeOS Linux provides a Debian
  environment with VS Code, the terminal, and (on most recent Chromebooks)
  GPU-accelerated graphics via Virtio-GPU. Both x86-64 and ARM64 Chromebook
  variants are included.

These targets are explicitly tested and documented in setup guides, not
merely "probably works."

### What is not a supported dev platform in v1

- Windows ARM (Surface Pro X, Qualcomm laptops) — may work but untested.
- Browser-based development environments (GitHub Codespaces, Gitpod) —
  the packer runs headless but the runtime requires display; deferred.
- Mobile operating systems (iOS, Android) as dev hosts.

## Consequences

- CI pipelines run on standard Linux x86-64 runners without display
  dependencies. The packer and cross-compiler are the only toolchain
  requirements for automated build and pack validation.
- The ARM64 Linux tier (RPi, Chromebook ARM) means the Rust packer and
  pre-built toolchain binaries must be provided for `aarch64-linux`. This
  is a first-class build target, not an afterthought.
- The macOS SDK download is a universal binary (ARM64 + x86-64), produced
  by lipo from per-architecture builds. LLVM's official releases already
  ship as universal macOS binaries; `blyt` requires two Cargo target
  builds (`aarch64-apple-darwin` + `x86_64-apple-darwin`) and a lipo step.
  Intel Mac users get a native binary with no special casing at runtime.
- Chromebook and RPi targeting signals that the toolchain is accessible
  on budget hardware. The packer performance targets in ADR-0044 are
  defined for mainstream desktop hardware; a somewhat slower dev cycle
  on the lowest tier (Pi 4, entry Chromebook) is acceptable. The goal
  is a usable loop, not identical iteration times across all tiers.
- Windows ARM and Intel Mac users are not blocked (Rust and the packer
  will likely work) but receive no official support or setup documentation
  in v1.
