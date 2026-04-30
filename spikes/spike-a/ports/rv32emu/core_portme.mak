# CoreMark port for rv32emu
# Uses riscv64-linux-gnu-gcc targeting RV32IMFC, with -nostdlib.
# Run from the coremark/ directory: make PORT_DIR=../ports/rv32emu

CROSS   ?= riscv64-linux-gnu-
CC       = $(CROSS)gcc

MARCH   ?= rv32imfc_zicsr
MABI    ?= ilp32f

PORT_CFLAGS  = -march=$(MARCH) -mabi=$(MABI) -O2
PORT_CFLAGS += -ffreestanding -nostdlib -fno-stack-protector -fno-common
# Static + no-PIE: rv32emu loads ELFs raw, with no dynamic linker — anything
# requiring /lib/ld-linux-riscv32-ilp32f.so.1 silently fails to start.
PORT_CFLAGS += -static -no-pie -fno-pie

# Berkeley SoftFloat provides the IEEE 754 double-precision helpers that the
# Ubuntu rv64 libgcc.a doesn't have an rv32 counterpart for. softfloat-glue.c
# wraps the f64_* API as the libgcc names GCC emits (__subdf3, __ltdf2 etc).
SOFTFLOAT_DIR ?= /spike-a/softfloat-build
SOFTFLOAT_INC ?= /spike-a/berkeley-softfloat-3/source/include

FLAGS_STR    = "$(PORT_CFLAGS) $(XCFLAGS)"

OUTFLAG  = -o
CFLAGS   = $(PORT_CFLAGS) -I$(PORT_DIR) -I$(SOFTFLOAT_INC) -I. \
           -DFLAGS_STR=\"$(FLAGS_STR)\"

# syscalls.c, core_portme.c, softfloat-glue.c compiled as part of SRCS.
# crt0.S goes last on the link line via LFLAGS_END (after softfloat.a so the
# archive can resolve symbols GCC emitted from the .c files above).
PORT_SRCS    = $(PORT_DIR)/syscalls.c $(PORT_DIR)/core_portme.c \
               $(PORT_DIR)/softfloat-glue.c
LFLAGS_END   = $(SOFTFLOAT_DIR)/softfloat.a $(PORT_DIR)/crt0.S \
               -Wl,--build-id=none
EXTRA_DEPENDS = $(PORT_DIR)/core_portme.mak

EXE  = .elf
OEXT = .o
OPATH = ./
MKDIR = mkdir -p

LOAD = echo "Loading done"
# CoreMark's run rule invokes "$(RUN) $(OUTFILE) <args>"; rv32emu takes the
# ELF path as its first positional, so pointing RUN at the emulator binary
# slots in cleanly.
RV32EMU ?= /spike-a/rv32emu/build/rv32emu
RUN  = $(RV32EMU)

.PHONY: port_prebuild port_postbuild port_prerun port_postrun port_preload port_postload
port_prebuild port_postbuild port_prerun port_postrun port_preload port_postload:

PERL=/usr/bin/perl
