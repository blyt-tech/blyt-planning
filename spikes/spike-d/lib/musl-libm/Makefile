## lib/musl-libm/Makefile — build libm-rv32.a from a vendored musl 1.2.5 tree.
##
## Inputs:
##   MUSL_SRC_DIR    path to extracted musl-1.2.5 source tree (must contain
##                   src/math/ and src/internal/).  The Dockerfile downloads
##                   and extracts musl, then sets this variable.
##   PORT_INCLUDE    spike-d's include shim (the override math.h, libm.h
##                   shim, etc.).  Must come before the cross toolchain's
##                   own headers.
##
## Outputs:
##   libm-rv32.a     static archive containing every musl object listed
##                   in MUSL_OBJS.
##
## The flag set is identical to what spike-b's port uses for lua_runtime.o,
## with the addition of -fno-fast-math / -ffp-contract=off / -frounding-math
## per ADR-0007's deterministic-FP preamble.  Any change here is a change
## to the cart's bit output across all hosts, so flag drift here is the
## first thing to suspect if the digest stream changes between builds.

CROSS    ?= riscv64-linux-gnu-gcc
AR       ?= riscv64-linux-gnu-ar
ARCH     := -march=rv32imfc_zicsr -mabi=ilp32f
OUTDIR   ?= build

MUSL_SRC_DIR    ?=
SHIM_INCLUDE    ?= ../../lib/musl-libm/include
PORT_INCLUDE    ?= ../../ports/rv32emu/include
SPIKE_B_INCLUDE ?= ../../../spike-b/ports/rv32emu/include

ifeq ($(MUSL_SRC_DIR),)
$(error MUSL_SRC_DIR is required — point at extracted musl-1.2.5 tree)
endif

CFLAGS_MUSL := $(ARCH) -O2 -g \
               -ffreestanding -nostdlib -fno-stack-protector -fno-common \
               -static -no-pie -fno-pie \
               -fno-fast-math -ffp-contract=off -frounding-math \
               -I$(PORT_INCLUDE) \
               -I$(SPIKE_B_INCLUDE) \
               -I$(SHIM_INCLUDE) \
               -I$(MUSL_SRC_DIR)/src/math \
               -Wno-unused-but-set-variable \
               -Wno-unused-variable \
               -Wno-implicit-function-declaration \
               -Wno-attributes

# ── files we vendor ──────────────────────────────────────────────────────────
# Float-precision transcendentals + atan2 + the helper kernels they call.
# We deliberately do NOT vendor sqrtf — the cart-runtime keeps using
# `fsqrt.s` for sqrt because (a) it is already deterministic via SoftFloat
# and (b) keeping sqrtf out of musl makes the spike's "drop transcendentals
# but keep sqrt" fallback (PLAN.md, fallback step 1) trivial: stop linking
# this archive and the cart loses everything *except* sqrtf.
MUSL_FILES := \
    sinf.c cosf.c tanf.c sincosf.c \
    atanf.c atan2f.c \
    expf.c exp2f.c logf.c log2f.c powf.c \
    floorf.c ceilf.c fabsf.c \
    __rem_pio2f.c __rem_pio2_large.c \
    __sindf.c __cosdf.c __tandf.c \
    __math_invalidf.c __math_xflowf.c __math_uflowf.c \
    __math_oflowf.c __math_divzerof.c \
    exp2f_data.c logf_data.c log2f_data.c powf_data.c

MUSL_OBJS := $(patsubst %.c, $(OUTDIR)/%.o, $(MUSL_FILES))

# ── build ────────────────────────────────────────────────────────────────────

$(OUTDIR):
	mkdir -p $@

$(OUTDIR)/%.o: $(MUSL_SRC_DIR)/src/math/%.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_MUSL) -c -o $@ $<

$(OUTDIR)/libm-rv32.a: $(MUSL_OBJS)
	$(AR) crs $@ $(MUSL_OBJS)

.PHONY: all clean
all: $(OUTDIR)/libm-rv32.a

clean:
	$(RM) -r $(OUTDIR)
