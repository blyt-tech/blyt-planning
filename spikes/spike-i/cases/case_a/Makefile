CROSS   ?= riscv64-linux-gnu-gcc
ARCH    := -march=rv32imfc_zicsr -mabi=ilp32f
OUTDIR  ?= build

## PT_INTERP — path to the RV32 dynamic linker the kernel exec()s on the
## native target. rv32emu ignores PT_INTERP entirely (the multi-library
## fc32_dynload runs in the emulator instead), so the same binary loads on
## both targets when this points at musl's standard install path. Override
## with `INTERP=/lib/ld-linux-riscv32-ilp32f.so.1` for a glibc-targeted run.
INTERP  ?= /lib/ld-musl-riscv32.so.1

SDK_DIR   := /spike-i/sdk/build
LIB_DIR   := /spike-i/lib
CASE_DIR  := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

CFLAGS := $(ARCH) -O2 -ffreestanding -nostdlib -fno-stack-protector -fno-pie \
           -I/spike-i/lib/include

.PHONY: all clean

all: $(OUTDIR)/cart_a

$(OUTDIR):
	mkdir -p $@

$(OUTDIR)/cart_a.o: $(CASE_DIR)/cart_a.c | $(OUTDIR)
	$(CROSS) $(CFLAGS) -c -o $@ $<

$(OUTDIR)/cart_a: $(SDK_DIR)/crt0.o $(OUTDIR)/cart_a.o
	$(CROSS) $(ARCH) -nostdlib -no-pie \
	    -Wl,--build-id=none \
	    -Wl,--export-dynamic \
	    -Wl,--dynamic-linker,$(INTERP) \
	    -Wl,-rpath-link,$(LIB_DIR) \
	    -T $(CASE_DIR)/../cart.ld \
	    -o $@ $^ \
	    -L$(LIB_DIR) -lconsole

clean:
	$(RM) -r $(OUTDIR)
