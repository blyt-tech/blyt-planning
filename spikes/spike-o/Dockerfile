## Spike O — Rust cart on riscv32imafc-unknown-none-elf + ADR-0108 SDK validation.
##
## Inherits Spike K's image (rv32emu, cross-toolchain, FNV-1a-64 digest
## infrastructure) and layers:
##   • Rust nightly-2025-08-01 with riscv32imafc-unknown-none-elf target
##   • Extended libconsole stub (image/audio/state-buffer stubs + ABI witness)
##   • blyt32 SDK crate (minimal: handles, buffers, ffi, errors)
##   • Toy Rust cart (stage-1 stub + stage-3 full)
##   • C reference cart for digest gate
##   • Compile-fail guard carts (wrong-buffer + closure-handler)
##
## Build prerequisites:
##   • DOCKER_BUILDKIT=1 (default in modern Docker).
##   • Named build contexts:
##       spike-k=../spike-k   (sources, not image — for WORKDIR convention only)
##       spike-d=../spike-d   (digest infrastructure sources)
##       spike-i=../spike-i   (crt0.S, cases/cart.ld from source tree)
##     The Makefile passes these automatically.
##   • fc32-spike-k-{arm64,amd64} images must already exist.
##
## Pass --build-arg SPIKE_K_IMAGE=fc32-spike-k-amd64 for the amd64 variant.

