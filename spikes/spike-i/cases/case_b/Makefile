CROSS   ?= riscv64-linux-gnu-gcc
ARCH    := -march=rv32imfc_zicsr -mabi=ilp32f
OUTDIR  ?= build
INTERP  ?= /lib/ld-musl-riscv32.so.1

SDK_DIR   := /spike-i/sdk/build
LIB_DIR   := /spike-i/lib
CASE_DIR  := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

CFLAGS := $(ARCH) -O2 -ffreestanding -nostdlib -fno-stack-protector -fno-pie \
           -ffunction-sections \
           -I/spike-i/lib/include

.PHONY: all clean

all: $(OUTDIR)/cart_b

$(OUTDIR):
	mkdir -p $@

$(OUTDIR)/cart_b.o: $(CASE_DIR)/cart_b.c | $(OUTDIR)
	$(CROSS) $(CFLAGS) -c -o $@ $<

$(OUTDIR)/mylib.o: $(CASE_DIR)/mylib.c | $(OUTDIR)
	$(CROSS) $(CFLAGS) -c -o $@ $<

$(OUTDIR)/cart_b: $(SDK_DIR)/crt0.o $(OUTDIR)/cart_b.o $(OUTDIR)/mylib.o
	$(CROSS) $(ARCH) -nostdlib -no-pie \
	    -Wl,--build-id=none \
	    -Wl,--export-dynamic \
	    -Wl,--gc-sections \
	    -Wl,--dynamic-linker,$(INTERP) \
	    -Wl,-rpath-link,$(LIB_DIR) \
	    -T $(CASE_DIR)/../cart.ld \
	    -o $@ $^ \
	    -L$(LIB_DIR) -lconsole

clean:
	$(RM) -r $(OUTDIR)
