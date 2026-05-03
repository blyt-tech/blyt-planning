## Spike H — Stage 4: native-Linux RV32 Lua benchmark builds.
##
## Builds the same two workloads spike-b uses (doom_tick, entity_update),
## but as RV32 Linux musl ELFs that link against the cross-toolchain's libc
## rather than spike-b's bare-metal runtime.
##
## Inputs (relative to this Makefile):
##   ../../spike-b/benchmarks/*.lua    benchmark scripts (shared with spike-b)
##   $(LUA_DIR)/src/                   Lua 5.4 source tree (downloaded by Dockerfile)
##
## Output: $(OUTDIR)/lua_cart_<name>.elf — RV32 musl-linked, statically linked.
##
## Override CROSS / LUA_DIR if building outside the spike-h Docker image.

CROSS    ?= riscv32-linux-musl-gcc
AR       ?= riscv32-linux-musl-ar

# Use the cross-toolchain's default -march/-mabi.  musl.cc's
# riscv32-linux-musl ships libc + libgcc for rv32gc/ilp32d only, so any
# non-default ABI fails to link with missing soft-float helpers
# (__addsf3, __mulsf3, __floatsisf, etc.).
#
# The cart-spec ABI is rv32imfc/ilp32f, which differs from rv32gc/ilp32d in
# two ways: no D extension and no A extension.  The Stage 1 hello.elf
# explicitly targets rv32imfc/ilp32f because it uses no libc; the Lua
# workloads use the toolchain default and rebuild against the cart-spec ABI
# is deferred to real-hardware validation on the Milk-V Duo.
ARCH     ?= -march=rv32gc -mabi=ilp32d

OUTDIR   ?= build
LUA_DIR  ?= /opt/lua
LUA_SRC  := $(LUA_DIR)/src
BENCH    ?= ../../spike-b/benchmarks
PORT_DIR := .

# Lua units to omit (same set as spike-b — no io, os, package, etc.).
LUA_OMIT := lua.c luac.c linit.c \
            liolib.c loslib.c loadlib.c \
            lcorolib.c ldblib.c lutf8lib.c
LUA_ALL_SRCS := $(notdir $(wildcard $(LUA_SRC)/*.c))
LUA_SRCS     := $(filter-out $(LUA_OMIT), $(LUA_ALL_SRCS))
LUA_OBJS     := $(patsubst %.c, $(OUTDIR)/lua/%.o, $(LUA_SRCS))

CFLAGS_BASE := $(ARCH) -O2 -g -static -no-pie \
               -Wno-implicit-fallthrough \
               -Wno-stringop-overflow \
               -Wno-deprecated-declarations \
               -Wno-array-bounds

CFLAGS_LUA  := $(CFLAGS_BASE) -I$(LUA_SRC)
CFLAGS_CART := $(CFLAGS_BASE) -I$(LUA_SRC)

LDFLAGS := -Wl,--build-id=none -Wl,--gc-sections

# Only the workloads referenced by PLAN.md §18.
BENCHMARKS  := doom_tick entity_update
ELFS        := $(patsubst %, $(OUTDIR)/lua_cart_%.elf, $(BENCHMARKS))
BENCH_HDRS  := $(patsubst %, $(OUTDIR)/embed/%.h, $(BENCHMARKS))

.PHONY: all clean arch-probe
all: $(OUTDIR) $(ELFS)

# ── Lua object files ─────────────────────────────────────────────────────────

$(OUTDIR)/lua:
	mkdir -p $@

$(OUTDIR)/lua/%.o: $(LUA_SRC)/%.c | $(OUTDIR)/lua
	$(CROSS) $(CFLAGS_LUA) -c -o $@ $<

$(OUTDIR)/lua/lua_init_libs.o: $(PORT_DIR)/lua_init_libs.c | $(OUTDIR)/lua
	$(CROSS) $(CFLAGS_LUA) -c -o $@ $<

LIB_OBJS := $(LUA_OBJS) $(OUTDIR)/lua/lua_init_libs.o

$(OUTDIR)/liblua54.a: $(LIB_OBJS)
	$(AR) crs $@ $(LIB_OBJS)

# ── benchmark embed headers ──────────────────────────────────────────────────

$(OUTDIR)/embed:
	mkdir -p $@

$(OUTDIR)/embed/%.h: $(BENCH)/%.lua | $(OUTDIR)/embed
	cd $(BENCH) && xxd -i $*.lua > $(abspath $@)

# ── per-benchmark cart ELFs ──────────────────────────────────────────────────

define CART_template
$(OUTDIR)/lua_cart_$(1).o: $(PORT_DIR)/lua_cart_linux.c $(OUTDIR)/embed/$(1).h | $(OUTDIR)
	$$(CROSS) $$(CFLAGS_CART) \
	    -DBENCH_NAME='"$(1)"' \
	    -DBENCH_HEADER='"$(abspath $(OUTDIR)/embed/$(1).h)"' \
	    -DBENCH_BUFSYM=$(subst -,_,$(1))_lua \
	    -DBENCH_LENSYM=$(subst -,_,$(1))_lua_len \
	    -DBENCH_FRAMES=$(BENCH_FRAMES_$(1)) \
	    -c -o $$@ $$<

$(OUTDIR)/lua_cart_$(1).elf: \
		$(OUTDIR)/lua_cart_$(1).o \
		$(OUTDIR)/liblua54.a
	$$(CROSS) $$(CFLAGS_CART) $$(LDFLAGS) -o $$@ \
	    $(OUTDIR)/lua_cart_$(1).o \
	    $(OUTDIR)/liblua54.a \
	    -lm
endef

BENCH_FRAMES_doom_tick      ?= 30
BENCH_FRAMES_entity_update  ?= 50

$(foreach b,$(BENCHMARKS),$(eval $(call CART_template,$(b))))

$(OUTDIR):
	mkdir -p $@

# ── arch probe ───────────────────────────────────────────────────────────────
# Print which -march/-mabi combinations the toolchain accepts.  Used to
# diagnose ABI mismatches; not part of `all`.
arch-probe:
	@for a in 'rv32imac -mabi=ilp32' 'rv32imafc -mabi=ilp32f' 'rv32imfc_zicsr -mabi=ilp32f' 'rv32gc -mabi=ilp32d'; do \
	    echo -n "$$a: "; \
	    echo 'int main(void){return 0;}' | $(CROSS) -march=$$a -x c -nostdlib -o /dev/null - 2>&1 \
	        && echo OK || echo FAIL; \
	done

# ── clean ────────────────────────────────────────────────────────────────────
clean:
	$(RM) -r $(OUTDIR)
