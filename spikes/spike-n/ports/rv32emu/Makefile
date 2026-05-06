## Spike N — build the hot-reload cart ELFs.
##
## Stages:
##   Stage 1 — no-op native + Lua carts (floor: rebuild with no source change)
##   Stage 2 — native edit pairs n1-n6 (save ELF + load ELF per edit)
##   Stage 3 — Lua edit pairs l1-l5 (save ELF + load ELF per edit)
##   Stage 4 — Lua coroutine ELFs (l7 pre, l8 pre+post)
##   Stage 5 — Lua failure ELFs (l6, l9, l10 post)
##
## Each native edit pair is compiled with:
##   -DEDIT_NUM=N -DEDIT_SIDE=0 -DNATIVE_STATE_VERSION=<pre_v>   (save ELF)
##   -DEDIT_NUM=N -DEDIT_SIDE=1 -DNATIVE_STATE_VERSION=<post_v>  (load ELF)
## Each Lua edit pair is compiled with:
##   -DLUA_EDIT_NUM=N -DLUA_EDIT_SIDE_POST=0 -DLUA_STATE_VERSION=<pre_v>  (save ELF)
##   -DLUA_EDIT_NUM=N -DLUA_EDIT_SIDE_POST=1 -DLUA_STATE_VERSION=<post_v> (load ELF)

CROSS  ?= riscv64-linux-gnu-gcc
AR     ?= riscv64-linux-gnu-ar
ARCH   := -march=rv32imfc_zicsr -mabi=ilp32f
OUTDIR ?= build

SPIKE_A_DIR ?= /spike-a
SPIKE_B_DIR ?= /spike-b
SPIKE_D_DIR ?= /spike-d
SPIKE_K_DIR ?= /spike-k
SPIKE_M_DIR ?= /spike-m
SPIKE_N_DIR ?= /spike-n

SOFTFLOAT_A   ?= $(SPIKE_A_DIR)/softfloat-build/softfloat.a
SOFTFLOAT_INC ?= $(SPIKE_A_DIR)/berkeley-softfloat-3/source/include

SPIKE_B_PORT := $(SPIKE_B_DIR)/ports/rv32emu
SPIKE_B_INC  := $(SPIKE_B_PORT)/include
SPIKE_M_INC  := $(SPIKE_M_DIR)/ports/rv32emu/include
SPIKE_N_INC  := $(SPIKE_N_DIR)/ports/rv32emu/include

CART_RUNTIME_K := $(SPIKE_K_DIR)/cart_runtime
CART_RUNTIME_M := $(SPIKE_M_DIR)/cart_runtime
CART_RUNTIME_N := $(SPIKE_N_DIR)/cart_runtime
LIB_M          := $(SPIKE_M_DIR)/lib
LIB_N          := $(SPIKE_N_DIR)/lib
WORKLOADS_N    := $(SPIKE_N_DIR)/workloads
LIBM_RV32_A    := $(SPIKE_M_DIR)/lib/musl-libm/build/libm-rv32.a

# ── flags ─────────────────────────────────────────────────────────────────────

CFLAGS_BASE := $(ARCH) -O2 -g \
               -ffreestanding -nostdlib -fno-stack-protector -fno-common \
               -static -no-pie -fno-pie \
               -fno-fast-math -ffp-contract=off -frounding-math

CFLAGS_RT   := $(CFLAGS_BASE) -DHAVE_LIBM \
               -I$(SPIKE_N_INC) \
               -I$(SPIKE_M_INC) \
               -I$(SPIKE_B_INC) \
               -I$(SOFTFLOAT_INC)

CFLAGS_CART := $(CFLAGS_BASE) -DHAVE_LIBM \
               -I$(SPIKE_N_INC) \
               -I$(SPIKE_M_INC) \
               -I$(SPIKE_B_INC) \
               -I$(CART_RUNTIME_N) \
               -I$(CART_RUNTIME_M) \
               -I$(CART_RUNTIME_K) \
               -I$(SOFTFLOAT_INC)

LDFLAGS := -Wl,--build-id=none -Wl,--gc-sections

# ── runtime + Lua infrastructure (reuse spike-m) ──────────────────────────────