ARG SPIKE_K_IMAGE=fc32-spike-k-arm64
FROM ${SPIKE_K_IMAGE} AS builder

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    curl \
    && rm -rf /var/lib/apt/lists/*

# ── Patch rv32emu with Spike I's multi-dynload loader (-L flag) ───────────────
# Spike K inherits from spike-d → spike-b → spike-a.  None of those applied
# the multi-dynload patch (spike-c's -L patch took a different branch).
# Apply spike-i's patch here and recompile rv32emu for this platform.
COPY --from=spike-i patches/ /spike-o/patches/
RUN cd /spike-a/rv32emu && \
    python3 /spike-o/patches/apply-multi-dynload.py && \
    make OUT=build -j"$(nproc)"

WORKDIR /spike-o

# ── Rust toolchain (nightly — required for -Zbuild-std with Tier-3 target) ───
# riscv32imafc-unknown-none-elf is Tier 3; no prebuilt std.
# rust-src + build-std rebuilds core + panic_abort from source.
ENV CARGO_HOME=/root/.cargo
ENV RUSTUP_HOME=/root/.rustup
ENV PATH="/root/.cargo/bin:${PATH}"

RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | \
    sh -s -- -y --default-toolchain nightly-2025-08-01 --no-modify-path

# Discover which riscv32 targets this nightly knows (diagnostic — never fails).
RUN rustup target list --installed; \
    rustc --print=target-list 2>/dev/null | grep riscv32 || true

# Add target (gracefully fails for Tier 3 / unknown targets; build-std covers it).
RUN rustup target add riscv32imafc-unknown-none-elf || true
# rust-src is required for -Zbuild-std=core,panic_abort
RUN rustup component add rust-src

# ── Pull Spike I SDK source artefacts via build context ──────────────────────
# Paths are relative to the spike-i source directory (build context, not image).
RUN mkdir -p /spike-o/sdk
COPY --from=spike-i sdk/crt0.S   /spike-o/sdk/crt0.S
COPY --from=spike-i cases/cart.ld /spike-o/sdk/cart.ld

# ── Copy spike-o sources ──────────────────────────────────────────────────────
# COPY sdk/ comes after the spike-i copies; spike-o/sdk/ has only Makefile and
# blyt32/ — no crt0.S or cart.ld — so the spike-i files are not overwritten.
COPY sdk/        /spike-o/sdk/
COPY lib/        /spike-o/lib/
COPY harness/    /spike-o/harness/
COPY cart/       /spike-o/cart/
COPY guard_wrong_buffer/    /spike-o/guard_wrong_buffer/
COPY guard_closure_handler/ /spike-o/guard_closure_handler/

# ── Build extended stub library ───────────────────────────────────────────────
RUN make -C /spike-o/lib OUTDIR=/spike-o/lib/build all -j"$(nproc)"

# ── Build crt0.o ─────────────────────────────────────────────────────────────
RUN make -C /spike-o/sdk OUTDIR=/spike-o/sdk/build all -j"$(nproc)"

# ── Build host-side stub packer (--lang rust mode) ───────────────────────────
RUN gcc -O2 -o /spike-o/harness/stub_packer_rust /spike-o/harness/stub_packer_rust.c

# ── Pre-build blyt32 SDK crate ────────────────────────────────────────────────
RUN cd /spike-o/sdk/blyt32 && \
    cargo build \
        -Zbuild-std=core,panic_abort \
        --target riscv32imafc-unknown-none-elf \
        --release

# ── Stage 1: minimal stub cart (no SDK, no API calls, loader smoke test) ─────
RUN cd /spike-o/cart && \
    cargo build \
        -Zbuild-std=core,panic_abort \
        --target riscv32imafc-unknown-none-elf \
        --release \
        --features stage1

RUN mkdir -p /spike-o/elfs
# -rpath embeds the library search path so rv32emu's dynload finds the stub.
RUN riscv64-linux-gnu-gcc \
        -march=rv32imfc_zicsr -mabi=ilp32f \
        -nostdlib -no-pie \
        -Wl,--build-id=none \
        -Wl,--export-dynamic \
        -Wl,--dynamic-linker,/lib/ld-musl-riscv32.so.1 \
        -Wl,-rpath,/spike-o/lib/build \
        -Wl,-rpath-link,/spike-o/lib/build \
        -T /spike-o/sdk/cart.ld \
        /spike-o/sdk/build/crt0.o \
        -Wl,-u,fc_cart_init -Wl,-u,fc_cart_update -Wl,-u,fc_cart_draw \
        /spike-o/cart/target/riscv32imafc-unknown-none-elf/release/libcart.a \
        -L/spike-o/lib/build -lconsole_spike_o \
        -o /spike-o/elfs/rust_cart_stage1.elf

# Sanity check Stage 1 ELF
RUN riscv64-linux-gnu-readelf -h /spike-o/elfs/rust_cart_stage1.elf \
    | grep -E "(Class|Machine|Type):"
RUN riscv64-linux-gnu-readelf -d /spike-o/elfs/rust_cart_stage1.elf \
    | grep NEEDED || true

# ── Stage 2: ABI-witness cart (raw extern "C" f32 argument) ──────────────────
RUN cd /spike-o/cart && \
    cargo build \
        -Zbuild-std=core,panic_abort \
        --target riscv32imafc-unknown-none-elf \
        --release \
        --features stage2

RUN riscv64-linux-gnu-gcc \
        -march=rv32imfc_zicsr -mabi=ilp32f \
        -nostdlib -no-pie \
        -Wl,--build-id=none \
        -Wl,--export-dynamic \
        -Wl,--dynamic-linker,/lib/ld-musl-riscv32.so.1 \
        -Wl,-rpath,/spike-o/lib/build \
        -Wl,-rpath-link,/spike-o/lib/build \
        -T /spike-o/sdk/cart.ld \
        /spike-o/sdk/build/crt0.o \
        -Wl,-u,fc_cart_init -Wl,-u,fc_cart_update -Wl,-u,fc_cart_draw \
        /spike-o/cart/target/riscv32imafc-unknown-none-elf/release/libcart.a \
        -L/spike-o/lib/build -lconsole_spike_o \
        -o /spike-o/elfs/rust_cart_stage2.elf

# ABI attribute check: capture for comparison with C reference
RUN riscv64-linux-gnu-readelf -A /spike-o/elfs/rust_cart_stage2.elf \
    | tee /spike-o/elfs/rust_cart_stage2.attrs

# ── Stage 3: full SDK cart + C reference ─────────────────────────────────────
RUN cd /spike-o/cart && \
    cargo build \
        -Zbuild-std=core,panic_abort \
        --target riscv32imafc-unknown-none-elf \
        --release

RUN riscv64-linux-gnu-gcc \
        -march=rv32imfc_zicsr -mabi=ilp32f \
        -nostdlib -no-pie \
        -Wl,--build-id=none \
        -Wl,--export-dynamic \
        -Wl,--dynamic-linker,/lib/ld-musl-riscv32.so.1 \
        -Wl,-rpath,/spike-o/lib/build \
        -Wl,-rpath-link,/spike-o/lib/build \
        -T /spike-o/sdk/cart.ld \
        /spike-o/sdk/build/crt0.o \
        -Wl,-u,fc_cart_init -Wl,-u,fc_cart_update -Wl,-u,fc_cart_draw \
        /spike-o/cart/target/riscv32imafc-unknown-none-elf/release/libcart.a \
        -L/spike-o/lib/build -lconsole_spike_o \
        -o /spike-o/elfs/rust_cart.elf

# C reference cart (equivalent API calls in C, same order, same arguments)
RUN riscv64-linux-gnu-gcc \
        -march=rv32imfc_zicsr -mabi=ilp32f \
        -O2 -ffreestanding -nostdlib -no-pie \
        -fno-stack-protector \
        -Wl,--build-id=none \
        -Wl,--export-dynamic \
        -Wl,--dynamic-linker,/lib/ld-musl-riscv32.so.1 \
        -Wl,-rpath,/spike-o/lib/build \
        -Wl,-rpath-link,/spike-o/lib/build \
        -T /spike-o/sdk/cart.ld \
        /spike-o/sdk/build/crt0.o \
        /spike-o/cart/c_reference/det_rust_ref.c \
        -L/spike-o/lib/build -lconsole_spike_o \
        -o /spike-o/elfs/c_ref_cart.elf

# ── Compile-fail guard carts (expected to FAIL; wrapped so Docker continues) ─

# guard_wrong_buffer: FieldHandle<MainBuffer> used with EnemiesBuffer — must fail
RUN cd /spike-o/guard_wrong_buffer && \
    cargo build \
        -Zbuild-std=core,panic_abort \
        --target riscv32imafc-unknown-none-elf \
        --release 2>&1 \
    | tee /spike-o/guard_wrong_buffer/build.stderr; \
    exit 0
RUN test ! -f /spike-o/guard_wrong_buffer/target/riscv32imafc-unknown-none-elf/release/libguard_wrong_buffer.a \
    && echo "guard_wrong_buffer correctly failed to compile (PASS)" \
    || (echo "GUARD FAIL: guard_wrong_buffer compiled when it should not have"; exit 1)

# guard_closure_handler: capturing closure where fn pointer required — must fail
RUN cd /spike-o/guard_closure_handler && \
    cargo build \
        -Zbuild-std=core,panic_abort \
        --target riscv32imafc-unknown-none-elf \
        --release 2>&1 \
    | tee /spike-o/guard_closure_handler/build.stderr; \
    exit 0
RUN test ! -f /spike-o/guard_closure_handler/target/riscv32imafc-unknown-none-elf/release/libguard_closure_handler.a \
    && echo "guard_closure_handler correctly failed to compile (PASS)" \
    || (echo "GUARD FAIL: guard_closure_handler compiled when it should not have"; exit 1)

# ── ELF sanity sweep ─────────────────────────────────────────────────────────
RUN for f in /spike-o/elfs/*.elf; do \
        echo "== $f =="; \
        riscv64-linux-gnu-readelf -h "$f" | grep -E "(Class|Machine):"; \
    done

# ── Runtime stage ─────────────────────────────────────────────────────────────
ARG SPIKE_K_IMAGE=fc32-spike-k-arm64
FROM ${SPIKE_K_IMAGE}
WORKDIR /spike-o
RUN mkdir -p /spike-o/elfs /spike-o/lib/build
COPY --from=builder /spike-o/elfs/                              ./elfs/
COPY --from=builder /spike-o/lib/build/libconsole_spike_o.so   ./lib/build/libconsole_spike_o.so
COPY --from=builder /spike-o/guard_wrong_buffer/build.stderr    ./guard_wrong_buffer.stderr
COPY --from=builder /spike-o/guard_closure_handler/build.stderr ./guard_closure_handler.stderr
COPY --from=builder /spike-o/elfs/rust_cart_stage2.attrs        ./elfs/rust_cart_stage2.attrs
# Copy the newly patched rv32emu binary from builder.
COPY --from=builder /spike-a/rv32emu/build/rv32emu /spike-a/rv32emu/build/rv32emu
