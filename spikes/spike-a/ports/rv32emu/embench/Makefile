## Embench-IoT port for rv32emu — builds all benchmarks as RV32IMFC ELFs.
##
## Usage (inside the Docker container / after setting up the cross-toolchain):
##   make -C ports/rv32emu/embench OUTDIR=<dir>
##
## The Docker build runs this; the top-level Makefile provides convenience
## targets (docker-embench, docker-embench-shell).
##
## Each ELF self-reports its timing when run under rv32emu:
##   Embench : <benchmark>          : <ms>.<xxx> ms
##
## qrduino is excluded: it requires <avr/pgmspace.h> (AVR MCU specific).

CROSS   ?= riscv64-linux-gnu-gcc
ARCH    := -march=rv32imfc_zicsr -mabi=ilp32f
OUTDIR  ?= build

# Paths relative to this Makefile's directory (ports/rv32emu/embench/)
PORT_DIR     := ..
SPIKE_DIR    := ../../..
EMBENCH      := $(SPIKE_DIR)/embench-iot
SOFTFLOAT_A   := $(SPIKE_DIR)/softfloat-build/softfloat.a
SOFTFLOAT_INC := $(SPIKE_DIR)/berkeley-softfloat-3/source/include

CFLAGS  := $(ARCH) -O2 -g
CFLAGS  += -ffreestanding -nostdlib -fno-stack-protector -fno-common
CFLAGS  += -static -no-pie -fno-pie
CFLAGS  += -DNDEBUG -DHAVE_BOARDSUPPORT_H
# GLOBAL_SCALE_FACTOR is normally set by the Embench SCons build (default 1).
# LOCAL_SCALE_FACTOR is defined per-benchmark in each benchmark's source file.
CFLAGS  += -DGLOBAL_SCALE_FACTOR=1

# Include paths: port dir (boardsupport.h, config.h, ctype.h override) first,
# then embench support, then SoftFloat headers.
BASE_INCLUDES := -I. -I$(EMBENCH)/support -I$(SOFTFLOAT_INC)

# Common source files compiled into every benchmark ELF.
# NOTE: boardsupport.c is NOT listed here — support/board.c does
#   #include "boardsupport.c"  (textual inclusion), so compiling it separately
#   would produce duplicate symbol errors.  The -I. flag ensures board.c finds
#   our boardsupport.c when it does the #include.
#
# softfloat-glue.c provides __fixdfsi/__floatsidf etc. needed by sqrt() in
# libsys.c (wikisort calls sqrt(long)).  softfloat.a is linked at the end to
# resolve the f64_* symbols the glue calls.
COMMON_SRCS := \
    $(PORT_DIR)/crt0.S \
    $(PORT_DIR)/syscalls.c \
    $(PORT_DIR)/softfloat-glue.c \
    libsys.c

# Embench support files (compiled once per benchmark binary)
SUPPORT_SRCS := \
    $(EMBENCH)/support/beebsc.c \
    $(EMBENCH)/support/main.c \
    $(EMBENCH)/support/board.c \
    $(EMBENCH)/support/chip.c

BENCHMARKS := \
    aha-mont64 \
    crc32 \
    depthconv \
    edn \
    huffbench \
    matmult-int \
    md5sum \
    nettle-aes \
    nettle-sha256 \
    nsichneu \
    picojpeg \
    sglib-combined \
    slre \
    statemate \
    tarfind \
    ud \
    wikisort \
    xgboost

# qrduino requires <avr/pgmspace.h> — not available for RISC-V, so excluded.

ELFS := $(patsubst %, $(OUTDIR)/embench-%.elf, $(BENCHMARKS))

.PHONY: all clean

all: $(OUTDIR) $(ELFS)

$(OUTDIR):
	mkdir -p $@

# Per-benchmark rule: compile all .c files in src/<name>/ together with the
# common and support files into a single ELF.  BENCHMARK_NAME is passed as a
# define so boardsupport.c can label its output line.
define BENCH_template
BENCH_SRCS_$(1) := $$(wildcard $(EMBENCH)/src/$(1)/*.c)

$(OUTDIR)/embench-$(1).elf: $$(BENCH_SRCS_$(1)) $(COMMON_SRCS) $(SUPPORT_SRCS) Makefile
	$(CROSS) $(CFLAGS) \
	    $(BASE_INCLUDES) -I$(EMBENCH)/src/$(1) \
	    -DBENCHMARK_NAME=\"$(1)\" \
	    -o $$@ \
	    $$(BENCH_SRCS_$(1)) $(COMMON_SRCS) $(SUPPORT_SRCS) \
	    $(SOFTFLOAT_A) \
	    -Wl,--build-id=none
endef

$(foreach b,$(BENCHMARKS),$(eval $(call BENCH_template,$(b))))

clean:
	$(RM) -r $(OUTDIR)