$(OUTDIR):
	mkdir -p $@

$(OUTDIR)/lua_runtime.o: $(SPIKE_B_PORT)/lua_runtime.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_RT) -c -o $@ $<

$(OUTDIR)/softfloat-glue.o: $(SPIKE_B_PORT)/softfloat-glue.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_RT) -c -o $@ $<

$(OUTDIR)/crt0.o: $(SPIKE_B_PORT)/crt0.S | $(OUTDIR)
	$(CROSS) $(CFLAGS_BASE) -c -o $@ $<

# ── cart_runtime objects ───────────────────────────────────────────────────────

$(OUTDIR)/digest.o: $(CART_RUNTIME_M)/digest.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_RT) -I$(CART_RUNTIME_M) -c -o $@ $<

$(OUTDIR)/pcg32.o: $(CART_RUNTIME_M)/pcg32.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_RT) -I$(CART_RUNTIME_M) -c -o $@ $<

$(OUTDIR)/save_state.o: $(CART_RUNTIME_M)/save_state.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_RT) -I$(CART_RUNTIME_M) -c -o $@ $<

$(OUTDIR)/save_state_n.o: $(CART_RUNTIME_N)/save_state_n.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_RT) -I$(CART_RUNTIME_N) -I$(CART_RUNTIME_M) -I$(CART_RUNTIME_K) -c -o $@ $<

$(OUTDIR)/migrate.o: $(CART_RUNTIME_N)/migrate.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_RT) -I$(CART_RUNTIME_N) -c -o $@ $<

$(OUTDIR)/runtime_tracked.o: $(CART_RUNTIME_M)/runtime_tracked.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_RT) -I$(CART_RUNTIME_M) -c -o $@ $<

$(OUTDIR)/region_frame_state.o: $(CART_RUNTIME_M)/region_frame_state.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_RT) -I$(CART_RUNTIME_M) -I$(CART_RUNTIME_K) -c -o $@ $<

$(OUTDIR)/region_persistent_scripts.o: $(CART_RUNTIME_M)/region_persistent_scripts.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_RT) -I$(CART_RUNTIME_M) -c -o $@ $<

$(OUTDIR)/cart_state_lua_simple.o: $(CART_RUNTIME_M)/cart_state_lua_simple.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_RT) -I$(CART_RUNTIME_M) -c -o $@ $<

$(OUTDIR)/save_io.o: $(CART_RUNTIME_M)/save_io.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_RT) -I$(CART_RUNTIME_M) -c -o $@ $<

$(OUTDIR)/hot_reload_diagnostic.o: $(CART_RUNTIME_N)/hot_reload_diagnostic.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_RT) -I$(CART_RUNTIME_N) -c -o $@ $<

# ── Lua VM ─────────────────────────────────────────────────────────────────────

LUA_DIR := $(SPIKE_B_DIR)/lua
LUA_SRC := $(LUA_DIR)/src

CFLAGS_LUA := $(CFLAGS_BASE) \
              -I$(SPIKE_N_INC) -I$(SPIKE_M_INC) -I$(SPIKE_B_INC) \
              -I$(LUA_SRC) -I$(SOFTFLOAT_INC) \
              -Wno-implicit-fallthrough -Wno-stringop-overflow \
              -Wno-deprecated-declarations -Wno-array-bounds

LUA_OMIT := lua.c luac.c linit.c liolib.c loslib.c loadlib.c ldblib.c lutf8lib.c
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

# ── lib + bindings ─────────────────────────────────────────────────────────────

$(OUTDIR)/lua_table_flatten.o: $(LIB_M)/lua_table_flatten.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_CART) -I$(LUA_SRC) -I$(LIB_M) -c -o $@ $<

# ── embed headers ──────────────────────────────────────────────────────────────

$(OUTDIR)/embed:
	mkdir -p $@

$(OUTDIR)/embed/blyt32_coroutine.h: $(LIB_M)/blyt32_coroutine.lua | $(OUTDIR)/embed
	cd $(LIB_M) && xxd -i blyt32_coroutine.lua > $(abspath $@)

