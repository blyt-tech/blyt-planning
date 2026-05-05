## Spike K — build the cross-host save-state cart ELFs.
##
## Stage 1 floor case (only Stage 1 is wired in this commit):
##   whetstone_save.elf        captures the buffer at frame N (default 15)
##   whetstone_load.elf        consumes a saved buffer, continues from N+1
##
## Each ELF links the spike-K cart_runtime + spike-D / spike-B Lua and
## softfloat infrastructure, exactly as Spike D's Makefile does.
##
## Layout assumed (relative to this Makefile, when run inside Docker):
##   /spike-a/                       spike-a artefacts
##   /spike-b/                       spike-b artefacts (Lua, runtime, crt0, glue)
##   /spike-d/                       spike-d artefacts (cart_runtime, libm-rv32.a, workloads)
##   /spike-k/cart_runtime/          save_state, runtime_tracked, region_frame_state, save_io
##   /spike-k/lib/musl-libm/build/   reuses spike-d's libm-rv32.a (the Dockerfile copies it)
##   /spike-k/workloads/             det_doom_tick.lua, det_entity_update.lua (symlinks → spike-d)
##   /spike-k/ports/rv32emu/         this file + whetstone_save/load.c + regions_*.c

CROSS  ?= riscv64-linux-gnu-gcc
AR     ?= riscv64-linux-gnu-ar
ARCH   := -march=rv32imfc_zicsr -mabi=ilp32f
OUTDIR ?= build

SPIKE_A_DIR ?= /spike-a
SPIKE_B_DIR ?= /spike-b
SPIKE_D_DIR ?= /spike-d
SPIKE_K_DIR ?= /spike-k

SOFTFLOAT_A   ?= $(SPIKE_A_DIR)/softfloat-build/softfloat.a
SOFTFLOAT_INC ?= $(SPIKE_A_DIR)/berkeley-softfloat-3/source/include

SPIKE_B_PORT := $(SPIKE_B_DIR)/ports/rv32emu
SPIKE_B_INC  := $(SPIKE_B_PORT)/include
SPIKE_K_INC  := $(SPIKE_K_DIR)/ports/rv32emu/include

CART_RUNTIME_K := $(SPIKE_K_DIR)/cart_runtime
LIBM_RV32_A    := $(SPIKE_K_DIR)/lib/musl-libm/build/libm-rv32.a

# ── flags (mirror spike-d) ───────────────────────────────────────────────────
CFLAGS_BASE := $(ARCH) -O2 -g \
               -ffreestanding -nostdlib -fno-stack-protector -fno-common \
               -static -no-pie -fno-pie \
               -fno-fast-math -ffp-contract=off -frounding-math

CFLAGS_RT   := $(CFLAGS_BASE) -DHAVE_LIBM \
               -I$(SPIKE_K_INC) \
               -I$(SPIKE_B_INC) \
               -I$(SOFTFLOAT_INC)

CFLAGS_CART := $(CFLAGS_BASE) -DHAVE_LIBM \
               -I$(SPIKE_K_INC) \
               -I$(SPIKE_B_INC) \
               -I$(CART_RUNTIME_K) \
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

# ── spike-K cart_runtime ─────────────────────────────────────────────────────

$(OUTDIR)/digest.o: $(CART_RUNTIME_K)/digest.c $(CART_RUNTIME_K)/digest.h | $(OUTDIR)
	$(CROSS) $(CFLAGS_RT) -I$(CART_RUNTIME_K) -c -o $@ $<

$(OUTDIR)/pcg32.o: $(CART_RUNTIME_K)/pcg32.c $(CART_RUNTIME_K)/pcg32.h | $(OUTDIR)
	$(CROSS) $(CFLAGS_RT) -I$(CART_RUNTIME_K) -c -o $@ $<

$(OUTDIR)/save_state.o: $(CART_RUNTIME_K)/save_state.c $(CART_RUNTIME_K)/save_state.h | $(OUTDIR)
	$(CROSS) $(CFLAGS_RT) -I$(CART_RUNTIME_K) -c -o $@ $<

