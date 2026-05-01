## Spike D — build the three deterministic cart ELFs.
##
## Outputs (in $(OUTDIR), default ./build):
##   lua_cart_det_doom_tick.elf
##   lua_cart_det_entity_update.elf
##   whetstone.elf
##
## Each cart links: cart_runtime/* + libm-rv32.a + (Lua + spike-b's
## lua_runtime.c with -DHAVE_LIBM) + spike-a's softfloat archive +
## spike-b's softfloat-glue.c + crt0.S.
##
## Layout assumed (relative to this Makefile, when run inside Docker):
##   /spike-a/                     spike-a artefacts (rv32emu, softfloat)
##   /spike-b/lua/                 vendored Lua 5.4 source
##   /spike-b/ports/rv32emu/       spike-b's port (lua_runtime.c, crt0.S, ...)
##   /spike-d/cart_runtime/        digest, pcg32, frame_state
##   /spike-d/lib/musl-libm/build/ libm-rv32.a (built first)
##   /spike-d/workloads/           det_doom_tick.lua, det_entity_update.lua
##   /spike-d/ports/rv32emu/       this file + lua_det_*.c + whetstone.c
##
## Outside Docker the same paths can be passed via the *_DIR variables.

CROSS  ?= riscv64-linux-gnu-gcc
AR     ?= riscv64-linux-gnu-ar
ARCH   := -march=rv32imfc_zicsr -mabi=ilp32f
OUTDIR ?= build

SPIKE_A_DIR ?= /spike-a
SPIKE_B_DIR ?= /spike-b
SPIKE_D_DIR ?= /spike-d

SOFTFLOAT_A   ?= $(SPIKE_A_DIR)/softfloat-build/softfloat.a
SOFTFLOAT_INC ?= $(SPIKE_A_DIR)/berkeley-softfloat-3/source/include

LUA_DIR := $(SPIKE_B_DIR)/lua
LUA_SRC := $(LUA_DIR)/src

SPIKE_B_PORT := $(SPIKE_B_DIR)/ports/rv32emu
SPIKE_B_INC  := $(SPIKE_B_PORT)/include
SPIKE_D_INC  := $(SPIKE_D_DIR)/ports/rv32emu/include

CART_RUNTIME := $(SPIKE_D_DIR)/cart_runtime
WORKLOADS    := $(SPIKE_D_DIR)/workloads

LIBM_RV32_A  := $(SPIKE_D_DIR)/lib/musl-libm/build/libm-rv32.a

# ── Lua source selection (mirrors spike-b) ───────────────────────────────────
LUA_OMIT := lua.c luac.c linit.c \
            liolib.c loslib.c loadlib.c \
            lcorolib.c ldblib.c lutf8lib.c
