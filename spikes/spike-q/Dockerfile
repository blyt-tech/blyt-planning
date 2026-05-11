## Spike Q — Lua+Rust hybrid binding: rv32 path and WASM call-on-demand.
##
## Validates ADR-0111's two execution paths:
##   Stage 1: Rust cart_lua_modules + .lua_exports section on rv32
##   Stage 2: rv32emu_call_fn call-on-demand API (standalone harness)
##   Stage 3 ELF: Rust cart ELF for WASM trampoline (WASM module built on host)
##
## Build prerequisites:
##   fc32-spike-k-{arm64,amd64} and fc32-spike-i must exist.
##   fc32-spike-i is tagged as fc32-spike-i-arm64 or fc32-spike-i-amd64
##   by the Makefile docker-build target.
##
## Named build contexts (Makefile passes these automatically):
##   spike-i=../spike-i   (source tree: crt0.S, patches/)
##
## Pass --build-arg SPIKE_K_IMAGE=fc32-spike-k-amd64 for the amd64 variant.
## Pass --build-arg SPIKE_I_IMAGE=fc32-spike-i-amd64 for the amd64 variant.

ARG SPIKE_K_IMAGE=fc32-spike-k-arm64
ARG SPIKE_I_IMAGE=fc32-spike-i-arm64

# ── Pull spike-i runtime artefacts (libraries, luac, lua sources) ─────────────
# Force arm64 platform: spike-i only exists as arm64 and we only COPY files
# (RV32 ELFs + C sources), never execute spike-i host binaries in this stage.
FROM --platform=linux/arm64 ${SPIKE_I_IMAGE} AS spike-i-rt

# ── Builder: spike-k base (rv32emu source, cross-toolchain) + Rust ────────────
FROM ${SPIKE_K_IMAGE} AS builder

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    curl xxd \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /spike-q

# ── Copy spike-i artefacts ────────────────────────────────────────────────────

# Lua C API headers
COPY --from=spike-i-rt /spike-i/lib/include/      /spike-i/lib/include/

# RV32 console libraries
COPY --from=spike-i-rt /spike-i/lib/libconsole.so    /spike-i/lib/libconsole.so
COPY --from=spike-i-rt /spike-i/lib/libconsolelua.so /spike-i/lib/libconsolelua.so

# musl dynamic linker
COPY --from=spike-i-rt /lib/ld-musl-riscv32.so.1  /lib/ld-musl-riscv32.so.1

# Lua source tree — needed both for building luac and for the WASM build.
COPY --from=spike-i-rt /spike-i/lua/src/          /spike-i/lua/src/

