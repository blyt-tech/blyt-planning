## Spike J — extends fc32-spike-i with master-hook composition + DAP/GDB stubs.
##
## Layered on spike-i so the cart binaries / libconsole.so / multi-dynload
## rv32emu / luac are reused as-is. Only libconsolelua.so is rebuilt with
## the master_hook dispatcher; case_c_dbg builds its three Lua variants.

ARG SPIKE_I_IMAGE=fc32-spike-i
FROM ${SPIKE_I_IMAGE} AS builder

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        gdb-multiarch nodejs npm python3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /spike-j

## Master-hook + dap_server + gdb_stub source
COPY lib/  lib/
COPY host/ host/

## Five compile-time variants of libconsolelua.so for Stage 1 step 4
## overhead measurement. The default build (lib/build/libconsolelua.so) is
## the production dev-mode config (all flags on); the four named variants
## live under variants/<name>/libconsolelua.so.
RUN make -C lib OUTDIR=/spike-j/lib/build VARIANT=default all -j$(nproc) && \
    cp lib/build/libconsolelua.so lib/libconsolelua.so

RUN for v in master-off master-budget master-budget-dap-idle master-throttle; do \
        mkdir -p /spike-j/variants/$v && \
        make -C lib OUTDIR=/spike-j/variants/$v VARIANT=$v -j$(nproc) && \
        cp /spike-j/variants/$v/libconsolelua.so /spike-j/variants/$v.so ; \
    done

## Reuse spike-i's libconsole.so verbatim — it has no Lua surface.
RUN cp /spike-i/lib/libconsole.so lib/libconsole.so

## Build case_c_dbg cart variants (shifted, deleted) using the new libconsolelua.
COPY cases/case_c_dbg/ cases/case_c_dbg/
RUN make -C cases/case_c_dbg OUTDIR=/spike-j/cases/case_c_dbg/build all -j$(nproc)

## Symlink case_b / case_c into spike-j (build artifacts come from spike-i)
RUN mkdir -p /spike-j/cases/case_b /spike-j/cases/case_c && \
    cp /spike-i/cases/case_b/cart_b /spike-j/cases/case_b/cart_b && \
    cp /spike-i/cases/case_c/cart_c /spike-j/cases/case_c/cart_c

## Build the standalone Lua-direct host harness used for Stage 2 / 4 DAP tests.
## Static link of libconsolelua's source against the host's lua VM, with the
## master_hook dispatcher and dap_server pthread.
RUN apt-get update && apt-get install -y --no-install-recommends \
        gcc libc6-dev liblua5.4-dev pkg-config \
    && rm -rf /var/lib/apt/lists/*

## The host build uses the docker image's host gcc (x86_64/aarch64) — not the
## RV32 cross — because the DAP server pthread runs in the host process. Lua
## headers come from /spike-i/lua/src (32-bit configured) but we link against
## the system lua5.4 for ease of host build. The protocol seam is identical.
RUN cd /spike-i/lua/src && \
    gcc -O2 -DLUA_USE_LINUX -DMASTER_HOOK_HOST_NOW \
        -I/spike-i/lua/src -I/spike-j/lib -I/spike-j/host \
        -o /usr/local/bin/dap_lua_host \
        /spike-j/host/dap_lua_host.c \
        /spike-j/host/dap_server.c \
        /spike-j/lib/master_hook.c \
        $(ls *.c | grep -vE '^(lua|luac)\.c$') \
        -lm -ldl -lpthread

## Build the standalone GDB stub harness for Stage 3 / 5 (smoke-tests the
## protocol surface; full rv32emu integration is a follow-up engineering item).
RUN gcc -O2 -I/spike-j/host \
        -o /usr/local/bin/gdb_stub_host \
        /spike-j/host/gdb_stub_host.c \
        /spike-j/host/gdb_stub.c \
        -lpthread || echo "(gdb_stub_host omitted — see host/gdb_stub_host.c)"

## Test harnesses
RUN mkdir -p /spike-j/host/node_modules
COPY host/dap_test.js host/gdb_test.py /spike-j/host/
RUN chmod +x /spike-j/host/gdb_test.py