$(OUTDIR)/embed/blyt32_hot_reload.h: $(LIB_N)/blyt32_hot_reload.lua | $(OUTDIR)/embed
	cd $(LIB_N) && xxd -i blyt32_hot_reload.lua > $(abspath $@)

# Generic workload embed rule: any workload .lua in WORKLOADS_N becomes a header.
$(OUTDIR)/embed/det_%.h: $(WORKLOADS_N)/det_%.lua | $(OUTDIR)/embed
	cd $(WORKLOADS_N) && xxd -i det_$*.lua > $(abspath $@)

# ── Common base object sets ────────────────────────────────────────────────────

CART_RT_BASE_OBJS := \
    $(OUTDIR)/digest.o \
    $(OUTDIR)/pcg32.o \
    $(OUTDIR)/save_state.o \
    $(OUTDIR)/save_state_n.o \
    $(OUTDIR)/migrate.o \
    $(OUTDIR)/runtime_tracked.o \
    $(OUTDIR)/region_frame_state.o \
    $(OUTDIR)/save_io.o \
    $(OUTDIR)/hot_reload_diagnostic.o

CART_RT_LUA_OBJS := \
    $(CART_RT_BASE_OBJS) \
    $(OUTDIR)/cart_state_lua_simple.o \
    $(OUTDIR)/region_persistent_scripts.o

LUA_LINK_EXTRAS := \
    $(OUTDIR)/lua_runtime.o \
    $(OUTDIR)/softfloat-glue.o \
    $(OUTDIR)/crt0.o \
    $(OUTDIR)/liblua54.a \
    $(LIBM_RV32_A) \
    $(SOFTFLOAT_A)

NATIVE_LINK_EXTRAS := \
    $(OUTDIR)/lua_runtime.o \
    $(OUTDIR)/softfloat-glue.o \
    $(OUTDIR)/crt0.o \
    $(LIBM_RV32_A) \
    $(SOFTFLOAT_A)

ALL_ELFS :=

# ── Stage 1: no-op native ──────────────────────────────────────────────────────

$(OUTDIR)/region_native_state_v0.o: $(CART_RUNTIME_N)/region_native_state.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_CART) -DNATIVE_STATE_VERSION=0 -c -o $@ $<

$(OUTDIR)/regions_native_noop.o: regions_native_noop.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_CART) -DNATIVE_STATE_VERSION=0 -c -o $@ $<

$(OUTDIR)/cart_native_noop.o: cart_native_noop.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_CART) -DNATIVE_STATE_VERSION=0 -c -o $@ $<

$(OUTDIR)/n_native_driver_save.o: n_native_driver_save.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_CART) -c -o $@ $<

$(OUTDIR)/n_native_driver_load.o: n_native_driver_load.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_CART) -c -o $@ $<

$(OUTDIR)/n_native_driver_full.o: n_native_driver_full.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_CART) -c -o $@ $<

NOOP_NATIVE_OBJS := \
    $(OUTDIR)/regions_native_noop.o \
    $(OUTDIR)/cart_native_noop.o \
    $(OUTDIR)/region_native_state_v0.o \
    $(CART_RT_BASE_OBJS)

$(OUTDIR)/det_noop_native_save.elf: $(OUTDIR)/n_native_driver_save.o $(NOOP_NATIVE_OBJS) | $(OUTDIR)
	$(CROSS) $(CFLAGS_CART) $(LDFLAGS) -o $@ $(OUTDIR)/n_native_driver_save.o $(NOOP_NATIVE_OBJS) $(NATIVE_LINK_EXTRAS)

$(OUTDIR)/det_noop_native_load.elf: $(OUTDIR)/n_native_driver_load.o $(NOOP_NATIVE_OBJS) | $(OUTDIR)
	$(CROSS) $(CFLAGS_CART) $(LDFLAGS) -o $@ $(OUTDIR)/n_native_driver_load.o $(NOOP_NATIVE_OBJS) $(NATIVE_LINK_EXTRAS)

$(OUTDIR)/det_noop_native_full.elf: $(OUTDIR)/n_native_driver_full.o $(NOOP_NATIVE_OBJS) | $(OUTDIR)
	$(CROSS) $(CFLAGS_CART) $(LDFLAGS) -o $@ $(OUTDIR)/n_native_driver_full.o $(NOOP_NATIVE_OBJS) $(NATIVE_LINK_EXTRAS)