$(OUTDIR)/runtime_tracked.o: $(CART_RUNTIME_K)/runtime_tracked.c $(CART_RUNTIME_K)/runtime_tracked.h | $(OUTDIR)
	$(CROSS) $(CFLAGS_RT) -I$(CART_RUNTIME_K) -c -o $@ $<

$(OUTDIR)/region_frame_state.o: $(CART_RUNTIME_K)/region_frame_state.c $(CART_RUNTIME_K)/region_frame_state.h | $(OUTDIR)
	$(CROSS) $(CFLAGS_RT) -I$(CART_RUNTIME_K) -c -o $@ $<

$(OUTDIR)/region_screen_shake.o: $(CART_RUNTIME_K)/region_screen_shake.c $(CART_RUNTIME_K)/region_screen_shake.h | $(OUTDIR)
	$(CROSS) $(CFLAGS_RT) -I$(CART_RUNTIME_K) -c -o $@ $<

$(OUTDIR)/region_voice_end_queue.o: $(CART_RUNTIME_K)/region_voice_end_queue.c $(CART_RUNTIME_K)/region_voice_end_queue.h | $(OUTDIR)
	$(CROSS) $(CFLAGS_RT) -I$(CART_RUNTIME_K) -c -o $@ $<

$(OUTDIR)/region_coroutine_save_blob.o: $(CART_RUNTIME_K)/region_coroutine_save_blob.c $(CART_RUNTIME_K)/region_coroutine_save_blob.h | $(OUTDIR)
	$(CROSS) $(CFLAGS_RT) -I$(CART_RUNTIME_K) -c -o $@ $<

# Synthetic mixer lives under spike-K/lib (not cart_runtime).
$(OUTDIR)/synthetic_mixer.o: $(SPIKE_K_DIR)/lib/synthetic_mixer.c $(SPIKE_K_DIR)/lib/synthetic_mixer.h | $(OUTDIR)
	$(CROSS) $(CFLAGS_RT) -I$(CART_RUNTIME_K) -c -o $@ $<

$(OUTDIR)/cart_state_whetstone.o: $(CART_RUNTIME_K)/cart_state_whetstone.c $(CART_RUNTIME_K)/cart_state_whetstone.h | $(OUTDIR)
	$(CROSS) $(CFLAGS_RT) -I$(CART_RUNTIME_K) -c -o $@ $<

$(OUTDIR)/save_io.o: $(CART_RUNTIME_K)/save_io.c $(CART_RUNTIME_K)/save_io.h | $(OUTDIR)
	$(CROSS) $(CFLAGS_RT) -I$(CART_RUNTIME_K) -c -o $@ $<

CART_RT_OBJS := \
    $(OUTDIR)/digest.o \
    $(OUTDIR)/pcg32.o \
    $(OUTDIR)/save_state.o \
    $(OUTDIR)/runtime_tracked.o \
    $(OUTDIR)/region_frame_state.o \
    $(OUTDIR)/save_io.o

# Per-cart cart_state region objects.  Each cart that declares a
# cart_state_t links its descriptor explicitly (whetstone here; later
# stages add det_doom_tick, det_entity_update, ...).
WHETSTONE_CART_STATE_OBJ := $(OUTDIR)/cart_state_whetstone.o

# ── whetstone variants ───────────────────────────────────────────────────────

$(OUTDIR)/whetstone_save.o: whetstone_save.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_CART) -c -o $@ $<

$(OUTDIR)/whetstone_load.o: whetstone_load.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_CART) -c -o $@ $<

$(OUTDIR)/regions_whetstone.o: regions_whetstone.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_CART) -c -o $@ $<

WHETSTONE_SAVE_ELF := $(OUTDIR)/whetstone_save.elf
WHETSTONE_LOAD_ELF := $(OUTDIR)/whetstone_load.elf

