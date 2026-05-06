## Spike M — managed Lua coroutine save/restore.
##
## Inherits Spike K's image (cart_runtime + save_state + libm-rv32 + Lua
## VM build) and layers spike-M's persistent_scripts region, the
## constrained-shape flattener, the blyt32.coroutine wrapper module,
## and the per-cart drivers.
##
## Pass --build-arg SPIKE_K_IMAGE=fc32-spike-k-amd64 to build the amd64
## variant; default points at the arm64 image.
##
## Build prerequisites:
##   • DOCKER_BUILDKIT=1 (default in modern Docker).
##   • Named build context "spike-k" pointing at ../spike-k so the
##     spike-K cart_runtime sources (digest.c/h, save_state.{c,h},
##     runtime_tracked.{c,h}, region_*, save_io.{c,h},
##     cart_state_lua_simple.{c,h}, frame_state.h, nan_canon.h, pcg32.{c,h},
##     ports/rv32emu/include/math.h) are reachable via `COPY --from=spike-k`.
##     The Makefile passes this automatically; manual invocations need:
##     --build-context spike-k=../spike-k.
##   • The spike-K runtime image must already exist (the Makefile builds
##     it as a dependency).

ARG SPIKE_K_IMAGE=fc32-spike-k-arm64
FROM ${SPIKE_K_IMAGE} AS builder

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    xxd \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /spike-m

# ── reuse spike-D's libm-rv32.a (the spike-K image inherits it at
#    /spike-d/elfs/libm-rv32.a from its FROM ${SPIKE_D_IMAGE} base; copy
#    again into spike-M's path conventions) ────────────────────────────────
RUN mkdir -p /spike-m/lib/musl-libm/build
RUN cp /spike-d/elfs/libm-rv32.a /spike-m/lib/musl-libm/build/libm-rv32.a

# ── spike-M's own additions (host symlinks excluded by .dockerignore) ───────
COPY cart_runtime/ /spike-m/cart_runtime/
COPY ports/        /spike-m/ports/
COPY workloads/    /spike-m/workloads/
COPY lib/          /spike-m/lib/

# ── spike-K source files that spike-M reuses verbatim.  The spike-K
#    repo treats some of these as symlinks into ../spike-d/ and excludes
#    them via .dockerignore — so we pull those from the named "spike-d"
#    build context directly.  The rest (real files in spike-K) come from
#    the named "spike-k" build context. ────────────────────────────────────
COPY --from=spike-d cart_runtime/digest.c                /spike-m/cart_runtime/digest.c
COPY --from=spike-d cart_runtime/digest.h                /spike-m/cart_runtime/digest.h
COPY --from=spike-d cart_runtime/pcg32.c                 /spike-m/cart_runtime/pcg32.c
COPY --from=spike-d cart_runtime/pcg32.h                 /spike-m/cart_runtime/pcg32.h
COPY --from=spike-d cart_runtime/nan_canon.h             /spike-m/cart_runtime/nan_canon.h
COPY --from=spike-d cart_runtime/frame_state.h           /spike-m/cart_runtime/frame_state.h
COPY --from=spike-d ports/rv32emu/include/math.h         /spike-m/ports/rv32emu/include/math.h
COPY --from=spike-k cart_runtime/save_state.c            /spike-m/cart_runtime/save_state.c
COPY --from=spike-k cart_runtime/save_state.h            /spike-m/cart_runtime/save_state.h
COPY --from=spike-k cart_runtime/runtime_tracked.c       /spike-m/cart_runtime/runtime_tracked.c
COPY --from=spike-k cart_runtime/runtime_tracked.h       /spike-m/cart_runtime/runtime_tracked.h
COPY --from=spike-k cart_runtime/region_frame_state.c    /spike-m/cart_runtime/region_frame_state.c
COPY --from=spike-k cart_runtime/region_frame_state.h    /spike-m/cart_runtime/region_frame_state.h
COPY --from=spike-k cart_runtime/save_io.c               /spike-m/cart_runtime/save_io.c
COPY --from=spike-k cart_runtime/save_io.h               /spike-m/cart_runtime/save_io.h
COPY --from=spike-k cart_runtime/cart_state_lua_simple.c /spike-m/cart_runtime/cart_state_lua_simple.c
COPY --from=spike-k cart_runtime/cart_state_lua_simple.h /spike-m/cart_runtime/cart_state_lua_simple.h

# ── build the spike-M cart ELFs ─────────────────────────────────────────────
RUN make -C ports/rv32emu \
        SPIKE_A_DIR=/spike-a \
        SPIKE_B_DIR=/spike-b \
        SPIKE_D_DIR=/spike-d \
        SPIKE_K_DIR=/spike-k \
        SPIKE_M_DIR=/spike-m \
        all -j"$(nproc)"

# Sanity check.
RUN for f in ports/rv32emu/build/*.elf ; do \
        echo "== $f ==" ; \
        riscv64-linux-gnu-readelf -h "$f" | grep -E "(Class|Machine):" ; \
    done

# ── runtime stage ──────────────────────────────────────────────────────────
ARG SPIKE_K_IMAGE=fc32-spike-k-arm64
FROM ${SPIKE_K_IMAGE}
WORKDIR /spike-m
COPY --from=builder /spike-m/ports/rv32emu/build/*.elf ./elfs/
