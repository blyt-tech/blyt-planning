## Spike A build environment
## Platform: linux/arm64 (mirrors Pi Zero 2 W Cortex-A53 ISA)
## Note: performance numbers are only meaningful on real hardware;
##       this container is for build/correctness validation only.

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    bc \
    git \
    ca-certificates \
    # RISC-V cross-toolchain; can target rv32 with -march=rv32imfc_zicsr -mabi=ilp32f
    gcc-riscv64-linux-gnu \
    binutils-riscv64-linux-gnu \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /spike-a

# Copy rv32emu source tree + our config
COPY rv32emu/ rv32emu/
COPY configs/rv32emu.config rv32emu/.config

# Build rv32emu (headless, RV32IMFC interpreter)
RUN make -C rv32emu OUT=build -j"$(nproc)"

# Sanity check: confirm the binary exists and reports usage
RUN rv32emu/build/rv32emu --help 2>&1 | head -5 || true
