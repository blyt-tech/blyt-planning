## Spike P — extended stub library (libconsole_spike_p.so).

CROSS   ?= riscv64-linux-gnu-gcc
ARCH    := -march=rv32imfc_zicsr -mabi=ilp32f
OUTDIR  ?= build
LIB_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

CFLAGS := $(ARCH) -O2 -fPIC -ffreestanding -nostdlib -fno-stack-protector \
           -fno-common

.PHONY: all clean

all: $(OUTDIR)/libconsole_spike_p.so

$(OUTDIR):
	mkdir -p $@

$(OUTDIR)/console_spike_p.o: $(LIB_DIR)/console_spike_p.c | $(OUTDIR)
	$(CROSS) $(CFLAGS) -c -o $@ $<

$(OUTDIR)/libconsole_spike_p.so: $(OUTDIR)/console_spike_p.o
	$(CROSS) -shared -fPIC -nostdlib \
	    -march=rv32imfc_zicsr -mabi=ilp32f \
	    -Wl,-soname,libconsole_spike_p.so \
	    -Wl,--unresolved-symbols=ignore-all \
	    -o $@ $^

clean:
	$(RM) -r $(OUTDIR)
