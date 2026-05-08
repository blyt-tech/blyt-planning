## Spike O — extended stub library (libconsole_spike_o.so).
##
## Provides stub implementations of the three API families used by the toy
## Rust cart: image load/blit, audio sfx play/set_volume, state buffer get/set.
## Also provides the ABI witness (SET_VOL line with raw IEEE 754 bits).
##
## All original libconsole.so functions (fc_console_main, fc_console_print,
## fc_console_printf, _exit, etc.) are included by also building libconsole_rv32.c
## which is copied into this directory from spike-i by the Dockerfile.

CROSS   ?= riscv64-linux-gnu-gcc
ARCH    := -march=rv32imfc_zicsr -mabi=ilp32f
OUTDIR  ?= build
LIB_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

CFLAGS := $(ARCH) -O2 -fPIC -ffreestanding -nostdlib -fno-stack-protector \
           -fno-common

.PHONY: all clean

all: $(OUTDIR)/libconsole_spike_o.so

$(OUTDIR):
	mkdir -p $@

$(OUTDIR)/libconsole_stub_ext.o: $(LIB_DIR)/libconsole_stub_ext.c | $(OUTDIR)
	$(CROSS) $(CFLAGS) -c -o $@ $<

$(OUTDIR)/libconsole_spike_o.so: $(OUTDIR)/libconsole_stub_ext.o
	$(CROSS) -shared -fPIC -nostdlib \
	    -march=rv32imfc_zicsr -mabi=ilp32f \
	    -Wl,-soname,libconsole_spike_o.so \
	    -Wl,--unresolved-symbols=ignore-all \
	    -o $@ $^

clean:
	$(RM) -r $(OUTDIR)