ALL_ELFS += $(OUTDIR)/det_noop_native_save.elf \
            $(OUTDIR)/det_noop_native_load.elf \
            $(OUTDIR)/det_noop_native_full.elf

# ── Stage 1: no-op Lua ────────────────────────────────────────────────────────

$(OUTDIR)/lua_det_bindings_n_v0.o: lua_det_bindings_n.c \
        $(OUTDIR)/embed/blyt32_coroutine.h \
        $(OUTDIR)/embed/blyt32_hot_reload.h | $(OUTDIR)
	$(CROSS) $(CFLAGS_CART) -DLUA_STATE_VERSION=0 \
	    -I$(LUA_SRC) -I$(LIB_M) -I$(LIB_N) \
	    -c -o $@ $<

$(OUTDIR)/region_lua_cart_state_v0.o: $(CART_RUNTIME_N)/region_lua_cart_state.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_CART) -DLUA_STATE_VERSION=0 -c -o $@ $<

$(OUTDIR)/regions_det_noop_lua.o: regions_det_noop_lua.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_CART) -DLUA_STATE_VERSION=0 -c -o $@ $<

$(OUTDIR)/cart_workload_det_noop_lua.o: cart_workload_det_noop_lua.c \
        $(OUTDIR)/embed/det_noop_lua.h \
        $(OUTDIR)/embed/blyt32_coroutine.h | $(OUTDIR)
	$(CROSS) $(CFLAGS_CART) -I$(LUA_SRC) -I$(OUTDIR) -c -o $@ $<

# Generic Spike M drivers (reused for Lua carts)
$(OUTDIR)/m_driver_save.o: $(SPIKE_M_DIR)/ports/rv32emu/m_driver_save.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_CART) -I$(LUA_SRC) \
	    -I$(CART_RUNTIME_M) -I$(CART_RUNTIME_K) \
	    -c -o $@ $<

$(OUTDIR)/m_driver_load.o: $(SPIKE_M_DIR)/ports/rv32emu/m_driver_load.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_CART) -I$(LUA_SRC) \
	    -I$(CART_RUNTIME_M) -I$(CART_RUNTIME_K) \
	    -c -o $@ $<

$(OUTDIR)/m_driver_full.o: $(SPIKE_M_DIR)/ports/rv32emu/m_driver_full.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_CART) -I$(LUA_SRC) \
	    -I$(CART_RUNTIME_M) -I$(CART_RUNTIME_K) \
	    -c -o $@ $<

NOOP_LUA_LINK_OBJS := \
    $(OUTDIR)/regions_det_noop_lua.o \
    $(OUTDIR)/cart_workload_det_noop_lua.o \
    $(OUTDIR)/lua_det_bindings_n_v0.o \
    $(OUTDIR)/lua_table_flatten.o \
    $(OUTDIR)/region_lua_cart_state_v0.o \
    $(CART_RT_LUA_OBJS)

$(OUTDIR)/det_noop_lua_save.elf: $(OUTDIR)/m_driver_save.o $(NOOP_LUA_LINK_OBJS) | $(OUTDIR)
	$(CROSS) $(CFLAGS_CART) $(LDFLAGS) -o $@ \
	    $(OUTDIR)/m_driver_save.o $(NOOP_LUA_LINK_OBJS) $(LUA_LINK_EXTRAS)

$(OUTDIR)/det_noop_lua_load.elf: $(OUTDIR)/m_driver_load.o $(NOOP_LUA_LINK_OBJS) | $(OUTDIR)
	$(CROSS) $(CFLAGS_CART) $(LDFLAGS) -o $@ \
	    $(OUTDIR)/m_driver_load.o $(NOOP_LUA_LINK_OBJS) $(LUA_LINK_EXTRAS)

$(OUTDIR)/det_noop_lua_full.elf: $(OUTDIR)/m_driver_full.o $(NOOP_LUA_LINK_OBJS) | $(OUTDIR)
	$(CROSS) $(CFLAGS_CART) $(LDFLAGS) -o $@ \
	    $(OUTDIR)/m_driver_full.o $(NOOP_LUA_LINK_OBJS) $(LUA_LINK_EXTRAS)

