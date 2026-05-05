## Spike K — cross-host save-state portability.
##
## Inherits Spike D's image (cross-compiler + Lua + musl libm subset +
## SoftFloat + rv32emu binary) and layers spike-K's cart_runtime + cart
## variants on top.  No new dependencies — every byte that affects
## determinism comes from spike-D's pinned image.
##
## Pass --build-arg SPIKE_D_IMAGE=fc32-spike-d-amd64 to build the amd64
## variant; default points at the arm64 image.
##
## Build prerequisites:
##   • DOCKER_BUILDKIT=1 (default in modern Docker).
##   • A named build context "spike-d" pointing at ../spike-d so the
##     spike-D cart_runtime sources (digest.c/h, pcg32.c/h, nan_canon.h,
##     frame_state.h) and the workloads / math.h shim are reachable via
##     `COPY --from=spike-d`.  The Makefile passes this automatically;
##     manual invocations need: --build-context spike-d=../spike-d.
##   • The spike-D runtime image must already exist (the Makefile builds
##     it as a dependency).

ARG SPIKE_D_IMAGE=fc32-spike-d-arm64
FROM ${SPIKE_D_IMAGE} AS builder

WORKDIR /spike-k

# ── reuse spike-D's libm-rv32.a (same archive, repositioned for spike-K's
#    Makefile path conventions) ──────────────────────────────────────────────
RUN mkdir -p /spike-k/lib/musl-libm/build
RUN cp /spike-d/elfs/libm-rv32.a /spike-k/lib/musl-libm/build/libm-rv32.a

# ── spike-K's own additions (host symlinks to spike-D files are excluded
#    by .dockerignore and brought in below from the spike-d build context) ──
COPY cart_runtime/ /spike-k/cart_runtime/
COPY ports/        /spike-k/ports/
COPY workloads/    /spike-k/workloads/
COPY lib/          /spike-k/lib/

# ── spike-D source files that spike-K reuses verbatim (single source of
#    truth lives in ../spike-d/; brought in here at Docker build time) ──────
COPY --from=spike-d cart_runtime/digest.c       /spike-k/cart_runtime/digest.c
COPY --from=spike-d cart_runtime/digest.h       /spike-k/cart_runtime/digest.h
COPY --from=spike-d cart_runtime/pcg32.c        /spike-k/cart_runtime/pcg32.c
COPY --from=spike-d cart_runtime/pcg32.h        /spike-k/cart_runtime/pcg32.h
COPY --from=spike-d cart_runtime/nan_canon.h    /spike-k/cart_runtime/nan_canon.h
COPY --from=spike-d cart_runtime/frame_state.h  /spike-k/cart_runtime/frame_state.h
COPY --from=spike-d ports/rv32emu/include/math.h /spike-k/ports/rv32emu/include/math.h
COPY --from=spike-d workloads/det_doom_tick.lua     /spike-k/workloads/det_doom_tick.lua
COPY --from=spike-d workloads/det_entity_update.lua /spike-k/workloads/det_entity_update.lua

# ── build the spike-K cart ELFs ──────────────────────────────────────────────
RUN make -C ports/rv32emu \
        SPIKE_A_DIR=/spike-a \
        SPIKE_B_DIR=/spike-b \
        SPIKE_D_DIR=/spike-d \
        SPIKE_K_DIR=/spike-k \
        all -j"$(nproc)"

# Sanity check: every cart ELF exists and is RV32 (ELFCLASS32, EM_RISCV).
RUN for f in ports/rv32emu/build/*.elf ; do \
        echo "== $f ==" ; \
        riscv64-linux-gnu-readelf -h "$f" | grep -E "(Class|Machine):" ; \
    done

# ── runtime stage ────────────────────────────────────────────────────────────
ARG SPIKE_D_IMAGE=fc32-spike-d-arm64
FROM ${SPIKE_D_IMAGE}
WORKDIR /spike-k
COPY --from=builder /spike-k/ports/rv32emu/build/*.elf ./elfs/
