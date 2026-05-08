## Spike O SDK — builds crt0.o from the spike-i crt0.S (copied into this dir
## by the Dockerfile via COPY --from=spike-i).

CROSS   ?= riscv64-linux-gnu-gcc
ARCH    := -march=rv32imfc_zicsr -mabi=ilp32f
OUTDIR  ?= build
SDK_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

.PHONY: all clean

all: $(OUTDIR)/crt0.o

$(OUTDIR):
	mkdir -p $@

$(OUTDIR)/crt0.o: $(SDK_DIR)/crt0.S | $(OUTDIR)
	$(CROSS) $(ARCH) -ffreestanding -nostdlib -c -o $@ $<

clean:
	$(RM) -r $(OUTDIR)