ALL_ELFS += $(OUTDIR)/det_noop_lua_save.elf \
            $(OUTDIR)/det_noop_lua_load.elf \
            $(OUTDIR)/det_noop_lua_full.elf

# ── Stage 2: native edit ELF macro ────────────────────────────────────────────
#
# define_native_edit(edit_num, pre_ver, post_ver)

define define_native_edit
$(OUTDIR)/region_native_state_v$(2).o: $(CART_RUNTIME_N)/region_native_state.c | $(OUTDIR)
	$$(CROSS) $$(CFLAGS_CART) -DNATIVE_STATE_VERSION=$(2) -c -o $$@ $$<

$(OUTDIR)/region_native_state_v$(3).o: $(CART_RUNTIME_N)/region_native_state.c | $(OUTDIR)
	$$(CROSS) $$(CFLAGS_CART) -DNATIVE_STATE_VERSION=$(3) -c -o $$@ $$<

$(OUTDIR)/cart_native_edit_n$(1)_pre.o: cart_native_edit.c | $(OUTDIR)
	$$(CROSS) $$(CFLAGS_CART) -DEDIT_NUM=$(1) -DEDIT_SIDE=0 -DNATIVE_STATE_VERSION=$(2) -c -o $$@ $$<

$(OUTDIR)/cart_native_edit_n$(1)_post.o: cart_native_edit.c | $(OUTDIR)
	$$(CROSS) $$(CFLAGS_CART) -DEDIT_NUM=$(1) -DEDIT_SIDE=1 -DNATIVE_STATE_VERSION=$(3) -c -o $$@ $$<

$(OUTDIR)/regions_native_edit_n$(1)_pre.o: regions_native_edit.c | $(OUTDIR)
	$$(CROSS) $$(CFLAGS_CART) -DEDIT_NUM=$(1) -DEDIT_SIDE=0 -DNATIVE_STATE_VERSION=$(2) -c -o $$@ $$<

$(OUTDIR)/regions_native_edit_n$(1)_post.o: regions_native_edit.c | $(OUTDIR)
	$$(CROSS) $$(CFLAGS_CART) -DEDIT_NUM=$(1) -DEDIT_SIDE=1 -DNATIVE_STATE_VERSION=$(3) -c -o $$@ $$<

NATIVE_N$(1)_PRE_OBJS := \
    $(OUTDIR)/regions_native_edit_n$(1)_pre.o \
    $(OUTDIR)/cart_native_edit_n$(1)_pre.o \
    $(OUTDIR)/region_native_state_v$(2).o \
    $$(CART_RT_BASE_OBJS)

NATIVE_N$(1)_POST_OBJS := \
    $(OUTDIR)/regions_native_edit_n$(1)_post.o \
    $(OUTDIR)/cart_native_edit_n$(1)_post.o \
    $(OUTDIR)/region_native_state_v$(3).o \
    $$(CART_RT_BASE_OBJS)

$(OUTDIR)/det_native_n$(1)_pre_save.elf: $(OUTDIR)/n_native_driver_save.o $$(NATIVE_N$(1)_PRE_OBJS)
	$$(CROSS) $$(CFLAGS_CART) $$(LDFLAGS) -o $$@ \
	    $(OUTDIR)/n_native_driver_save.o $$(NATIVE_N$(1)_PRE_OBJS) $$(NATIVE_LINK_EXTRAS)

$(OUTDIR)/det_native_n$(1)_pre_full.elf: $(OUTDIR)/n_native_driver_full.o $$(NATIVE_N$(1)_PRE_OBJS)
	$$(CROSS) $$(CFLAGS_CART) $$(LDFLAGS) -o $$@ \
	    $(OUTDIR)/n_native_driver_full.o $$(NATIVE_N$(1)_PRE_OBJS) $$(NATIVE_LINK_EXTRAS)

