## Spike M — build the cross-host save-state cart ELFs.
##
## Stages 1 and 2 are wired in this iteration:
##   • det_cutscene_linear   — Stage 1 (single fixed-frame save floor)
##   • det_cutscene_branched — Stage 2 (branched body, all-S sweep)
##
## The drivers (m_driver_save.c / _load.c / _full.c) are generic; each
## cart's per-cart shim (cart_workload_<name>.c) supplies the workload
## bytes via xxd-emitted symbols in embed/*.h.

CROSS  ?= riscv64-linux-gnu-gcc
AR     ?= riscv64-linux-gnu-ar
ARCH   := -march=rv32imfc_zicsr -mabi=ilp32f
OUTDIR ?= build

SPIKE_A_DIR ?= /spike-a
SPIKE_B_DIR ?= /spike-b
SPIKE_D_DIR ?= /spike-d
SPIKE_K_DIR ?= /spike-k
SPIKE_M_DIR ?= /spike-m

SOFTFLOAT_A   ?= $(SPIKE_A_DIR)/softfloat-build/softfloat.a
SOFTFLOAT_INC ?= $(SPIKE_A_DIR)/berkeley-softfloat-3/source/include

SPIKE_B_PORT := $(SPIKE_B_DIR)/ports/rv32emu
SPIKE_B_INC  := $(SPIKE_B_PORT)/include
SPIKE_M_INC  := $(SPIKE_M_DIR)/ports/rv32emu/include

CART_RUNTIME_M := $(SPIKE_M_DIR)/cart_runtime
LIB_M          := $(SPIKE_M_DIR)/lib
WORKLOADS_M    := $(SPIKE_M_DIR)/workloads
LIBM_RV32_A    := $(SPIKE_M_DIR)/lib/musl-libm/build/libm-rv32.a

# ── flags ────────────────────────────────────────────────────────────────────
CFLAGS_BASE := $(ARCH) -O2 -g \
               -ffreestanding -nostdlib -fno-stack-protector -fno-common \
               -static -no-pie -fno-pie \
               -fno-fast-math -ffp-contract=off -frounding-math

CFLAGS_RT   := $(CFLAGS_BASE) -DHAVE_LIBM \
               -I$(SPIKE_M_INC) \
               -I$(SPIKE_B_INC) \
               -I$(SOFTFLOAT_INC)

CFLAGS_CART := $(CFLAGS_BASE) -DHAVE_LIBM \
               -I$(SPIKE_M_INC) \
               -I$(SPIKE_B_INC) \
               -I$(CART_RUNTIME_M) \
               -I$(SOFTFLOAT_INC)

LDFLAGS := -Wl,--build-id=none -Wl,--gc-sections

# ── runtime + softfloat glue + crt (reuse spike-b's sources) ─────────────────

$(OUTDIR):
	mkdir -p $@

$(OUTDIR)/lua_runtime.o: $(SPIKE_B_PORT)/lua_runtime.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_RT) -c -o $@ $<

$(OUTDIR)/softfloat-glue.o: $(SPIKE_B_PORT)/softfloat-glue.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_RT) -c -o $@ $<

$(OUTDIR)/crt0.o: $(SPIKE_B_PORT)/crt0.S | $(OUTDIR)
	$(CROSS) $(CFLAGS_BASE) -c -o $@ $<

# ── spike-M cart_runtime ────────────────────────────────────────────────────

$(OUTDIR)/digest.o: $(CART_RUNTIME_M)/digest.c $(CART_RUNTIME_M)/digest.h | $(OUTDIR)
	$(CROSS) $(CFLAGS_RT) -I$(CART_RUNTIME_M) -c -o $@ $<

$(OUTDIR)/pcg32.o: $(CART_RUNTIME_M)/pcg32.c $(CART_RUNTIME_M)/pcg32.h | $(OUTDIR)
	$(CROSS) $(CFLAGS_RT) -I$(CART_RUNTIME_M) -c -o $@ $<

$(OUTDIR)/save_state.o: $(CART_RUNTIME_M)/save_state.c $(CART_RUNTIME_M)/save_state.h | $(OUTDIR)
	$(CROSS) $(CFLAGS_RT) -I$(CART_RUNTIME_M) -c -o $@ $<

$(OUTDIR)/runtime_tracked.o: $(CART_RUNTIME_M)/runtime_tracked.c $(CART_RUNTIME_M)/runtime_tracked.h | $(OUTDIR)
	$(CROSS) $(CFLAGS_RT) -I$(CART_RUNTIME_M) -c -o $@ $<

