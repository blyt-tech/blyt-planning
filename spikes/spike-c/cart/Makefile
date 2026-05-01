## Build spike_c_cart.elf — dynamically linked cart that calls into liblua54.so

CROSS   ?= riscv64-linux-gnu-gcc
ARCH    := -march=rv32imfc_zicsr -mabi=ilp32f
OUTDIR  ?= build

LUA_SRC  := /spike-c/lua/src
LIB_DIR  := /spike-c/lib

CART_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

CFLAGS := $(ARCH) -O2 -ffreestanding -nostdlib -fno-stack-protector -fno-pie \
           -I/spike-c/lib/include \
           -I$(LUA_SRC) \
           -I$(CART_DIR)

.PHONY: all clean

all: $(OUTDIR)/spike_c_cart.elf

$(OUTDIR):
	mkdir -p $@

$(OUTDIR)/crt0.o: $(CART_DIR)/crt0.S | $(OUTDIR)
	$(CROSS) $(ARCH) -ffreestanding -nostdlib -c -o $@ $<

$(OUTDIR)/cart_runtime.o: $(CART_DIR)/cart_runtime.c | $(OUTDIR)
	$(CROSS) $(CFLAGS) -c -o $@ $<

$(OUTDIR)/spike_c_cart.o: $(CART_DIR)/spike_c_cart.c | $(OUTDIR)
	$(CROSS) $(CFLAGS) -c -o $@ $<

$(OUTDIR)/spike_c_cart.elf: \
		$(OUTDIR)/crt0.o \
		$(OUTDIR)/cart_runtime.o \
		$(OUTDIR)/spike_c_cart.o
	$(CROSS) $(ARCH) -nostdlib \
	    -Wl,--build-id=none \
	    -Wl,-rpath-link,$(LIB_DIR) \
	    -Wl,--dynamic-linker,/no/interp \
	    -L$(LIB_DIR) -llua54 \
	    -o $@ \
	    $(OUTDIR)/crt0.o \
	    $(OUTDIR)/cart_runtime.o \
	    $(OUTDIR)/spike_c_cart.o

clean:
	$(RM) -r $(OUTDIR)
