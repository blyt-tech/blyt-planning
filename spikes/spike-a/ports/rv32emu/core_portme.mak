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
FLAGS_STR    = "$(PORT_CFLAGS) $(XCFLAGS)"

OUTFLAG  = -o
CFLAGS   = $(PORT_CFLAGS) -I$(PORT_DIR) -I. -DFLAGS_STR=\"$(FLAGS_STR)\"

# syscalls.c is compiled as part of SRCS (via PORT_SRCS).
# crt0.S is passed at link time via LFLAGS_END so it lands last on the
# command line, after all .c sources, which is what GCC expects.
# No -lgcc: the Ubuntu riscv64-linux-gnu toolchain's libgcc.a is rv64
# (ELFCLASS64), incompatible with our rv32 link. With HAS_FLOAT=0 and our
# own memset/memcpy in syscalls.c, no libgcc helpers are needed.
PORT_SRCS    = $(PORT_DIR)/syscalls.c $(PORT_DIR)/core_portme.c
LFLAGS_END   = $(PORT_DIR)/crt0.S -Wl,--build-id=none
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
