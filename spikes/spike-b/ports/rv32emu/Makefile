## Build Lua 5.4 for RV32IMFC + per-benchmark cart ELFs.
##
## Invoked from the Dockerfile after Lua source has been fetched into
## /spike-b/lua/.  Outputs go to ./build/, with the per-benchmark ELFs
## named build/lua_cart_<bench>.elf.
##
## Layout assumed (relative to this Makefile):
##   ../../lua/src/         Lua 5.4 source tree (lapi.c, lvm.c, lauxlib.c, ...)
##   ../../benchmarks/      *.lua scripts (one per benchmark)
##   ../../softfloat-glue   pulled from spike-a image at /spike-a/...
##
## Spike-a artefacts live at /spike-a/ inside the Docker image we build on
## top of, so we reference them by absolute path:
##   SPIKE_A_DIR = /spike-a
##
## Override SPIKE_A_DIR if building outside Docker.

CROSS    ?= riscv64-linux-gnu-gcc
ARCH     := -march=rv32imfc_zicsr -mabi=ilp32f
OUTDIR   ?= build

SPIKE_A_DIR    ?= /spike-a
SOFTFLOAT_A    ?= $(SPIKE_A_DIR)/softfloat-build/softfloat.a
SOFTFLOAT_INC  ?= $(SPIKE_A_DIR)/berkeley-softfloat-3/source/include

PORT_DIR := .
LUA_DIR  := ../../lua
LUA_SRC  := $(LUA_DIR)/src
BENCH    := ../../benchmarks

# ── Lua source selection ─────────────────────────────────────────────────────
# Skip stdlib units we cannot serve (no FS, no dynamic loading) and use our
# own linit replacement (lua_init_libs.c).
LUA_OMIT := lua.c luac.c linit.c \
            liolib.c loslib.c loadlib.c \
            lcorolib.c ldblib.c lutf8lib.c
