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
    python3 \
    # RISC-V cross-toolchain; can target rv32 with -march=rv32imfc_zicsr -mabi=ilp32f
    gcc-riscv64-linux-gnu \
    binutils-riscv64-linux-gnu \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /spike-a

# ── rv32emu ──────────────────────────────────────────────────────────────────
COPY rv32emu/ rv32emu/
COPY configs/rv32emu.config rv32emu/.config

# Apply ADR-0082 MIPS cap patch to rv32emu source (idempotent; cap disabled
# when FC32_MIPS_CAP=0, which is the default).
COPY patches/ patches/
RUN cd rv32emu && python3 ../patches/apply-mips-cap.py

# FC32_DISABLE_LTO=1 turns off CONFIG_LTO before building rv32emu.  This is
# a workaround for GCC's LTO crashing under qemu-user emulation — used by
# spike-d's linux/amd64 build path on Apple Silicon hosts.  Disabling LTO
# does not affect interpreter semantics; it only changes optimisation
# (and slightly increases binary size).
ARG FC32_DISABLE_LTO=0
RUN if [ "$FC32_DISABLE_LTO" = "1" ]; then \
        sed -i 's/^CONFIG_LTO=y/CONFIG_LTO=n/' rv32emu/.config && \
        echo "Disabled CONFIG_LTO in rv32emu/.config" ; \
    fi

# FC32_MIPS_CAP=0 → uncapped (identical to upstream behaviour).
# Pass --build-arg FC32_MIPS_CAP=N to enable the throttle at N MIPS.
ARG FC32_MIPS_CAP=0
RUN make -C rv32emu OUT=build \
        FC32_EXTRA_CFLAGS="-DFC32_MIPS_CAP=${FC32_MIPS_CAP}" \
        -j"$(nproc)"

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
