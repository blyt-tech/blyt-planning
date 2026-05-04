CROSS   ?= riscv64-linux-gnu-gcc
ARCH    := -march=rv32imfc_zicsr -mabi=ilp32f
OUTDIR  ?= build
INTERP  ?= /lib/ld-musl-riscv32.so.1

SDK_DIR   := /spike-i/sdk/build
LIB_DIR   := /spike-i/lib
CASE_DIR  := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

LUA_SRC   := /spike-i/lua/src

CFLAGS := $(ARCH) -O2 -ffreestanding -nostdlib -fno-stack-protector -fno-pie \
           -ffunction-sections \
           -I/spike-i/lib/include \
           -I$(LUA_SRC) \
           -I$(OUTDIR)

.PHONY: all clean

all: $(OUTDIR)/cart_d

$(OUTDIR):
	mkdir -p $@

## Use the custom luac built from the patched 32-bit Lua source so bytecode
## matches libconsolelua's lua_Integer/lua_Number sizes.
$(OUTDIR)/cart.luac: $(CASE_DIR)/main.lua | $(OUTDIR)
	luac -o $@ $<

$(OUTDIR)/cart_lua_bytes.inc: $(OUTDIR)/cart.luac | $(OUTDIR)
	xxd -i $< | sed 's/^unsigned.*//' | sed '/^$$/d' \
	    | sed 's/.*{//' | sed 's/}.*$$//' > $@

$(OUTDIR)/cart_d.o: $(CASE_DIR)/cart_d.c $(OUTDIR)/cart_lua_bytes.inc | $(OUTDIR)
	$(CROSS) $(CFLAGS) -c -o $@ $<

$(OUTDIR)/mylib.o: $(CASE_DIR)/mylib.c | $(OUTDIR)
	$(CROSS) $(CFLAGS) -c -o $@ $<

$(OUTDIR)/cart_lua_modules.o: $(CASE_DIR)/cart_lua_modules.c | $(OUTDIR)
	$(CROSS) $(CFLAGS) -c -o $@ $<

$(OUTDIR)/cart_d: $(SDK_DIR)/crt0.o $(OUTDIR)/cart_d.o \
                  $(OUTDIR)/mylib.o $(OUTDIR)/cart_lua_modules.o
	$(CROSS) $(ARCH) -nostdlib -no-pie \
	    -Wl,--build-id=none \
	    -Wl,--export-dynamic \
	    -Wl,--gc-sections \
	    -Wl,--dynamic-linker,$(INTERP) \
	    -Wl,-rpath-link,$(LIB_DIR) \
	    -T $(CASE_DIR)/../cart.ld \
	    -o $@ $^ \
	    -L$(LIB_DIR) -lconsole -Wl,--no-as-needed -lconsolelua -Wl,--as-needed

clean:
	$(RM) -r $(OUTDIR)