LUA_ALL_SRCS := $(notdir $(wildcard $(LUA_SRC)/*.c))
LUA_SRCS     := $(filter-out $(LUA_OMIT), $(LUA_ALL_SRCS))
LUA_OBJS     := $(patsubst %.c, $(OUTDIR)/lua/%.o, $(LUA_SRCS))

# ── flags ────────────────────────────────────────────────────────────────────
# -I include/ first so our libc stand-ins (stdio.h, stdlib.h, math.h, etc.)
# win over the cross-toolchain sysroot ones.  We DO want GCC's own stdint.h,
# stddef.h, stdarg.h, float.h from $(gcc -print-file-name=include), so we
# don't pass -nostdinc.  -ffreestanding -nostdlib still keeps glibc startup
# and libc archives out of the link.
CFLAGS_BASE := $(ARCH) -O2 -g \
               -ffreestanding -nostdlib -fno-stack-protector -fno-common \
               -static -no-pie -fno-pie

CFLAGS_LUA  := $(CFLAGS_BASE) \
               -I$(PORT_DIR)/include \
               -I$(LUA_SRC) \
               -I$(SOFTFLOAT_INC) \
               -Wno-implicit-fallthrough \
               -Wno-stringop-overflow \
               -Wno-deprecated-declarations \
               -Wno-array-bounds

CFLAGS_RT   := $(CFLAGS_BASE) \
               -I$(PORT_DIR)/include \
               -I$(SOFTFLOAT_INC)

# Keep this consistent across CFLAGS_LUA / CFLAGS_RT — used for cart compile.
CFLAGS_CART := $(CFLAGS_BASE) \
               -I$(PORT_DIR)/include \
               -I$(LUA_SRC) \
               -I$(SOFTFLOAT_INC)

LDFLAGS := -Wl,--build-id=none -Wl,--gc-sections

# ── benchmarks ───────────────────────────────────────────────────────────────
BENCHMARKS  := binarytrees fannkuch fasta mandelbrot nbody spectral-norm \
               entity_update doom_tick doom_tick_gc
ELFS        := $(patsubst %, $(OUTDIR)/lua_cart_%.elf, $(BENCHMARKS))
BENCH_HDRS  := $(patsubst %, $(OUTDIR)/embed/%.h, $(BENCHMARKS))

NATIVE_ELFS := $(OUTDIR)/doom_tick_c.elf

.PHONY: all clean
all: $(OUTDIR) $(ELFS) $(NATIVE_ELFS)

# ── Lua object files ─────────────────────────────────────────────────────────

$(OUTDIR)/lua:
	mkdir -p $@

$(OUTDIR)/lua/%.o: $(LUA_SRC)/%.c | $(OUTDIR)/lua
	$(CROSS) $(CFLAGS_LUA) -c -o $@ $<

# Our linit replacement, compiled as a Lua object so it ends up in the archive.
$(OUTDIR)/lua/lua_init_libs.o: $(PORT_DIR)/lua_init_libs.c | $(OUTDIR)/lua
	$(CROSS) $(CFLAGS_LUA) -c -o $@ $<

LIB_OBJS := $(LUA_OBJS) $(OUTDIR)/lua/lua_init_libs.o

$(OUTDIR)/liblua54.a: $(LIB_OBJS)
	riscv64-linux-gnu-ar crs $@ $(LIB_OBJS)

# ── runtime + softfloat glue ─────────────────────────────────────────────────

$(OUTDIR)/lua_runtime.o: $(PORT_DIR)/lua_runtime.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_RT) -c -o $@ $<

$(OUTDIR)/softfloat-glue.o: $(PORT_DIR)/softfloat-glue.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_RT) -c -o $@ $<

$(OUTDIR)/crt0.o: $(PORT_DIR)/crt0.S | $(OUTDIR)
	$(CROSS) $(CFLAGS_BASE) -c -o $@ $<

# ── benchmark embed headers (xxd -i) ─────────────────────────────────────────

$(OUTDIR)/embed:
	mkdir -p $@

$(OUTDIR)/embed/%.h: $(BENCH)/%.lua | $(OUTDIR)/embed
	cd $(BENCH) && xxd -i $*.lua > $(abspath $@)

# ── native C cart (no Lua VM) ────────────────────────────────────────────────

$(OUTDIR)/doom_tick_c.o: $(PORT_DIR)/doom_tick_c.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_RT) -c -o $@ $<

$(OUTDIR)/doom_tick_c.elf: \
		$(OUTDIR)/doom_tick_c.o \
		$(OUTDIR)/lua_runtime.o \
		$(OUTDIR)/softfloat-glue.o \
		$(OUTDIR)/crt0.o \
		$(SOFTFLOAT_A)
	$(CROSS) $(CFLAGS_RT) $(LDFLAGS) -o $@ \
	    $(OUTDIR)/doom_tick_c.o \
	    $(OUTDIR)/lua_runtime.o \
	    $(OUTDIR)/softfloat-glue.o \
	    $(OUTDIR)/crt0.o \
	    $(SOFTFLOAT_A)

# ── per-benchmark cart ELFs ──────────────────────────────────────────────────
# Each benchmark gets its own object compiled from lua_cart.c with -D flags
# selecting which embedded header / symbol to use.  The xxd symbol naming
# rule: filename → all non-alphanumeric chars replaced by underscores.

define CART_template
$(OUTDIR)/lua_cart_$(1).o: $(PORT_DIR)/lua_cart.c $(OUTDIR)/embed/$(1).h | $(OUTDIR)
	$$(CROSS) $$(CFLAGS_CART) \
	    -DBENCH_NAME='"$(1)"' \
	    -DBENCH_HEADER='"$(abspath $(OUTDIR)/embed/$(1).h)"' \
	    -DBENCH_BUFSYM=$(subst -,_,$(1))_lua \
	    -DBENCH_LENSYM=$(subst -,_,$(1))_lua_len \
	    -DBENCH_FRAMES=$(BENCH_FRAMES_$(1)) \
	    -c -o $$@ $$<

$(OUTDIR)/lua_cart_$(1).elf: \
		$(OUTDIR)/lua_cart_$(1).o \
		$(OUTDIR)/lua_runtime.o \
		$(OUTDIR)/softfloat-glue.o \
		$(OUTDIR)/crt0.o \
		$(OUTDIR)/liblua54.a \
		$(SOFTFLOAT_A)
	$$(CROSS) $$(CFLAGS_CART) $$(LDFLAGS) -o $$@ \
	    $(OUTDIR)/lua_cart_$(1).o \
	    $(OUTDIR)/lua_runtime.o \
	    $(OUTDIR)/softfloat-glue.o \
	    $(OUTDIR)/crt0.o \
	    $(OUTDIR)/liblua54.a \
	    $(SOFTFLOAT_A)
endef

# Default frame counts — small for slow benchmarks, larger for fast ones.
# These are tuned so each cart ELF runs in well under a minute under arm64
# Docker (one-time correctness check).  Override at make time if needed.
BENCH_FRAMES_binarytrees    ?= 20
BENCH_FRAMES_fannkuch       ?= 20
BENCH_FRAMES_fasta          ?= 20
BENCH_FRAMES_mandelbrot     ?= 20
BENCH_FRAMES_nbody          ?= 20
BENCH_FRAMES_spectral-norm  ?= 20
BENCH_FRAMES_entity_update  ?= 50
BENCH_FRAMES_doom_tick      ?= 30
BENCH_FRAMES_doom_tick_gc   ?= 30

$(foreach b,$(BENCHMARKS),$(eval $(call CART_template,$(b))))

$(OUTDIR):
	mkdir -p $@

# ── clean ────────────────────────────────────────────────────────────────────
clean:
	$(RM) -r $(OUTDIR)