$(WHETSTONE_SAVE_ELF): \
		$(OUTDIR)/whetstone_save.o \
		$(OUTDIR)/regions_whetstone.o \
		$(WHETSTONE_CART_STATE_OBJ) \
		$(CART_RT_OBJS) \
		$(OUTDIR)/lua_runtime.o \
		$(OUTDIR)/softfloat-glue.o \
		$(OUTDIR)/crt0.o \
		$(LIBM_RV32_A) \
		$(SOFTFLOAT_A)
	$(CROSS) $(CFLAGS_RT) $(LDFLAGS) -o $@ \
	    $(OUTDIR)/whetstone_save.o \
	    $(OUTDIR)/regions_whetstone.o \
	    $(WHETSTONE_CART_STATE_OBJ) \
	    $(CART_RT_OBJS) \
	    $(OUTDIR)/lua_runtime.o \
	    $(OUTDIR)/softfloat-glue.o \
	    $(OUTDIR)/crt0.o \
	    $(LIBM_RV32_A) \
	    $(SOFTFLOAT_A)

$(WHETSTONE_LOAD_ELF): \
		$(OUTDIR)/whetstone_load.o \
		$(OUTDIR)/regions_whetstone.o \
		$(WHETSTONE_CART_STATE_OBJ) \
		$(CART_RT_OBJS) \
		$(OUTDIR)/lua_runtime.o \
		$(OUTDIR)/softfloat-glue.o \
		$(OUTDIR)/crt0.o \
		$(LIBM_RV32_A) \
		$(SOFTFLOAT_A)
	$(CROSS) $(CFLAGS_RT) $(LDFLAGS) -o $@ \
	    $(OUTDIR)/whetstone_load.o \
	    $(OUTDIR)/regions_whetstone.o \
	    $(WHETSTONE_CART_STATE_OBJ) \
	    $(CART_RT_OBJS) \
	    $(OUTDIR)/lua_runtime.o \
	    $(OUTDIR)/softfloat-glue.o \
	    $(OUTDIR)/crt0.o \
	    $(LIBM_RV32_A) \
	    $(SOFTFLOAT_A)

$(OUTDIR)/corruption_tests.o: corruption_tests.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_CART) -c -o $@ $<

CORRUPTION_TESTS_ELF := $(OUTDIR)/corruption_tests.elf

$(CORRUPTION_TESTS_ELF): \
		$(OUTDIR)/corruption_tests.o \
		$(OUTDIR)/regions_whetstone.o \
		$(WHETSTONE_CART_STATE_OBJ) \
		$(CART_RT_OBJS) \
		$(OUTDIR)/lua_runtime.o \
		$(OUTDIR)/softfloat-glue.o \
		$(OUTDIR)/crt0.o \
		$(LIBM_RV32_A) \
		$(SOFTFLOAT_A)
	$(CROSS) $(CFLAGS_RT) $(LDFLAGS) -o $@ \
	    $(OUTDIR)/corruption_tests.o \
	    $(OUTDIR)/regions_whetstone.o \
	    $(WHETSTONE_CART_STATE_OBJ) \
	    $(CART_RT_OBJS) \
	    $(OUTDIR)/lua_runtime.o \
	    $(OUTDIR)/softfloat-glue.o \
	    $(OUTDIR)/crt0.o \
	    $(LIBM_RV32_A) \
	    $(SOFTFLOAT_A)

# ── det_shake variants (Stage 5) ─────────────────────────────────────────────

$(OUTDIR)/det_shake_save.o: det_shake_save.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_CART) -c -o $@ $<

$(OUTDIR)/det_shake_load.o: det_shake_load.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_CART) -c -o $@ $<

$(OUTDIR)/det_shake_full.o: det_shake_full.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_CART) -c -o $@ $<

$(OUTDIR)/regions_det_shake.o: regions_det_shake.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_CART) -c -o $@ $<

DET_SHAKE_RT_OBJS := \
    $(OUTDIR)/digest.o \
    $(OUTDIR)/pcg32.o \
    $(OUTDIR)/save_state.o \
    $(OUTDIR)/runtime_tracked.o \
    $(OUTDIR)/region_frame_state.o \
    $(OUTDIR)/region_screen_shake.o \
    $(OUTDIR)/save_io.o

