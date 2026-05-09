## Spike P — heap allocator, atomics, and on_load two-category validation.
##
## Starts from the spike-k image (same base as spike-o) to get rv32emu and
## the cross-toolchain, then installs the same Rust nightly as spike-o and
## copies spike-o's pre-built artifacts (lib, multi-dynload-patched rv32emu).
## This avoids rebuilding spike-k/spike-o from source while keeping a clean
## builder layer with Rust available.
##
## Build prerequisites:
##   • fc32-spike-k-{arm64,amd64} images must exist.
##   • fc32-spike-o-{arm64,amd64} images must exist.
##   • DOCKER_BUILDKIT=1 (default in modern Docker).
##   • Named build context:
##       spike-i=../spike-i  (for multi-dynload patch; Makefile passes this)
##
## Pass --build-arg SPIKE_K_IMAGE=fc32-spike-k-amd64 for the amd64 variant.

ARG SPIKE_K_IMAGE=fc32-spike-k-arm64
ARG SPIKE_O_IMAGE=fc32-spike-o-arm64

# Pull the patched rv32emu and spike-o lib from the spike-o runtime image.
FROM ${SPIKE_O_IMAGE} AS spike-o-rt

# ── Builder: spike-k + Rust + spike-p sources ─────────────────────────────────
FROM ${SPIKE_K_IMAGE} AS builder

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    curl \
    && rm -rf /var/lib/apt/lists/*

# Copy the multi-dynload-patched rv32emu from spike-o (already compiled).
COPY --from=spike-o-rt /spike-a/rv32emu/build/rv32emu /spike-a/rv32emu/build/rv32emu

# ── Rebuild rv32emu with A extension enabled ─────────────────────────────────
# spike-k ships rv32emu built WITHOUT CONFIG_EXT_A (not needed for benchmarks).
# Spike P needs A extension to execute AMO/LR-SC instructions emitted by our
# atomics cart.  Apply spike-o's multi-dynload patch (-L flag) and enable
# CONFIG_EXT_A, then rebuild.
COPY --from=spike-i patches/ /spike-p/patches/
RUN sed -i 's/# CONFIG_EXT_A is not set/CONFIG_EXT_A=y/' /spike-a/rv32emu/.config && \
    cd /spike-a/rv32emu && \
    python3 /spike-p/patches/apply-multi-dynload.py && \
    make OUT=build -j"$(nproc)"

# ── Rust toolchain — same version as spike-o ─────────────────────────────────
ENV CARGO_HOME=/root/.cargo
ENV RUSTUP_HOME=/root/.rustup
ENV PATH="/root/.cargo/bin:${PATH}"

RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | \
    sh -s -- -y --default-toolchain nightly-2025-08-01 --no-modify-path

RUN rustup target add riscv32imafc-unknown-none-elf || true
RUN rustup component add rust-src

WORKDIR /spike-p

# ── Copy spike-p sources ──────────────────────────────────────────────────────
COPY sdk/        /spike-p/sdk/
COPY lib/        /spike-p/lib/
COPY cart/       /spike-p/cart/

# ── Build stub library ───────────────────────────────────────────────────────
RUN make -C /spike-p/lib OUTDIR=/spike-p/lib/build all -j"$(nproc)"

# ── Build SDK (crt0.o) ───────────────────────────────────────────────────────
RUN make -C /spike-p/sdk OUTDIR=/spike-p/sdk/build all -j"$(nproc)"

# ── Pre-build blyt32 SDK crate ───────────────────────────────────────────────
RUN cd /spike-p/sdk/blyt32 && \
    cargo build \
        -Zbuild-std=core,panic_abort \
        --target riscv32imafc-unknown-none-elf \
        --release

# ── Stage 1: atomics cart (HEAP_SIZE=0, no alloc) ────────────────────────────
RUN cd /spike-p/cart && \
    cargo build \
        -Zbuild-std=core,panic_abort \
        --target riscv32imafc-unknown-none-elf \
        --release \
        --features atomics

RUN mkdir -p /spike-p/elfs
RUN riscv64-linux-gnu-gcc \
        -march=rv32imfc_zicsr -mabi=ilp32f \
        -nostdlib -no-pie \
        -Wl,--build-id=none \
        -Wl,--export-dynamic \
        -Wl,--dynamic-linker,/lib/ld-musl-riscv32.so.1 \
        -Wl,-rpath,/spike-p/lib/build \
        -Wl,-rpath-link,/spike-p/lib/build \
        -T /spike-p/sdk/cart.ld \
        -Wl,--defsym,HEAP_SIZE=0 \
        /spike-p/sdk/build/crt0.o \
        -Wl,-u,fc_cart_init -Wl,-u,fc_cart_update -Wl,-u,fc_cart_draw \
        /spike-p/cart/target/riscv32imafc-unknown-none-elf/release/libcart.a \
        -L/spike-p/lib/build -lconsole_spike_p \
        -o /spike-p/elfs/atomics_cart.elf

# ── Stage 2: heap smoke cart (HEAP_SIZE=65536, with alloc) ───────────────────
RUN cd /spike-p/cart && \
    cargo build \
        -Zbuild-std=core,alloc,panic_abort \
        --target riscv32imafc-unknown-none-elf \
        --release \
        --features heap_smoke

RUN riscv64-linux-gnu-gcc \
        -march=rv32imfc_zicsr -mabi=ilp32f \
        -nostdlib -no-pie \
        -Wl,--build-id=none \
        -Wl,--export-dynamic \
        -Wl,--dynamic-linker,/lib/ld-musl-riscv32.so.1 \
        -Wl,-rpath,/spike-p/lib/build \
        -Wl,-rpath-link,/spike-p/lib/build \
        -T /spike-p/sdk/cart.ld \
        -Wl,--defsym,HEAP_SIZE=65536 \
        /spike-p/sdk/build/crt0.o \
        -Wl,-u,fc_cart_init -Wl,-u,fc_cart_update -Wl,-u,fc_cart_draw \
        /spike-p/cart/target/riscv32imafc-unknown-none-elf/release/libcart.a \
        -L/spike-p/lib/build -lconsole_spike_p \
        -o /spike-p/elfs/heap_smoke_cart.elf

# ── Stage 3/4: alloc cart (HEAP_SIZE=131072) ─────────────────────────────────
RUN cd /spike-p/cart && \
    cargo build \
        -Zbuild-std=core,alloc,panic_abort \
        --target riscv32imafc-unknown-none-elf \
        --release

RUN riscv64-linux-gnu-gcc \
        -march=rv32imfc_zicsr -mabi=ilp32f \
        -nostdlib -no-pie \
        -Wl,--build-id=none \
        -Wl,--export-dynamic \
        -Wl,--dynamic-linker,/lib/ld-musl-riscv32.so.1 \
        -Wl,-rpath,/spike-p/lib/build \
        -Wl,-rpath-link,/spike-p/lib/build \
        -T /spike-p/sdk/cart.ld \
        -Wl,--defsym,HEAP_SIZE=131072 \
        /spike-p/sdk/build/crt0.o \
        -Wl,-u,fc_cart_init -Wl,-u,fc_cart_update -Wl,-u,fc_cart_draw \
        /spike-p/cart/target/riscv32imafc-unknown-none-elf/release/libcart.a \
        -L/spike-p/lib/build -lconsole_spike_p \
        -o /spike-p/elfs/alloc_cart.elf

# ── ELF sanity sweep ─────────────────────────────────────────────────────────
RUN for f in /spike-p/elfs/*.elf; do \
        echo "== $f =="; \
        riscv64-linux-gnu-readelf -h "$f" | grep -E "(Class|Machine):"; \
    done

# ── Disassembly: confirm A-extension opcodes in atomics cart ─────────────────
# The host objdump (riscv64-linux-gnu) lacks A-extension decode support and
# renders AMO/LR-SC instructions as raw .insn directives.  AMO opcode is
# 0b0101111 (opcode[6:0] = 0x2F).  The lower byte of any AMO instruction is
# either 0x2F (rd even) or 0xAF (rd odd), so grep for hex values ending in
# [2a]f to detect AMO/LR/SC instructions.
RUN riscv64-linux-gnu-objdump -d /spike-p/elfs/atomics_cart.elf \
    | grep -Ei '0x[0-9a-f]+[2a]f$' \
    | tee /spike-p/elfs/atomics-disasm.txt
RUN test -s /spike-p/elfs/atomics-disasm.txt \
    || (echo "FAIL: no A-extension opcodes found in atomics_cart.elf"; exit 1)

# ── ELF hash ─────────────────────────────────────────────────────────────────
RUN sha256sum /spike-p/elfs/*.elf | sort > /spike-p/elfs/elfs.builder.sha256

# ── Runtime stage ─────────────────────────────────────────────────────────────
FROM ${SPIKE_K_IMAGE}
WORKDIR /spike-p
RUN mkdir -p /spike-p/elfs /spike-p/lib/build
COPY --from=builder /spike-p/elfs/                             ./elfs/
COPY --from=builder /spike-p/lib/build/libconsole_spike_p.so  ./lib/build/libconsole_spike_p.so
# Use spike-p's rebuilt rv32emu (with CONFIG_EXT_A + multi-dynload patch).
COPY --from=builder /spike-a/rv32emu/build/rv32emu             /spike-a/rv32emu/build/rv32emu