$(OUTDIR)/det_native_n$(1)_post_load.elf: $(OUTDIR)/n_native_driver_load.o $$(NATIVE_N$(1)_POST_OBJS)
	$$(CROSS) $$(CFLAGS_CART) $$(LDFLAGS) -o $$@ \
	    $(OUTDIR)/n_native_driver_load.o $$(NATIVE_N$(1)_POST_OBJS) $$(NATIVE_LINK_EXTRAS)

$(OUTDIR)/det_native_n$(1)_post_full.elf: $(OUTDIR)/n_native_driver_full.o $$(NATIVE_N$(1)_POST_OBJS)
	$$(CROSS) $$(CFLAGS_CART) $$(LDFLAGS) -o $$@ \
	    $(OUTDIR)/n_native_driver_full.o $$(NATIVE_N$(1)_POST_OBJS) $$(NATIVE_LINK_EXTRAS)

ALL_ELFS += \
    $(OUTDIR)/det_native_n$(1)_pre_save.elf \
    $(OUTDIR)/det_native_n$(1)_pre_full.elf \
    $(OUTDIR)/det_native_n$(1)_post_load.elf \
    $(OUTDIR)/det_native_n$(1)_post_full.elf
endef

# (edit_num, pre_version, post_version)
$(eval $(call define_native_edit,1,0,0))
$(eval $(call define_native_edit,2,0,0))
$(eval $(call define_native_edit,3,0,3))
$(eval $(call define_native_edit,4,3,4))
$(eval $(call define_native_edit,5,4,5))
$(eval $(call define_native_edit,6,5,6))

# ── Stage 3-5: Lua edit ELF macro ─────────────────────────────────────────────
#
# define_lua_edit(edit_num, pre_lua_state_ver, post_lua_state_ver)
# For coroutine edits, lua_state_ver is always 0.

define define_lua_edit
$(OUTDIR)/lua_det_bindings_n_v$(2).o: lua_det_bindings_n.c \
        $(OUTDIR)/embed/blyt32_coroutine.h | $(OUTDIR)
	$$(CROSS) $$(CFLAGS_CART) -DLUA_STATE_VERSION=$(2) \
	    -I$(LUA_SRC) -I$(LIB_M) -I$(LIB_N) \
	    -c -o $$@ $$<

$(OUTDIR)/lua_det_bindings_n_v$(3).o: lua_det_bindings_n.c \
        $(OUTDIR)/embed/blyt32_coroutine.h | $(OUTDIR)
	$$(CROSS) $$(CFLAGS_CART) -DLUA_STATE_VERSION=$(3) \
	    -I$(LUA_SRC) -I$(LIB_M) -I$(LIB_N) \
	    -c -o $$@ $$<

$(OUTDIR)/region_lua_cart_state_v$(2).o: $(CART_RUNTIME_N)/region_lua_cart_state.c | $(OUTDIR)
	$$(CROSS) $$(CFLAGS_CART) -DLUA_STATE_VERSION=$(2) -c -o $$@ $$<

$(OUTDIR)/region_lua_cart_state_v$(3).o: $(CART_RUNTIME_N)/region_lua_cart_state.c | $(OUTDIR)
	$$(CROSS) $$(CFLAGS_CART) -DLUA_STATE_VERSION=$(3) -c -o $$@ $$<

$(OUTDIR)/cart_workload_lua_edit_l$(1)_pre.o: cart_workload_lua_edit.c \
        $(OUTDIR)/embed/blyt32_coroutine.h | $(OUTDIR)
	$$(CROSS) $$(CFLAGS_CART) -DLUA_EDIT_NUM=$(1) -DLUA_EDIT_SIDE_POST=0 \
	    -I$(LUA_SRC) -I$(OUTDIR) -c -o $$@ $$<

$(OUTDIR)/cart_workload_lua_edit_l$(1)_post.o: cart_workload_lua_edit.c \
        $(OUTDIR)/embed/blyt32_coroutine.h | $(OUTDIR)
	$$(CROSS) $$(CFLAGS_CART) -DLUA_EDIT_NUM=$(1) -DLUA_EDIT_SIDE_POST=1 \
	    -I$(LUA_SRC) -I$(OUTDIR) -c -o $$@ $$<