DET_SHAKE_SAVE_ELF := $(OUTDIR)/det_shake_save.elf
DET_SHAKE_LOAD_ELF := $(OUTDIR)/det_shake_load.elf
DET_SHAKE_FULL_ELF := $(OUTDIR)/det_shake_full.elf

define LINK_DET_SHAKE
$(1): \
		$(2) \
		$(OUTDIR)/regions_det_shake.o \
		$(DET_SHAKE_RT_OBJS) \
		$(OUTDIR)/lua_runtime.o \
		$(OUTDIR)/softfloat-glue.o \
		$(OUTDIR)/crt0.o \
		$(LIBM_RV32_A) \
		$(SOFTFLOAT_A)
	$$(CROSS) $$(CFLAGS_RT) $$(LDFLAGS) -o $$@ \
	    $(2) \
	    $(OUTDIR)/regions_det_shake.o \
	    $(DET_SHAKE_RT_OBJS) \
	    $(OUTDIR)/lua_runtime.o \
	    $(OUTDIR)/softfloat-glue.o \
	    $(OUTDIR)/crt0.o \
	    $(LIBM_RV32_A) \
	    $(SOFTFLOAT_A)
endef
$(eval $(call LINK_DET_SHAKE,$(DET_SHAKE_SAVE_ELF),$(OUTDIR)/det_shake_save.o))
$(eval $(call LINK_DET_SHAKE,$(DET_SHAKE_LOAD_ELF),$(OUTDIR)/det_shake_load.o))
$(eval $(call LINK_DET_SHAKE,$(DET_SHAKE_FULL_ELF),$(OUTDIR)/det_shake_full.o))

# ── det_audio_branch variants (Stage 3) ──────────────────────────────────────

$(OUTDIR)/det_audio_branch_save.o: det_audio_branch_save.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_CART) -I$(SPIKE_K_DIR) -c -o $@ $<

$(OUTDIR)/det_audio_branch_load.o: det_audio_branch_load.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_CART) -I$(SPIKE_K_DIR) -c -o $@ $<

$(OUTDIR)/det_audio_branch_full.o: det_audio_branch_full.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_CART) -I$(SPIKE_K_DIR) -c -o $@ $<

$(OUTDIR)/regions_det_audio_branch.o: regions_det_audio_branch.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_CART) -c -o $@ $<

DET_AUDIO_BRANCH_RT_OBJS := \
    $(OUTDIR)/digest.o \
    $(OUTDIR)/pcg32.o \
    $(OUTDIR)/save_state.o \
    $(OUTDIR)/runtime_tracked.o \
    $(OUTDIR)/region_frame_state.o \
    $(OUTDIR)/region_voice_end_queue.o \
    $(OUTDIR)/synthetic_mixer.o \
    $(OUTDIR)/save_io.o

DET_AUDIO_BRANCH_SAVE_ELF := $(OUTDIR)/det_audio_branch_save.elf
DET_AUDIO_BRANCH_LOAD_ELF := $(OUTDIR)/det_audio_branch_load.elf
DET_AUDIO_BRANCH_FULL_ELF := $(OUTDIR)/det_audio_branch_full.elf

define LINK_DET_AUDIO_BRANCH
$(1): \
		$(2) \
		$(OUTDIR)/regions_det_audio_branch.o \
		$(DET_AUDIO_BRANCH_RT_OBJS) \
		$(OUTDIR)/lua_runtime.o \
		$(OUTDIR)/softfloat-glue.o \
		$(OUTDIR)/crt0.o \
		$(LIBM_RV32_A) \
		$(SOFTFLOAT_A)
	$$(CROSS) $$(CFLAGS_RT) $$(LDFLAGS) -o $$@ \
	    $(2) \
	    $(OUTDIR)/regions_det_audio_branch.o \
	    $(DET_AUDIO_BRANCH_RT_OBJS) \
	    $(OUTDIR)/lua_runtime.o \
	    $(OUTDIR)/softfloat-glue.o \
	    $(OUTDIR)/crt0.o \
	    $(LIBM_RV32_A) \
	    $(SOFTFLOAT_A)