$(OUTDIR)/region_frame_state.o: $(CART_RUNTIME_M)/region_frame_state.c $(CART_RUNTIME_M)/region_frame_state.h | $(OUTDIR)
	$(CROSS) $(CFLAGS_RT) -I$(CART_RUNTIME_M) -c -o $@ $<

$(OUTDIR)/region_persistent_scripts.o: $(CART_RUNTIME_M)/region_persistent_scripts.c $(CART_RUNTIME_M)/region_persistent_scripts.h | $(OUTDIR)
	$(CROSS) $(CFLAGS_RT) -I$(CART_RUNTIME_M) -c -o $@ $<

$(OUTDIR)/cart_state_lua_simple.o: $(CART_RUNTIME_M)/cart_state_lua_simple.c $(CART_RUNTIME_M)/cart_state_lua_simple.h | $(OUTDIR)
	$(CROSS) $(CFLAGS_RT) -I$(CART_RUNTIME_M) -c -o $@ $<

$(OUTDIR)/save_io.o: $(CART_RUNTIME_M)/save_io.c $(CART_RUNTIME_M)/save_io.h | $(OUTDIR)
	$(CROSS) $(CFLAGS_RT) -I$(CART_RUNTIME_M) -c -o $@ $<

CART_RT_OBJS_M := \
    $(OUTDIR)/digest.o \
    $(OUTDIR)/pcg32.o \
    $(OUTDIR)/save_state.o \
    $(OUTDIR)/runtime_tracked.o \
    $(OUTDIR)/region_frame_state.o \
    $(OUTDIR)/cart_state_lua_simple.o \
    $(OUTDIR)/region_persistent_scripts.o \
    $(OUTDIR)/save_io.o

# ── Lua infrastructure ──────────────────────────────────────────────────────

LUA_DIR := $(SPIKE_B_DIR)/lua
LUA_SRC := $(LUA_DIR)/src

CFLAGS_LUA := $(CFLAGS_BASE) \
              -I$(SPIKE_M_INC) \
              -I$(SPIKE_B_INC) \
              -I$(LUA_SRC) \
              -I$(SOFTFLOAT_INC) \
              -Wno-implicit-fallthrough \
              -Wno-stringop-overflow \
              -Wno-deprecated-declarations \
              -Wno-array-bounds

LUA_OMIT := lua.c luac.c linit.c \
            liolib.c loslib.c loadlib.c \
            ldblib.c lutf8lib.c
