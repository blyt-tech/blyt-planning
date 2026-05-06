## Spike L — libretro core for the FC32 runtime.
## Inherits Spike I's image (rv32emu + libconsole.so + libconsolelua.so +
## the four cart cases) and adds RetroArch + libretro headers + Python
## tooling for the headless save-state / drift / rewind-capacity gates.

ARG SPIKE_I_IMAGE=fc32-spike-i
FROM ${SPIKE_I_IMAGE} AS builder

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        libretro-dev \
        retroarch \
        retroarch-assets \
        python3 \
        python3-numpy \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /spike-l

COPY host/   /spike-l/host/
COPY lib/    /spike-l/lib/
COPY cases/  /spike-l/cases/
COPY Makefile /spike-l/Makefile

## Build the libretro core. Use system libretro.h (libretro-dev) when
## available; falls back to vendored copy.
RUN cd /spike-l && \
    cc -O2 -Wall -Wextra -Wno-unused-parameter -fPIC \
        -Ihost -Ilib \
        -o runtime_libretro.so \
        host/retro_core.c host/palette.c host/input_map.c host/audio_push.c \
        lib/blyt_facade.c \
        -shared -lm

RUN file /spike-l/runtime_libretro.so

## Sanity check: dump the libretro entry points.
RUN nm -D /spike-l/runtime_libretro.so \
    | grep -E "retro_(init|run|load_game|serialize|unserialize)" \
    | sort
