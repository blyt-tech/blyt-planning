## Spike C — Lua 5.4 as a versioned RV32IMFC shared library
## Base: fc32-spike-a (rv32emu binary, SoftFloat, RISC-V cross-toolchain)

ARG SPIKE_A_IMAGE=fc32-spike-a
FROM ${SPIKE_A_IMAGE} AS builder

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends wget && rm -rf /var/lib/apt/lists/*

WORKDIR /spike-c

ARG LUA_VERSION=5.4.7
RUN wget -q https://www.lua.org/ftp/lua-${LUA_VERSION}.tar.gz \
    && tar xf lua-${LUA_VERSION}.tar.gz \
    && mv lua-${LUA_VERSION} lua \
    && rm lua-${LUA_VERSION}.tar.gz \
    && sed -i 's|^#define LUA_32BITS\([[:space:]]\+\)0$|#define LUA_32BITS\11|' \
            lua/src/luaconf.h \
    && grep '^#define LUA_32BITS' lua/src/luaconf.h

## Build liblua54.so
COPY lib/ lib/
RUN make -C lib OUTDIR=/spike-c/lib/build all -j$(nproc) \
    && cp lib/build/liblua54.so lib/liblua54.so

## Sanity check: .so is ELF32 DYN EM_RISCV with exported Lua symbols
RUN riscv64-linux-gnu-readelf -h lib/liblua54.so | grep -E "(Class|Type|Machine):" \
    && riscv64-linux-gnu-readelf -d lib/liblua54.so | grep SONAME \
    && riscv64-linux-gnu-nm -D lib/liblua54.so | grep -E "lua_newstate|luaL_dostring|lua_close"

## Patch rv32emu with dynamic loader support
COPY patches/ patches/
RUN cd /spike-a/rv32emu && python3 /spike-c/patches/apply-dynamic-loader.py
RUN make -C /spike-a/rv32emu OUT=build -j$(nproc)

## Build the cart ELF
COPY cart/ cart/
RUN make -C cart OUTDIR=/spike-c/cart/build all -j$(nproc) \
    && cp cart/build/spike_c_cart.elf cart/spike_c_cart.elf

## Sanity check: cart is EXEC/DYN EM_RISCV with DT_NEEDED: liblua54.so
RUN riscv64-linux-gnu-readelf -h cart/spike_c_cart.elf | grep -E "(Class|Type|Machine):" \
    && riscv64-linux-gnu-readelf -d cart/spike_c_cart.elf | grep NEEDED

FROM ${SPIKE_A_IMAGE}
WORKDIR /spike-c
## The patched rv32emu (with -L support) replaces the one from fc32-spike-a
COPY --from=builder /spike-a/rv32emu/build/rv32emu  /spike-a/rv32emu/build/rv32emu
COPY --from=builder /spike-c/lib/liblua54.so        /spike-c/lib/liblua54.so
COPY --from=builder /spike-c/cart/spike_c_cart.elf  /spike-c/cart/spike_c_cart.elf
