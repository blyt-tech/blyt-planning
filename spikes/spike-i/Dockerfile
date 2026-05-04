## Spike I — four cart cases × emulator/native/WASM targets
## Base: fc32-spike-c (rv32emu with dynamic loader, Lua 5.4.7, cross-toolchain)

ARG SPIKE_C_IMAGE=fc32-spike-c
ARG SPIKE_H_IMAGE=fc32-spike-h
FROM ${SPIKE_C_IMAGE} AS builder

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    python3 wget ca-certificates \
    && rm -rf /var/lib/apt/lists/*

## xxd converts the .luac into a C include. luac itself is built below from
## the patched 32-bit Lua source so the bytecode matches the runtime
## libconsolelua's lua_Integer/lua_Number sizes.
RUN apt-get update && apt-get install -y --no-install-recommends xxd \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /spike-i

## Lua 5.4.7 source — needed for libconsolelua.so build and Lua C API headers
ARG LUA_VERSION=5.4.7
RUN wget -q https://www.lua.org/ftp/lua-${LUA_VERSION}.tar.gz \
    && tar xf lua-${LUA_VERSION}.tar.gz \
    && mv lua-${LUA_VERSION} lua \
    && rm lua-${LUA_VERSION}.tar.gz \
    && sed -i 's/^#define LUA_32BITS\([[:space:]]\+\)0$/#define LUA_32BITS\11/' \
            lua/src/luaconf.h \
    && grep '^#define LUA_32BITS' lua/src/luaconf.h

## Build a host-native luac matching the 32-bit Lua config so produced bytecode
## (lua_Integer = int, lua_Number = float) loads in libconsolelua at runtime.
RUN apt-get update && apt-get install -y --no-install-recommends gcc libc6-dev \
    && rm -rf /var/lib/apt/lists/* \
    && cd lua/src \
    && gcc -O2 -o /usr/local/bin/luac \
        $(ls *.c | grep -v '^lua\.c$') \
        -lm \
    && /usr/local/bin/luac -v

## Build the SDK crt0, libconsole.so, libconsolelua.so
COPY sdk/   sdk/
COPY lib/   lib/
RUN make -C sdk  OUTDIR=/spike-i/sdk/build  all -j$(nproc)
RUN make -C lib  OUTDIR=/spike-i/lib/build  all -j$(nproc) \
    && cp lib/build/libconsole.so    lib/libconsole.so \
    && cp lib/build/libconsolelua.so lib/libconsolelua.so

## Sanity check libraries
RUN riscv64-linux-gnu-readelf -h lib/libconsole.so | grep -E "(Class|Type|Machine):" \
    && riscv64-linux-gnu-readelf -d lib/libconsole.so | grep SONAME \
    && riscv64-linux-gnu-nm -D lib/libconsole.so | grep fc_console_main \
    && riscv64-linux-gnu-nm -D lib/libconsole.so | grep -E "fc_cart_init" \
    && riscv64-linux-gnu-readelf -h lib/libconsolelua.so | grep -E "(Class|Type|Machine):" \
    && riscv64-linux-gnu-nm -D lib/libconsolelua.so | grep fc_cart_init

## Patch rv32emu with multi-library loader
COPY patches/ patches/
RUN cd /spike-a/rv32emu && python3 /spike-i/patches/apply-multi-dynload.py
RUN make -C /spike-a/rv32emu OUT=build -j$(nproc)

## Build all four cart cases
COPY cases/  cases/
RUN make -C cases/case_a OUTDIR=/spike-i/cases/case_a/build all -j$(nproc)
RUN make -C cases/case_b OUTDIR=/spike-i/cases/case_b/build all -j$(nproc)
RUN make -C cases/case_c OUTDIR=/spike-i/cases/case_c/build all -j$(nproc)
RUN make -C cases/case_d OUTDIR=/spike-i/cases/case_d/build all -j$(nproc)

## Stage 3 native-target prep: pull a musl RV32 dynamic linker from the
## fc32-spike-h image (which carries the musl.cc prebuilt cross-toolchain)
## and install it at the path PT_INTERP expects. The libs themselves have
## no libc dependencies, so any working RV32 dynamic linker is sufficient.
FROM ${SPIKE_H_IMAGE} AS musl-source

FROM ${SPIKE_C_IMAGE}
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    qemu-user-static xxd \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /spike-i
COPY --from=builder /spike-a/rv32emu/build/rv32emu      /spike-a/rv32emu/build/rv32emu
COPY --from=builder /spike-i/lib/libconsole.so          /spike-i/lib/libconsole.so
COPY --from=builder /spike-i/lib/libconsolelua.so       /spike-i/lib/libconsolelua.so
COPY --from=builder /spike-i/cases/case_a/build/cart_a  /spike-i/cases/case_a/cart_a
COPY --from=builder /spike-i/cases/case_b/build/cart_b  /spike-i/cases/case_b/cart_b
COPY --from=builder /spike-i/cases/case_c/build/cart_c  /spike-i/cases/case_c/cart_c
COPY --from=builder /spike-i/cases/case_d/build/cart_d  /spike-i/cases/case_d/cart_d

## musl ships ld-musl-riscv32.so.1 as a symlink to libc.so (the dynamic
## linker and libc are the same binary in musl). Copy the libc.so target
## directly to the PT_INTERP path.
COPY --from=musl-source \
    /opt/riscv32/riscv32-linux-musl/lib/libc.so \
    /lib/ld-musl-riscv32.so.1

## Stage 5 build inputs — Lua source tree + the host-native 32-bit luac binary,
## so the Lua-direct WASM build can compile the Lua VM and produce bytecode
## that matches the runtime VM's lua_Integer/lua_Number sizes.
COPY --from=builder /spike-i/lua/src/ /spike-i/lua/src/
COPY --from=builder /usr/local/bin/luac /usr/local/bin/luac
## Cart Lua sources for cases c and d, plus case d's C-side modules.
COPY --from=builder /spike-i/cases/case_c/main.lua          /spike-i/cases/case_c/main.lua
COPY --from=builder /spike-i/cases/case_d/main.lua          /spike-i/cases/case_d/main.lua
COPY --from=builder /spike-i/cases/case_d/cart_lua_modules.c /spike-i/cases/case_d/cart_lua_modules.c
COPY --from=builder /spike-i/cases/case_d/mylib.c            /spike-i/cases/case_d/mylib.c
## Common include dir referenced by libconsolelua sources
COPY --from=builder /spike-i/lib/include/                    /spike-i/lib/include/
