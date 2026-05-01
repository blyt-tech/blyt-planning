## Spike B build environment.
## Builds on top of the spike-a image so we reuse its rv32emu binary,
## SoftFloat library, and RISC-V toolchain.  We add Lua 5.4 source and
## compile per-benchmark cart ELFs.
##
## Pass --build-arg SPIKE_A_IMAGE=fc32-spike-a-capped to use the capped
## rv32emu (ADR-0082 MIPS cap baked in).

ARG SPIKE_A_IMAGE=fc32-spike-a
FROM ${SPIKE_A_IMAGE} AS builder

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    wget \
    xxd \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /spike-b

# ── Lua 5.4 ──────────────────────────────────────────────────────────────────
ARG LUA_VERSION=5.4.7
RUN wget -q https://www.lua.org/ftp/lua-${LUA_VERSION}.tar.gz \
    && tar xf lua-${LUA_VERSION}.tar.gz \
    && mv lua-${LUA_VERSION} lua \
    && rm lua-${LUA_VERSION}.tar.gz \
    && sed -i 's|^#define LUA_32BITS\([[:space:]]\+\)0$|#define LUA_32BITS\11|' \
            lua/src/luaconf.h \
    && grep '^#define LUA_32BITS' lua/src/luaconf.h

# ── port + benchmarks ────────────────────────────────────────────────────────
COPY ports/    ports/
COPY benchmarks/ benchmarks/

RUN make -C ports/rv32emu all -j"$(nproc)"

# Sanity check: at least one cart ELF exists, RV32 ELFCLASS32, EM_RISCV.
RUN ls ports/rv32emu/build/lua_cart_*.elf | head -1 | \
    xargs riscv64-linux-gnu-readelf -h | grep -E "(Class|Machine):"

# ── runtime stage ────────────────────────────────────────────────────────────
ARG SPIKE_A_IMAGE=fc32-spike-a
FROM ${SPIKE_A_IMAGE}
WORKDIR /spike-b
COPY --from=builder /spike-b/ports/rv32emu/build/lua_cart_*.elf  ./elfs/
COPY --from=builder /spike-b/ports/rv32emu/build/doom_tick_c.elf ./elfs/
