## Spike N — hot-reload mechanism validation.
##
## Inherits Spike M's image (Lua VM + coroutine mechanism + flatten lib +
## cart_runtime + rv32emu) and layers Spike N's migration machinery,
## hot-reload diagnostic surface, edit-suite ELFs, and harness tools.
##
## Pass --build-arg SPIKE_M_IMAGE=fc32-spike-m-amd64 for the amd64 variant.
##
## Build prerequisites:
##   • DOCKER_BUILDKIT=1 (default in modern Docker).
##   • Named build contexts "spike-m", "spike-k", "spike-d" pointing at
##     ../spike-m, ../spike-k, ../spike-d respectively.
##     The Makefile passes these automatically; manual invocations need:
##     --build-context spike-m=../spike-m
##     --build-context spike-k=../spike-k
##     --build-context spike-d=../spike-d

ARG SPIKE_M_IMAGE=fc32-spike-m-arm64
FROM ${SPIKE_M_IMAGE} AS builder

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    xxd \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /spike-n

# ── copy Spike N sources ──────────────────────────────────────────────────────
COPY cart_runtime/ /spike-n/cart_runtime/
COPY ports/        /spike-n/ports/
COPY workloads/    /spike-n/workloads/
COPY lib/          /spike-n/lib/
COPY harness/      /spike-n/harness/

# ── reuse spike-m's libm-rv32.a ───────────────────────────────────────────────
RUN mkdir -p /spike-n/lib/musl-libm/build
RUN cp /spike-d/elfs/libm-rv32.a /spike-n/lib/musl-libm/build/libm-rv32.a

# ── pull spike-d / spike-k / spike-m sources that N reuses ───────────────────
COPY --from=spike-d cart_runtime/digest.c                /spike-n/cart_runtime/digest.c
COPY --from=spike-d cart_runtime/digest.h                /spike-n/cart_runtime/digest.h
COPY --from=spike-d cart_runtime/pcg32.c                 /spike-n/cart_runtime/pcg32.c
COPY --from=spike-d cart_runtime/pcg32.h                 /spike-n/cart_runtime/pcg32.h
COPY --from=spike-d cart_runtime/nan_canon.h             /spike-n/cart_runtime/nan_canon.h
COPY --from=spike-d cart_runtime/frame_state.h           /spike-n/cart_runtime/frame_state.h
COPY --from=spike-d ports/rv32emu/include/math.h         /spike-n/ports/rv32emu/include/math.h
COPY --from=spike-k cart_runtime/save_state.c            /spike-n/cart_runtime/save_state.c
COPY --from=spike-k cart_runtime/save_state.h            /spike-n/cart_runtime/save_state.h
COPY --from=spike-k cart_runtime/runtime_tracked.c       /spike-n/cart_runtime/runtime_tracked.c
COPY --from=spike-k cart_runtime/runtime_tracked.h       /spike-n/cart_runtime/runtime_tracked.h
COPY --from=spike-k cart_runtime/region_frame_state.c    /spike-n/cart_runtime/region_frame_state.c
COPY --from=spike-k cart_runtime/region_frame_state.h    /spike-n/cart_runtime/region_frame_state.h
COPY --from=spike-k cart_runtime/save_io.c               /spike-n/cart_runtime/save_io.c
COPY --from=spike-k cart_runtime/save_io.h               /spike-n/cart_runtime/save_io.h
COPY --from=spike-k cart_runtime/cart_state_lua_simple.c /spike-n/cart_runtime/cart_state_lua_simple.c
COPY --from=spike-k cart_runtime/cart_state_lua_simple.h /spike-n/cart_runtime/cart_state_lua_simple.h
COPY --from=spike-m cart_runtime/region_persistent_scripts.c /spike-n/cart_runtime/region_persistent_scripts.c
COPY --from=spike-m cart_runtime/region_persistent_scripts.h /spike-n/cart_runtime/region_persistent_scripts.h
COPY --from=spike-m lib/lua_table_flatten.c              /spike-n/lib/lua_table_flatten.c
COPY --from=spike-m lib/lua_table_flatten.h              /spike-n/lib/lua_table_flatten.h
COPY --from=spike-m lib/blyt32_coroutine.lua             /spike-n/lib/blyt32_coroutine.lua
COPY --from=spike-m ports/rv32emu/m_driver_save.c        /spike-n/ports/rv32emu/m_driver_save.c
COPY --from=spike-m ports/rv32emu/m_driver_load.c        /spike-n/ports/rv32emu/m_driver_load.c
COPY --from=spike-m ports/rv32emu/m_driver_full.c        /spike-n/ports/rv32emu/m_driver_full.c

# ── build host-side harness tools ────────────────────────────────────────────
RUN gcc -O2 -o /spike-n/harness/edit_driver /spike-n/harness/edit_driver.c
RUN gcc -O2 -o /spike-n/harness/stub_packer /spike-n/harness/stub_packer.c

# ── build rv32 cart ELFs ──────────────────────────────────────────────────────
RUN make -C ports/rv32emu \
        SPIKE_A_DIR=/spike-a \
        SPIKE_B_DIR=/spike-b \
        SPIKE_D_DIR=/spike-d \
        SPIKE_K_DIR=/spike-k \
        SPIKE_M_DIR=/spike-m \
        SPIKE_N_DIR=/spike-n \
        all -j"$(nproc)"

# Sanity check.
RUN for f in ports/rv32emu/build/*.elf; do \
        echo "== $f ==" ; \
        riscv64-linux-gnu-readelf -h "$f" | grep -E "(Class|Machine):" ; \
    done

# ── runtime stage ─────────────────────────────────────────────────────────────
ARG SPIKE_M_IMAGE=fc32-spike-m-arm64
FROM ${SPIKE_M_IMAGE}
WORKDIR /spike-n
COPY --from=builder /spike-n/ports/rv32emu/build/*.elf ./elfs/
COPY --from=builder /spike-n/harness/edit_driver        ./harness/edit_driver
COPY --from=builder /spike-n/harness/stub_packer        ./harness/stub_packer