LUA_ALL_SRCS := $(notdir $(wildcard $(LUA_SRC)/*.c))
LUA_SRCS     := $(filter-out $(LUA_OMIT), $(LUA_ALL_SRCS))
LUA_OBJS     := $(patsubst %.c, $(OUTDIR)/lua/%.o, $(LUA_SRCS))

$(OUTDIR)/lua:
	mkdir -p $@

$(OUTDIR)/lua/%.o: $(LUA_SRC)/%.c | $(OUTDIR)/lua
	$(CROSS) $(CFLAGS_LUA) -c -o $@ $<

$(OUTDIR)/lua/lua_init_libs.o: $(SPIKE_B_PORT)/lua_init_libs.c | $(OUTDIR)/lua
	$(CROSS) $(CFLAGS_LUA) -c -o $@ $<

LUA_LIB_OBJS := $(LUA_OBJS) $(OUTDIR)/lua/lua_init_libs.o

$(OUTDIR)/liblua54.a: $(LUA_LIB_OBJS)
	$(AR) crs $@ $(LUA_LIB_OBJS)

# ── lib + bindings ──────────────────────────────────────────────────────────

$(OUTDIR)/lua_table_flatten.o: $(LIB_M)/lua_table_flatten.c $(LIB_M)/lua_table_flatten.h | $(OUTDIR)
	$(CROSS) $(CFLAGS_CART) -I$(LUA_SRC) -I$(LIB_M) -c -o $@ $<

$(OUTDIR)/lua_det_bindings_m.o: lua_det_bindings_m.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_CART) -I$(LUA_SRC) -I$(LIB_M) -c -o $@ $<

# ── generic drivers ─────────────────────────────────────────────────────────

$(OUTDIR)/m_driver_save.o: m_driver_save.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_CART) -I$(LUA_SRC) -c -o $@ $<

$(OUTDIR)/m_driver_load.o: m_driver_load.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_CART) -I$(LUA_SRC) -c -o $@ $<

$(OUTDIR)/m_driver_full.o: m_driver_full.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_CART) -I$(LUA_SRC) -c -o $@ $<

# ── Lua workload embedding ──────────────────────────────────────────────────

$(OUTDIR)/embed:
	mkdir -p $@

$(OUTDIR)/embed/blyt32_coroutine.h: $(LIB_M)/blyt32_coroutine.lua | $(OUTDIR)/embed
	cd $(LIB_M) && xxd -i blyt32_coroutine.lua > $(abspath $@)

# Generic workload embed rule: any workload .lua becomes a header.
$(OUTDIR)/embed/det_%.h: $(WORKLOADS_M)/det_%.lua | $(OUTDIR)/embed
	cd $(WORKLOADS_M) && xxd -i det_$*.lua > $(abspath $@)

# ── per-cart link template ──────────────────────────────────────────────────
#
# Each cart needs:
#   $(OUTDIR)/embed/<name>.h           (generated from workloads/<name>.lua)
#   $(OUTDIR)/cart_workload_<name>.o   (compiled from cart_workload_<name>.c)
#   $(OUTDIR)/regions_<name>.o         (compiled from regions_<name>.c)
#   ELFs: <name>_save.elf, <name>_load.elf, <name>_full.elf
#
# define_cart $(1)=cart_name

define define_cart
$(OUTDIR)/cart_workload_$(1).o: cart_workload_$(1).c $(OUTDIR)/embed/$(1).h $(OUTDIR)/embed/blyt32_coroutine.h | $(OUTDIR)
	$$(CROSS) $$(CFLAGS_CART) -I$$(LUA_SRC) -I$(OUTDIR) -c -o $$@ $$<

$(OUTDIR)/regions_$(1).o: regions_$(1).c | $(OUTDIR)
	$$(CROSS) $$(CFLAGS_CART) -c -o $$@ $$<

CART_$(1)_LINK_OBJS := \
    $(OUTDIR)/regions_$(1).o \
    $(OUTDIR)/cart_workload_$(1).o \
    $(OUTDIR)/lua_det_bindings_m.o \
    $(OUTDIR)/lua_table_flatten.o \
    $$(CART_RT_OBJS_M) \
    $(OUTDIR)/lua_runtime.o \
    $(OUTDIR)/softfloat-glue.o \
    $(OUTDIR)/crt0.o \
    $(OUTDIR)/liblua54.a \
    $$(LIBM_RV32_A) \
    $$(SOFTFLOAT_A)

$(OUTDIR)/$(1)_save.elf: $(OUTDIR)/m_driver_save.o $$(CART_$(1)_LINK_OBJS)
	$$(CROSS) $$(CFLAGS_CART) $$(LDFLAGS) -o $$@ \
	    $(OUTDIR)/m_driver_save.o $$(CART_$(1)_LINK_OBJS)

$(OUTDIR)/$(1)_load.elf: $(OUTDIR)/m_driver_load.o $$(CART_$(1)_LINK_OBJS)
	$$(CROSS) $$(CFLAGS_CART) $$(LDFLAGS) -o $$@ \
	    $(OUTDIR)/m_driver_load.o $$(CART_$(1)_LINK_OBJS)

$(OUTDIR)/$(1)_full.elf: $(OUTDIR)/m_driver_full.o $$(CART_$(1)_LINK_OBJS)
	$$(CROSS) $$(CFLAGS_CART) $$(LDFLAGS) -o $$@ \
	    $(OUTDIR)/m_driver_full.o $$(CART_$(1)_LINK_OBJS)

ALL_ELFS += $(OUTDIR)/$(1)_save.elf $(OUTDIR)/$(1)_load.elf $(OUTDIR)/$(1)_full.elf
endef

ALL_ELFS :=

$(eval $(call define_cart,det_cutscene_linear))
$(eval $(call define_cart,det_cutscene_branched))
$(eval $(call define_cart,det_two_scripts))
$(eval $(call define_cart,det_short_script))
$(eval $(call define_cart,det_two_scripts_destroy))
$(eval $(call define_cart,det_scripts_with_entities))
$(eval $(call define_cart,det_table_shapes))
$(eval $(call define_cart,det_transient_coroutine))
$(eval $(call define_cart,det_neg_slot_overflow))
$(eval $(call define_cart,det_neg_unsupported_type))
$(eval $(call define_cart,det_neg_blob_truncation))

# ── targets ─────────────────────────────────────────────────────────────────

.PHONY: all clean
all: $(OUTDIR) $(ALL_ELFS)

clean:
	$(RM) -r $(OUTDIR)