# Build host-native 32-bit luac from source (platform-neutral: works on arm64+amd64).
# The spike-i runtime ships a binary luac but it's arm64-only.
RUN apt-get update && apt-get install -y --no-install-recommends gcc libc6-dev \
    && rm -rf /var/lib/apt/lists/* \
    && cd /spike-i/lua/src \
    && gcc -O2 -DLUA_32BITS \
        -o /usr/local/bin/luac \
        $(ls *.c | grep -v '^lua\.c$') \
        -lm \
    && /usr/local/bin/luac -v

# crt0.S from spike-i source context — compile into crt0.o
COPY --from=spike-i sdk/crt0.S /spike-i/sdk/crt0.S

RUN mkdir -p /spike-i/sdk/build && \
    riscv64-linux-gnu-gcc \
        -march=rv32imfc_zicsr -mabi=ilp32f \
        -ffreestanding -nostdlib -fno-stack-protector -fno-pie \
        -c /spike-i/sdk/crt0.S \
        -o /spike-i/sdk/build/crt0.o

# ── Apply multi-dynload + call_fn patches to rv32emu and rebuild ──────────────

COPY patches/ /spike-q/patches/

# Copy multi-dynload patch from spike-i source context
COPY --from=spike-i patches/apply-multi-dynload.py /spike-i/patches/apply-multi-dynload.py

# Enable CONFIG_EXT_A — Rust's core library for riscv32imafc-unknown-none-elf
# emits A-extension (atomic) instructions even in no_std pure-compute code.
RUN sed -i 's/# CONFIG_EXT_A is not set/CONFIG_EXT_A=y/' \
        /spike-a/rv32emu/.config && \
    cd /spike-a/rv32emu && \
    python3 /spike-i/patches/apply-multi-dynload.py && \
    python3 /spike-q/patches/apply_call_fn.py && \
    make OUT=build -j"$(nproc)"

# ── Rust toolchain ────────────────────────────────────────────────────────────

ENV CARGO_HOME=/root/.cargo
ENV RUSTUP_HOME=/root/.rustup
ENV PATH="/root/.cargo/bin:${PATH}"

RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | \
    sh -s -- -y --default-toolchain nightly-2025-08-01 --no-modify-path

RUN rustup target add riscv32imafc-unknown-none-elf || true
RUN rustup component add rust-src

# ── Copy spike-q source files ─────────────────────────────────────────────────

COPY cases/ /spike-q/cases/

# ── Stage 1: Build Rust staticlib (cart_lua_modules + .lua_exports) ───────────

RUN cd /spike-q/cases/rv32_binding/lib/rust && \
    cargo build \
        -Zbuild-std=core,panic_abort \
        --target riscv32imafc-unknown-none-elf \
        --release

RUN test -f /spike-q/cases/rv32_binding/lib/rust/target/riscv32imafc-unknown-none-elf/release/libcart_lua_modules.a \
    || (echo "FAIL: Rust staticlib not found"; exit 1)

# ── Stage 1: Compile Lua bytecode ─────────────────────────────────────────────

RUN mkdir -p /spike-q/cases/rv32_binding/build
RUN luac -o /spike-q/cases/rv32_binding/build/cart.luac \
         /spike-q/cases/rv32_binding/cart.lua
RUN xxd -i /spike-q/cases/rv32_binding/build/cart.luac \
    | sed 's/^unsigned.*//' | sed '/^$/d' \
    | sed 's/.*{//' | sed 's/}.*$//' \
    > /spike-q/cases/rv32_binding/build/cart_lua_bytes.inc

# Copy cart.c template (defines _cart_lua_bytecode[] with #include directive).
# Not generated via printf to avoid Dockerfile treating '#include' as a comment.
RUN cp /spike-q/cases/rv32_binding/cart.c.template \
       /spike-q/cases/rv32_binding/build/cart.c

RUN riscv64-linux-gnu-gcc \
    -march=rv32imfc_zicsr -mabi=ilp32f -O2 \
    -ffreestanding -nostdlib -fno-stack-protector -fno-pie \
    -I/spike-q/cases/rv32_binding/build \
    -c /spike-q/cases/rv32_binding/build/cart.c \
    -o /spike-q/cases/rv32_binding/build/cart.o

# ── Stage 1: Link cart_hybrid.elf ─────────────────────────────────────────────

RUN mkdir -p /spike-q/elfs

RUN riscv64-linux-gnu-gcc \
    -march=rv32imfc_zicsr -mabi=ilp32f \
    -nostdlib -no-pie \
    -Wl,--build-id=none \
    -Wl,--export-dynamic \
    -Wl,--gc-sections \
    -Wl,--dynamic-linker,/lib/ld-musl-riscv32.so.1 \
    -Wl,-rpath,/spike-i/lib \
    -Wl,-rpath-link,/spike-i/lib \
    -T /spike-q/cases/rv32_binding/cart.ld \
    /spike-i/sdk/build/crt0.o \
    /spike-q/cases/rv32_binding/build/cart.o \
    -Wl,--whole-archive \
    /spike-q/cases/rv32_binding/lib/rust/target/riscv32imafc-unknown-none-elf/release/libcart_lua_modules.a \
    -Wl,--no-whole-archive \
    -Wl,-u,cart_lua_modules \
    -L/spike-i/lib -lconsole -Wl,--no-as-needed -lconsolelua -Wl,--as-needed \
    -o /spike-q/elfs/cart_hybrid.elf

RUN riscv64-linux-gnu-readelf -h /spike-q/elfs/cart_hybrid.elf \
    | grep -E "(Class|Machine|Type):"

# Stage 1 gate: .lua_exports section present with "mylib"
RUN riscv64-linux-gnu-objdump -s -j .lua_exports /spike-q/elfs/cart_hybrid.elf \
    > /spike-q/elfs/lua_exports_dump.txt && \
    cat /spike-q/elfs/lua_exports_dump.txt && \
    grep -q "mylib" /spike-q/elfs/lua_exports_dump.txt \
    || (echo "FAIL: .lua_exports absent or missing 'mylib'"; exit 1)

# Stage 1 gate: cart_lua_modules in dynamic symbol table
RUN riscv64-linux-gnu-nm -D /spike-q/elfs/cart_hybrid.elf \
    | grep -q "cart_lua_modules" \
    || (echo "FAIL: cart_lua_modules not in dynamic symtab"; exit 1)

# ── Stage 2: Build test_funcs.elf (RV32 target) ───────────────────────────────

RUN riscv64-linux-gnu-gcc \
    -march=rv32imfc_zicsr -mabi=ilp32f -O2 \
    -ffreestanding -nostdlib -no-pie \
    -Wl,--build-id=none \
    -Wl,-e,_start \
    /spike-q/cases/wasm_trampoline/harness/test_funcs.c \
    -o /spike-q/elfs/test_funcs.elf

RUN riscv64-linux-gnu-nm /spike-q/elfs/test_funcs.elf \
    | grep -E "(add32|addf)" \
    || (echo "FAIL: add32/addf not in test_funcs.elf"; exit 1)

# ── Stage 2: Build test_call_on_demand harness (host binary) ──────────────────

RUN cd /spike-a/rv32emu && \
    ar rcs build/librv32emu.a \
    $(ls build/*.o | grep -v '/main\.o$' | sort)

# Add softfloat objects to librv32emu.a: only the RISCV-specialize objects and
# the primitive f32/f64/i32 operations (excluding 80-bit extF80 which rv32emu
# doesn't use and which would pull in missing specialize symbols).
RUN ar rcs /spike-a/rv32emu/build/librv32emu.a \
    $(find /spike-a/rv32emu/build/softfloat -name '*.o' \
           ! -name 'extF80*' ! -name 'f128*' ! -name '*128*' | sort) || true

RUN gcc -O2 \
    -include /spike-a/rv32emu/src/common.h \
    -I/spike-a/rv32emu/src \
    /spike-q/cases/wasm_trampoline/harness/test_call_on_demand.c \
    -L/spike-a/rv32emu/build -lrv32emu \
    -lm \
    -o /spike-q/elfs/test_call_on_demand

# ── Stage 3: Build rust_cart.elf (RV32 static binary for WASM trampoline) ─────

RUN cd /spike-q/cases/wasm_trampoline/rust_cart && \
    cargo build \
        -Zbuild-std=core,panic_abort \
        --target riscv32imafc-unknown-none-elf \
        --release

RUN printf 'OUTPUT_ARCH(riscv)\nENTRY(_start)\nSECTIONS {\n    . = 0x00010000;\n    .text : { *(.text._start) *(.text .text.*) }\n    .rodata : { *(.rodata .rodata.*) }\n    .sdata : {\n        PROVIDE(__global_pointer$ = . + 0x800);\n        *(.sdata .sdata.*)\n    }\n    .data : { *(.data .data.*) }\n    .bss : { *(.bss .bss.*) *(COMMON) }\n    .lua_exports : { KEEP(*(.lua_exports)) }\n    /DISCARD/ : { *(.note*) *(.comment) *(.eh_frame) }\n}\n' \
    > /spike-q/elfs/rust_cart.ld

RUN riscv64-linux-gnu-gcc \
    -march=rv32imfc_zicsr -mabi=ilp32f \
    -nostdlib -no-pie \
    -Wl,--build-id=none \
    -Wl,--gc-sections \
    -T /spike-q/elfs/rust_cart.ld \
    -Wl,-u,fast_add -Wl,-u,fast_mul \
    -Wl,--whole-archive \
    /spike-q/cases/wasm_trampoline/rust_cart/target/riscv32imafc-unknown-none-elf/release/librust_cart.a \
    -Wl,--no-whole-archive \
    -o /spike-q/elfs/rust_cart.elf

# Stage 3 gate: .lua_exports with "mylib"
RUN riscv64-linux-gnu-objdump -s -j .lua_exports /spike-q/elfs/rust_cart.elf \
    > /spike-q/elfs/rust_cart_exports_dump.txt && \
    cat /spike-q/elfs/rust_cart_exports_dump.txt && \
    grep -q "mylib" /spike-q/elfs/rust_cart_exports_dump.txt \
    || (echo "FAIL: .lua_exports missing in rust_cart.elf"; exit 1)

# Stage 3 gate: fast_add and fast_mul present
RUN riscv64-linux-gnu-nm /spike-q/elfs/rust_cart.elf \
    | grep -E "(fast_add|fast_mul)" \
    || (echo "FAIL: fast_add/fast_mul absent in rust_cart.elf"; exit 1)

# ── ELF checksums ─────────────────────────────────────────────────────────────

RUN sha256sum /spike-q/elfs/*.elf | sort > /spike-q/elfs/elfs.builder.sha256

# ── Runtime image ──────────────────────────────────────────────────────────────

FROM ${SPIKE_K_IMAGE}

WORKDIR /spike-q

RUN mkdir -p /spike-q/elfs /spike-i/lib

COPY --from=builder /spike-q/elfs/                            /spike-q/elfs/
COPY --from=builder /spike-a/rv32emu/build/rv32emu            /spike-a/rv32emu/build/rv32emu
COPY --from=builder /spike-i/lib/libconsole.so                /spike-i/lib/libconsole.so
COPY --from=builder /spike-i/lib/libconsolelua.so             /spike-i/lib/libconsolelua.so
COPY --from=builder /lib/ld-musl-riscv32.so.1                 /lib/ld-musl-riscv32.so.1
# Lua source tree — used by Makefile Stage 3 WASM build (extracted from this image)
COPY --from=builder /spike-i/lua/src/                         /spike-i/lua/src/