endef
$(eval $(call LINK_DET_AUDIO_BRANCH,$(DET_AUDIO_BRANCH_SAVE_ELF),$(OUTDIR)/det_audio_branch_save.o))
$(eval $(call LINK_DET_AUDIO_BRANCH,$(DET_AUDIO_BRANCH_LOAD_ELF),$(OUTDIR)/det_audio_branch_load.o))
$(eval $(call LINK_DET_AUDIO_BRANCH,$(DET_AUDIO_BRANCH_FULL_ELF),$(OUTDIR)/det_audio_branch_full.o))

# ── det_cutscene variants (Stage 4) ──────────────────────────────────────────

$(OUTDIR)/det_cutscene_save.o: det_cutscene_save.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_CART) -c -o $@ $<

$(OUTDIR)/det_cutscene_load.o: det_cutscene_load.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_CART) -c -o $@ $<

$(OUTDIR)/det_cutscene_full.o: det_cutscene_full.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_CART) -c -o $@ $<

$(OUTDIR)/regions_det_cutscene.o: regions_det_cutscene.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_CART) -c -o $@ $<

DET_CUTSCENE_RT_OBJS := \
    $(OUTDIR)/digest.o \
    $(OUTDIR)/pcg32.o \
    $(OUTDIR)/save_state.o \
    $(OUTDIR)/runtime_tracked.o \
    $(OUTDIR)/region_frame_state.o \
    $(OUTDIR)/region_coroutine_save_blob.o \
    $(OUTDIR)/save_io.o

DET_CUTSCENE_SAVE_ELF := $(OUTDIR)/det_cutscene_save.elf
DET_CUTSCENE_LOAD_ELF := $(OUTDIR)/det_cutscene_load.elf
DET_CUTSCENE_FULL_ELF := $(OUTDIR)/det_cutscene_full.elf

define LINK_DET_CUTSCENE
$(1): \
		$(2) \
		$(OUTDIR)/regions_det_cutscene.o \
		$(DET_CUTSCENE_RT_OBJS) \
		$(OUTDIR)/lua_runtime.o \
		$(OUTDIR)/softfloat-glue.o \
		$(OUTDIR)/crt0.o \
		$(LIBM_RV32_A) \
		$(SOFTFLOAT_A)
	$$(CROSS) $$(CFLAGS_RT) $$(LDFLAGS) -o $$@ \
	    $(2) \
	    $(OUTDIR)/regions_det_cutscene.o \
	    $(DET_CUTSCENE_RT_OBJS) \
	    $(OUTDIR)/lua_runtime.o \
	    $(OUTDIR)/softfloat-glue.o \
	    $(OUTDIR)/crt0.o \
	    $(LIBM_RV32_A) \
	    $(SOFTFLOAT_A)
endef
$(eval $(call LINK_DET_CUTSCENE,$(DET_CUTSCENE_SAVE_ELF),$(OUTDIR)/det_cutscene_save.o))
$(eval $(call LINK_DET_CUTSCENE,$(DET_CUTSCENE_LOAD_ELF),$(OUTDIR)/det_cutscene_load.o))
$(eval $(call LINK_DET_CUTSCENE,$(DET_CUTSCENE_FULL_ELF),$(OUTDIR)/det_cutscene_full.o))

ALL_ELFS := $(WHETSTONE_SAVE_ELF) $(WHETSTONE_LOAD_ELF) $(CORRUPTION_TESTS_ELF) \
            $(DET_SHAKE_SAVE_ELF) $(DET_SHAKE_LOAD_ELF) $(DET_SHAKE_FULL_ELF) \
            $(DET_AUDIO_BRANCH_SAVE_ELF) $(DET_AUDIO_BRANCH_LOAD_ELF) $(DET_AUDIO_BRANCH_FULL_ELF) \
            $(DET_CUTSCENE_SAVE_ELF) $(DET_CUTSCENE_LOAD_ELF) $(DET_CUTSCENE_FULL_ELF)

.PHONY: all clean
all: $(OUTDIR) $(ALL_ELFS)

clean:
	$(RM) -r $(OUTDIR)
