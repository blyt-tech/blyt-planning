## Spike J — case_c_dbg builds three .luac variants for DAP test harness.
##
## The DAP test path uses dap_lua_host.c (host-side Lua-direct) which loads
## .luac directly — no cart ELF needed. The cart ELF binary (cart_c_dbg) is
## only required if running through rv32emu, which the slim spike-i image
## does not have the SDK / crt0.o for. Building the cart ELF here would
## require either an SDK rebuild or basing the spike-j Dockerfile on
## spike-i's *builder* stage (a substantially larger image). The spike's
## DAP composition validation does not need the cart ELF.

OUTDIR  ?= build
CASE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

.PHONY: all clean

all: $(OUTDIR)/cart.luac \
     $(OUTDIR)/cart_shifted.luac \
     $(OUTDIR)/cart_deleted.luac

$(OUTDIR):
	mkdir -p $@

$(OUTDIR)/cart.luac: $(CASE_DIR)/main.lua | $(OUTDIR)
	luac -o $@ $<

$(OUTDIR)/cart_shifted.luac: $(CASE_DIR)/main_shifted.lua | $(OUTDIR)
	luac -o $@ $<

$(OUTDIR)/cart_deleted.luac: $(CASE_DIR)/main_deleted.lua | $(OUTDIR)
	luac -o $@ $<

clean:
	$(RM) -r $(OUTDIR)