LUA_ALL_SRCS := $(notdir $(wildcard $(LUA_SRC)/*.c))
LUA_SRCS     := $(filter-out $(LUA_OMIT), $(LUA_ALL_SRCS))
LUA_OBJS     := $(patsubst %.c, $(OUTDIR)/lua/%.o, $(LUA_SRCS))

# ── flags ────────────────────────────────────────────────────────────────────
# Spike-d's include path goes BEFORE spike-b's so our math.h shadows
# spike-b's (we need isnan/isinf/double_t for the musl float kernels).
CFLAGS_BASE := $(ARCH) -O2 -g \
               -ffreestanding -nostdlib -fno-stack-protector -fno-common \
               -static -no-pie -fno-pie \
               -fno-fast-math -ffp-contract=off -frounding-math

CFLAGS_LUA  := $(CFLAGS_BASE) \
               -I$(SPIKE_D_INC) \
               -I$(SPIKE_B_INC) \
               -I$(LUA_SRC) \
               -I$(SOFTFLOAT_INC) \
               -Wno-implicit-fallthrough \
               -Wno-stringop-overflow \
               -Wno-deprecated-declarations \
               -Wno-array-bounds

CFLAGS_RT   := $(CFLAGS_BASE) -DHAVE_LIBM \
               -I$(SPIKE_D_INC) \
               -I$(SPIKE_B_INC) \
               -I$(SOFTFLOAT_INC)

CFLAGS_CART := $(CFLAGS_BASE) \
               -I$(SPIKE_D_INC) \
               -I$(SPIKE_B_INC) \
               -I$(LUA_SRC) \
               -I$(CART_RUNTIME) \
               -I$(SOFTFLOAT_INC)

LDFLAGS := -Wl,--build-id=none -Wl,--gc-sections

# ── workloads ────────────────────────────────────────────────────────────────
LUA_WORKLOADS := det_doom_tick det_entity_update
ALL_LUA_ELFS  := $(patsubst %, $(OUTDIR)/lua_cart_%.elf, $(LUA_WORKLOADS))
WHETSTONE_ELF := $(OUTDIR)/whetstone.elf

ALL_ELFS := $(ALL_LUA_ELFS) $(WHETSTONE_ELF)

.PHONY: all clean
all: $(OUTDIR) $(ALL_ELFS)

# ── Lua object files ─────────────────────────────────────────────────────────

$(OUTDIR)/lua:
	mkdir -p $@

$(OUTDIR)/lua/%.o: $(LUA_SRC)/%.c | $(OUTDIR)/lua
	$(CROSS) $(CFLAGS_LUA) -c -o $@ $<

$(OUTDIR)/lua/lua_init_libs.o: $(SPIKE_B_PORT)/lua_init_libs.c | $(OUTDIR)/lua
	$(CROSS) $(CFLAGS_LUA) -c -o $@ $<

LIB_OBJS := $(LUA_OBJS) $(OUTDIR)/lua/lua_init_libs.o

$(OUTDIR)/liblua54.a: $(LIB_OBJS)
	$(AR) crs $@ $(LIB_OBJS)

# ── runtime + softfloat glue + crt ───────────────────────────────────────────
# lua_runtime.c is reused from spike-b but compiled with -DHAVE_LIBM so
# its float-precision math stubs are suppressed (musl provides them).
$(OUTDIR)/lua_runtime.o: $(SPIKE_B_PORT)/lua_runtime.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_RT) -c -o $@ $<

$(OUTDIR)/softfloat-glue.o: $(SPIKE_B_PORT)/softfloat-glue.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_RT) -c -o $@ $<

$(OUTDIR)/crt0.o: $(SPIKE_B_PORT)/crt0.S | $(OUTDIR)
	$(CROSS) $(CFLAGS_BASE) -c -o $@ $<

# ── cart_runtime ─────────────────────────────────────────────────────────────

$(OUTDIR)/digest.o: $(CART_RUNTIME)/digest.c $(CART_RUNTIME)/digest.h | $(OUTDIR)
	$(CROSS) $(CFLAGS_RT) -I$(CART_RUNTIME) -c -o $@ $<

$(OUTDIR)/pcg32.o: $(CART_RUNTIME)/pcg32.c $(CART_RUNTIME)/pcg32.h | $(OUTDIR)
	$(CROSS) $(CFLAGS_RT) -I$(CART_RUNTIME) -c -o $@ $<

$(OUTDIR)/lua_det_bindings.o: lua_det_bindings.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_CART) -c -o $@ $<

CART_RT_OBJS := $(OUTDIR)/digest.o $(OUTDIR)/pcg32.o

# ── workload embed headers (xxd -i) ──────────────────────────────────────────

$(OUTDIR)/embed:
	mkdir -p $@

$(OUTDIR)/embed/%.h: $(WORKLOADS)/%.lua | $(OUTDIR)/embed
	cd $(WORKLOADS) && xxd -i $*.lua > $(abspath $@)

# ── Lua det carts ────────────────────────────────────────────────────────────
# Each Lua workload gets its own object compiled from lua_det_cart.c with
# -D selecting the embedded header.

define LUA_CART_template
$(OUTDIR)/lua_det_cart_$(1).o: lua_det_cart.c $(OUTDIR)/embed/$(1).h | $(OUTDIR)
	$$(CROSS) $$(CFLAGS_CART) \
	    -DWORKLOAD_NAME='"$(1)"' \
	    -DWORKLOAD_HEADER='"$(abspath $(OUTDIR)/embed/$(1).h)"' \
	    -DWORKLOAD_BUFSYM=$(subst -,_,$(1))_lua \
	    -DWORKLOAD_LENSYM=$(subst -,_,$(1))_lua_len \
	    -c -o $$@ $$<

$(OUTDIR)/lua_cart_$(1).elf: \
		$(OUTDIR)/lua_det_cart_$(1).o \
		$(OUTDIR)/lua_det_bindings.o \
		$(CART_RT_OBJS) \
		$(OUTDIR)/lua_runtime.o \
		$(OUTDIR)/softfloat-glue.o \
		$(OUTDIR)/crt0.o \
		$(OUTDIR)/liblua54.a \
		$(LIBM_RV32_A) \
		$(SOFTFLOAT_A)
	$$(CROSS) $$(CFLAGS_CART) $$(LDFLAGS) -o $$@ \
	    $(OUTDIR)/lua_det_cart_$(1).o \
	    $(OUTDIR)/lua_det_bindings.o \
	    $(CART_RT_OBJS) \
	    $(OUTDIR)/lua_runtime.o \
	    $(OUTDIR)/softfloat-glue.o \
	    $(OUTDIR)/crt0.o \
	    $(OUTDIR)/liblua54.a \
	    $(LIBM_RV32_A) \
	    $(SOFTFLOAT_A)
endef

$(foreach w,$(LUA_WORKLOADS),$(eval $(call LUA_CART_template,$(w))))

# ── whetstone (pure C, no Lua) ───────────────────────────────────────────────

$(OUTDIR)/whetstone.o: whetstone.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_RT) -I$(CART_RUNTIME) -c -o $@ $<

$(WHETSTONE_ELF): \
		$(OUTDIR)/whetstone.o \
		$(CART_RT_OBJS) \
		$(OUTDIR)/lua_runtime.o \
		$(OUTDIR)/softfloat-glue.o \
		$(OUTDIR)/crt0.o \
		$(LIBM_RV32_A) \
		$(SOFTFLOAT_A)
	$(CROSS) $(CFLAGS_RT) $(LDFLAGS) -o $@ \
	    $(OUTDIR)/whetstone.o \
	    $(CART_RT_OBJS) \
	    $(OUTDIR)/lua_runtime.o \
	    $(OUTDIR)/softfloat-glue.o \
	    $(OUTDIR)/crt0.o \
	    $(LIBM_RV32_A) \
	    $(SOFTFLOAT_A)

$(OUTDIR):
	mkdir -p $@

# ── clean ────────────────────────────────────────────────────────────────────

clean:
	$(RM) -r $(OUTDIR)