$(OUTDIR)/regions_lua_edit_l$(1)_pre.o: regions_lua_edit.c | $(OUTDIR)
	$$(CROSS) $$(CFLAGS_CART) -DLUA_EDIT_NUM=$(1) -DLUA_EDIT_SIDE_POST=0 \
	    -DLUA_STATE_VERSION=$(2) -c -o $$@ $$<

$(OUTDIR)/regions_lua_edit_l$(1)_post.o: regions_lua_edit.c | $(OUTDIR)
	$$(CROSS) $$(CFLAGS_CART) -DLUA_EDIT_NUM=$(1) -DLUA_EDIT_SIDE_POST=1 \
	    -DLUA_STATE_VERSION=$(3) -c -o $$@ $$<

LUA_L$(1)_PRE_OBJS := \
    $(OUTDIR)/regions_lua_edit_l$(1)_pre.o \
    $(OUTDIR)/cart_workload_lua_edit_l$(1)_pre.o \
    $(OUTDIR)/lua_det_bindings_n_v$(2).o \
    $(OUTDIR)/lua_table_flatten.o \
    $(OUTDIR)/region_lua_cart_state_v$(2).o \
    $$(CART_RT_LUA_OBJS)

LUA_L$(1)_POST_OBJS := \
    $(OUTDIR)/regions_lua_edit_l$(1)_post.o \
    $(OUTDIR)/cart_workload_lua_edit_l$(1)_post.o \
    $(OUTDIR)/lua_det_bindings_n_v$(3).o \
    $(OUTDIR)/lua_table_flatten.o \
    $(OUTDIR)/region_lua_cart_state_v$(3).o \
    $$(CART_RT_LUA_OBJS)

$(OUTDIR)/det_lua_l$(1)_pre_save.elf: $(OUTDIR)/m_driver_save.o $$(LUA_L$(1)_PRE_OBJS)
	$$(CROSS) $$(CFLAGS_CART) $$(LDFLAGS) -o $$@ \
	    $(OUTDIR)/m_driver_save.o $$(LUA_L$(1)_PRE_OBJS) $$(LUA_LINK_EXTRAS)

$(OUTDIR)/det_lua_l$(1)_post_load.elf: $(OUTDIR)/m_driver_load.o $$(LUA_L$(1)_POST_OBJS)
	$$(CROSS) $$(CFLAGS_CART) $$(LDFLAGS) -o $$@ \
	    $(OUTDIR)/m_driver_load.o $$(LUA_L$(1)_POST_OBJS) $$(LUA_LINK_EXTRAS)

$(OUTDIR)/det_lua_l$(1)_post_full.elf: $(OUTDIR)/m_driver_full.o $$(LUA_L$(1)_POST_OBJS)
	$$(CROSS) $$(CFLAGS_CART) $$(LDFLAGS) -o $$@ \
	    $(OUTDIR)/m_driver_full.o $$(LUA_L$(1)_POST_OBJS) $$(LUA_LINK_EXTRAS)

ALL_ELFS += \
    $(OUTDIR)/det_lua_l$(1)_pre_save.elf \
    $(OUTDIR)/det_lua_l$(1)_post_load.elf \
    $(OUTDIR)/det_lua_l$(1)_post_full.elf
endef

# (edit_num, pre_lua_state_version, post_lua_state_version)
$(eval $(call define_lua_edit,1,0,0))
$(eval $(call define_lua_edit,2,0,2))
$(eval $(call define_lua_edit,3,2,3))
$(eval $(call define_lua_edit,4,3,4))
$(eval $(call define_lua_edit,5,4,4))

# ── Coroutine pre-save ELF (shared PRE for l6-l10) ────────────────────────────

$(OUTDIR)/embed/det_lua_cart_coroutine_pre.h: $(WORKLOADS_N)/det_lua_cart_coroutine_pre.lua | $(OUTDIR)/embed
	cd $(WORKLOADS_N) && xxd -i det_lua_cart_coroutine_pre.lua > $(abspath $@)

