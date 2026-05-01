## Spike D build environment.
## Builds on top of a spike-b image so we reuse:
##   - the rv32emu binary (from spike-a, layered into spike-b)
##   - the SoftFloat archive (from spike-a)
##   - the vendored Lua 5.4 source (from spike-b)
##   - lua_runtime.c, lua_init_libs.c, crt0.S, softfloat-glue.c (from spike-b)
##
## Pass --build-arg SPIKE_B_IMAGE=fc32-spike-b-amd64 to build the amd64
## variant; default points at the arm64 image.  Both images share the
## same source tree — `--platform` is what differs.

ARG SPIKE_B_IMAGE=fc32-spike-b
FROM ${SPIKE_B_IMAGE} AS builder

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    wget \
    xxd \
    coreutils \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /spike-d

# ── musl 1.2.5 sources ───────────────────────────────────────────────────────
# Pinned by version + SHA256.  The cross-host bit-identity property of the
# spike turns on this URL+hash being the same on both host platforms, so
# any version drift is a spike-breaking change and must update both.
ARG MUSL_VERSION=1.2.5
ARG MUSL_SHA256=a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4
RUN wget -q https://musl.libc.org/releases/musl-${MUSL_VERSION}.tar.gz \
    && echo "${MUSL_SHA256}  musl-${MUSL_VERSION}.tar.gz" | sha256sum -c - \
    && tar xf musl-${MUSL_VERSION}.tar.gz \
    && mv musl-${MUSL_VERSION} musl \
    && rm musl-${MUSL_VERSION}.tar.gz

# ── spike-d sources ──────────────────────────────────────────────────────────
COPY cart_runtime/ cart_runtime/
COPY ports/        ports/
COPY workloads/    workloads/
COPY lib/          lib/

# ── build libm-rv32.a from vendored musl ─────────────────────────────────────
RUN make -C lib/musl-libm \
        MUSL_SRC_DIR=/spike-d/musl \
        OUTDIR=/spike-d/lib/musl-libm/build \
        all -j"$(nproc)"

# Sanity check: the archive exists and has the float transcendentals we
# care about.  If `sinf` is missing here, the cart link will fail anyway,
# so we'd rather fail at the smaller archive build step.
RUN riscv64-linux-gnu-ar t /spike-d/lib/musl-libm/build/libm-rv32.a | \
    grep -E '^(sinf|cosf|expf|powf|atan2f)\.o$$'

# ── build the three Spike D cart ELFs ────────────────────────────────────────
RUN make -C ports/rv32emu \
        SPIKE_A_DIR=/spike-a \
        SPIKE_B_DIR=/spike-b \
        SPIKE_D_DIR=/spike-d \
        all -j"$(nproc)"

# Sanity check: every cart ELF exists and is RV32 (ELFCLASS32, EM_RISCV).
RUN for f in ports/rv32emu/build/lua_cart_det_*.elf ports/rv32emu/build/whetstone.elf ; do \
        echo "== $f ==" ; \
        riscv64-linux-gnu-readelf -h "$f" | grep -E "(Class|Machine):" ; \
    done

# ── runtime stage ────────────────────────────────────────────────────────────
ARG SPIKE_B_IMAGE=fc32-spike-b
FROM ${SPIKE_B_IMAGE}
WORKDIR /spike-d
COPY --from=builder /spike-d/ports/rv32emu/build/lua_cart_det_*.elf  ./elfs/
COPY --from=builder /spike-d/ports/rv32emu/build/whetstone.elf       ./elfs/
COPY --from=builder /spike-d/lib/musl-libm/build/libm-rv32.a         ./elfs/
