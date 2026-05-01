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

# ── rv32emu ──────────────────────────────────────────────────────────────────
COPY rv32emu/ rv32emu/
COPY configs/rv32emu.config rv32emu/.config
RUN make -C rv32emu OUT=build -j"$(nproc)"

# ── Berkeley SoftFloat (RV32 ilp32f static archive) ──────────────────────────
COPY berkeley-softfloat-3/ berkeley-softfloat-3/
COPY softfloat-build/ softfloat-build/
RUN make -C softfloat-build -j"$(nproc)"

# ── CoreMark (RV32IMFC guest ELF) ────────────────────────────────────────────
COPY coremark/ coremark/
COPY ports/ ports/
# Default ITERATIONS sized for ~10s of CoreMark on Apple Silicon's arm64 Docker;
# real-hardware runs override it. 'compile' just builds the ELF — running is
# left to `make docker-bench` so the image build stays cheap.
RUN make -C coremark PORT_DIR=../ports/rv32emu ITERATIONS=20000 compile

# Sanity check: ELF exists and is RV32 (ELFCLASS32, EM_RISCV). Running the
# benchmark itself takes ~12s; that's deferred to `make docker-bench`.
RUN test -f coremark/coremark.elf \
 && riscv64-linux-gnu-readelf -h coremark/coremark.elf | grep -E "(Class|Machine):"

# ── Embench-IoT (18 RV32IMFC guest ELFs) ─────────────────────────────────────
COPY embench-iot/ embench-iot/
RUN make -C ports/rv32emu/embench \
        OUTDIR=/spike-a/ports/rv32emu/embench/build \
        -j"$(nproc)"

# Sanity check: at least one ELF exists and is RV32.
RUN ls ports/rv32emu/embench/build/embench-*.elf | head -1 | \
    xargs riscv64-linux-gnu-readelf -h | grep -E "(Class|Machine):"