$(OUTDIR)/cart_workload_coroutine_pre.o: cart_workload_lua_coroutine.c \
        $(OUTDIR)/embed/blyt32_coroutine.h \
        $(OUTDIR)/embed/det_lua_cart_coroutine_pre.h | $(OUTDIR)
	$(CROSS) $(CFLAGS_CART) -DLUA_COROUTINE_PRE=1 \
	    -I$(LUA_SRC) -I$(OUTDIR) -c -o $@ $<

$(OUTDIR)/regions_coroutine_pre.o: regions_det_noop_lua.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_CART) -DLUA_STATE_VERSION=0 -c -o $@ $<

COROUTINE_PRE_LINK_OBJS := \
    $(OUTDIR)/regions_coroutine_pre.o \
    $(OUTDIR)/cart_workload_coroutine_pre.o \
    $(OUTDIR)/lua_det_bindings_n_v0.o \
    $(OUTDIR)/lua_table_flatten.o \
    $(OUTDIR)/region_lua_cart_state_v0.o \
    $(CART_RT_LUA_OBJS)

$(OUTDIR)/det_lua_coroutine_pre_save.elf: $(OUTDIR)/m_driver_save.o $(COROUTINE_PRE_LINK_OBJS)
	$(CROSS) $(CFLAGS_CART) $(LDFLAGS) -o $@ \
	    $(OUTDIR)/m_driver_save.o $(COROUTINE_PRE_LINK_OBJS) $(LUA_LINK_EXTRAS)

ALL_ELFS += $(OUTDIR)/det_lua_coroutine_pre_save.elf

# Coroutine POST ELFs (l6-l10) — one per post workload
define define_lua_coroutine_post
$(OUTDIR)/embed/det_lua_cart_coroutine_$(1)_post.h: $(WORKLOADS_N)/det_lua_cart_coroutine_$(1)_post.lua | $(OUTDIR)/embed
	cd $(WORKLOADS_N) && xxd -i det_lua_cart_coroutine_$(1)_post.lua > $(abspath $@)

$(OUTDIR)/cart_workload_coroutine_$(1)_post.o: \
        cart_workload_lua_coroutine.c \
        $(OUTDIR)/embed/blyt32_coroutine.h \
        $(OUTDIR)/embed/det_lua_cart_coroutine_$(1)_post.h | $(OUTDIR)
	$$(CROSS) $$(CFLAGS_CART) -DLUA_COROUTINE_EDIT_$(1)=1 \
	    -I$(LUA_SRC) -I$(OUTDIR) -c -o $$@ $$<

$(OUTDIR)/regions_coroutine_$(1)_post.o: regions_det_noop_lua.c | $(OUTDIR)
	$$(CROSS) $$(CFLAGS_CART) -DLUA_STATE_VERSION=0 -c -o $$@ $$<

COROUTINE_$(1)_POST_LINK_OBJS := \
    $(OUTDIR)/regions_coroutine_$(1)_post.o \
    $(OUTDIR)/cart_workload_coroutine_$(1)_post.o \
    $(OUTDIR)/lua_det_bindings_n_v0.o \
    $(OUTDIR)/lua_table_flatten.o \
    $(OUTDIR)/region_lua_cart_state_v0.o \
    $$(CART_RT_LUA_OBJS)

$(OUTDIR)/det_lua_$(1)_post_load.elf: $(OUTDIR)/m_driver_load.o $$(COROUTINE_$(1)_POST_LINK_OBJS)
	$$(CROSS) $$(CFLAGS_CART) $$(LDFLAGS) -o $$@ \
	    $(OUTDIR)/m_driver_load.o $$(COROUTINE_$(1)_POST_LINK_OBJS) $$(LUA_LINK_EXTRAS)

ALL_ELFS += $(OUTDIR)/det_lua_$(1)_post_load.elf
endef

$(eval $(call define_lua_coroutine_post,l6))
$(eval $(call define_lua_coroutine_post,l7))
$(eval $(call define_lua_coroutine_post,l8))
$(eval $(call define_lua_coroutine_post,l9))
$(eval $(call define_lua_coroutine_post,l10))

# ── targets ───────────────────────────────────────────────────────────────────

.PHONY: all clean
all: $(OUTDIR) $(ALL_ELFS)

clean:
	$(RM) -r $(OUTDIR)
