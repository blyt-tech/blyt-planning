## Spike H build environment.
##
## Cross-compiles cart-side artefacts (RV32 musl ELFs) for use inside a QEMU
## RV64 Linux guest.  This image does NOT itself host a QEMU full-system run —
## that's a separate `make qemu-run` step on the host (Apple Silicon hosts
## use Homebrew's qemu-system-riscv64; CI hosts use distro packages).
##
## Platform: linux/arm64.  The riscv32-linux-musl prebuilt cross-toolchain
## is hosted at musl.cc and supports both x86_64 and aarch64 build hosts.
##
## What lives here:
##   /opt/riscv32                  riscv32-linux-musl-cross prebuilt toolchain
##   /opt/lua                      Lua 5.4.7 source (downloaded; 32-bit numbers on)
##   /usr/bin/riscv64-linux-gnu-*  cross-tools for an RV64 native build path
##                                 (used by the launcher; optional in this stage)
##   /usr/bin/qemu-system-riscv64  full-system QEMU (for in-container smoke tests
##                                 of small images; the main qemu-run is on host)

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    bc \
    ca-certificates \
    curl \
    file \
    git \
    libseccomp-dev \
    qemu-system-misc \
    qemu-user-static \
    qemu-utils \
    python3 \
    wget \
    xxd \
    xz-utils \
    && rm -rf /var/lib/apt/lists/*

# ── riscv64-linux-gnu cross-toolchain + libseccomp for cross-building launcher
# The launcher is a native RV64 Linux process (not a cart); it must run inside
# the QEMU guest as an RV64 binary.  We cross-compile it here so the QEMU guest
# doesn't need a build environment (avoiding slow apt-get over SLIRP networking).
RUN dpkg --add-architecture riscv64 && \
    apt-get update && \
    apt-get install -y --no-install-recommends \
        gcc-riscv64-linux-gnu \
        libc6-dev:riscv64 \
        libseccomp-dev:riscv64 \
    && rm -rf /var/lib/apt/lists/*

# ── riscv32-linux-musl prebuilt toolchain ──────────────────────────────────
# musl.cc ships separate aarch64 and x86_64 build-host packages.  We pick
# based on the build platform.  Pinning to a specific release URL would be
# safer; for the spike we accept "latest" and record the version used
# in the toolchain-version file below.
ARG MUSLCC_BASE=https://musl.cc
RUN set -eu; \
    arch=$(dpkg --print-architecture); \
    case "$arch" in \
        amd64) host=x86_64-linux-musl ;; \
        arm64) host=aarch64-linux-musl ;; \
        *) echo "unsupported build arch: $arch"; exit 1 ;; \
    esac; \
    cd /opt; \
    curl -L -o riscv32-cross.tgz \
        $MUSLCC_BASE/riscv32-linux-musl-cross.tgz; \
    tar xf riscv32-cross.tgz; \
    rm riscv32-cross.tgz; \
    mv riscv32-linux-musl-cross riscv32; \
    /opt/riscv32/bin/riscv32-linux-musl-gcc --version | head -1 \
        > /opt/riscv32/version.txt
ENV PATH=/opt/riscv32/bin:$PATH

# ── Lua 5.4 source tree ─────────────────────────────────────────────────────
# Mirrors the spike-b/Dockerfile flow: download, drop into /opt/lua, switch
# LUA_32BITS on so the build matches the Pi-class cart configuration.
ARG LUA_VERSION=5.4.7
RUN cd /opt && \
    wget -q https://www.lua.org/ftp/lua-${LUA_VERSION}.tar.gz && \
    tar xf lua-${LUA_VERSION}.tar.gz && \
    mv lua-${LUA_VERSION} lua && \
    rm lua-${LUA_VERSION}.tar.gz && \
    sed -i 's|^#define LUA_32BITS\([[:space:]]\+\)0$|#define LUA_32BITS\11|' \
        /opt/lua/src/luaconf.h && \
    grep '^#define LUA_32BITS' /opt/lua/src/luaconf.h

# ── sanity: the toolchain produces an RV32 ELF ──────────────────────────────
RUN echo 'int main(void){return 0;}' > /tmp/x.c && \
    riscv32-linux-musl-gcc -static -o /tmp/x /tmp/x.c && \
    riscv32-linux-musl-readelf -h /tmp/x | grep -E '^  (Class|Machine|Flags)' && \
    rm /tmp/x.c /tmp/x

WORKDIR /spike-h
