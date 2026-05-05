## Spike J — extends Spike I's libconsolelua.so with master_hook composition.
## libconsole.so is unchanged from spike-i; we copy the prebuilt binary from
## the spike-i image. Only libconsolelua.so is rebuilt.
##
## Compile-time variant via VARIANT make argument:
##   VARIANT=default                  (all flags on — production dev-mode)
##   VARIANT=master-off               (-DMASTER_OFF — no-hook baseline)
##   VARIANT=master-budget            (-DMASTER_BUDGET — Spike G parity)
##   VARIANT=master-budget-dap-idle   (-DMASTER_BUDGET_DAP_IDLE — headline)
##   VARIANT=master-throttle          (-DMASTER_THROTTLE — Spike G.3 parity)

CROSS   ?= riscv64-linux-gnu-gcc
ARCH    := -march=rv32imfc_zicsr -mabi=ilp32f
OUTDIR  ?= build
VARIANT ?= default

LUA_SRC := /spike-i/lua/src
SPIKE_I_LIB := /spike-i/lib
LIB_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

CFLAGS_BASE := $(ARCH) -O2 -fPIC \
               -ffreestanding -nostdlib -fno-stack-protector -fno-common \
               -Wno-implicit-fallthrough -Wno-stringop-overflow \
               -Wno-deprecated-declarations -Wno-array-bounds

CFLAGS_LUA := $(CFLAGS_BASE) \
              -I$(LIB_DIR) \
              -I$(SPIKE_I_LIB)/include \
              -I$(LUA_SRC)

VARIANT_FLAGS_default                := -DMASTER_BUDGET_DAP_IDLE
VARIANT_FLAGS_master-off             := -DMASTER_OFF
VARIANT_FLAGS_master-budget          := -DMASTER_BUDGET
VARIANT_FLAGS_master-budget-dap-idle := -DMASTER_BUDGET_DAP_IDLE
VARIANT_FLAGS_master-throttle        := -DMASTER_THROTTLE

VARIANT_DEF := $(VARIANT_FLAGS_$(VARIANT))

LUA_OMIT := lua.c luac.c linit.c \
            liolib.c loslib.c loadlib.c \
            lcorolib.c ldblib.c lutf8lib.c

LUA_ALL  := $(notdir $(wildcard $(LUA_SRC)/*.c))
LUA_SRCS := $(filter-out $(LUA_OMIT), $(LUA_ALL))
LUA_OBJS := $(patsubst %.c, $(OUTDIR)/lua/%.o, $(LUA_SRCS))

## Reuse spike-i's runtime objects as-is.
RT_OBJS  := $(OUTDIR)/lua_lib_runtime.o \
            $(OUTDIR)/lua_init_libs.o \
            $(OUTDIR)/softfloat_stubs.o

.PHONY: all clean

all: $(OUTDIR)/libconsolelua.so

$(OUTDIR):
	mkdir -p $@

$(OUTDIR)/lua: | $(OUTDIR)
	mkdir -p $@

$(OUTDIR)/lua/%.o: $(LUA_SRC)/%.c | $(OUTDIR)/lua
	$(CROSS) $(CFLAGS_LUA) -c -o $@ $<

$(OUTDIR)/lua_lib_runtime.o: $(LIB_DIR)/lua_lib_runtime.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_LUA) -c -o $@ $<

$(OUTDIR)/lua_init_libs.o: $(LIB_DIR)/lua_init_libs.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_LUA) -c -o $@ $<

$(OUTDIR)/softfloat_stubs.o: $(LIB_DIR)/softfloat_stubs.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_LUA) -c -o $@ $<

$(OUTDIR)/master_hook.o: $(LIB_DIR)/master_hook.c $(LIB_DIR)/master_hook.h | $(OUTDIR)
	$(CROSS) $(CFLAGS_LUA) $(VARIANT_DEF) -c -o $@ $<

$(OUTDIR)/libconsolelua_rv32.o: $(LIB_DIR)/libconsolelua_rv32.c $(LIB_DIR)/master_hook.h | $(OUTDIR)
	$(CROSS) $(CFLAGS_LUA) $(VARIANT_DEF) -c -o $@ $<

$(OUTDIR)/libconsolelua.so: $(OUTDIR)/libconsolelua_rv32.o $(OUTDIR)/master_hook.o \
                             $(LUA_OBJS) $(RT_OBJS)
	$(CROSS) -shared -fPIC -nostdlib \
	    -march=rv32imfc_zicsr -mabi=ilp32f \
	    -Wl,-soname,libconsolelua.so \
	    -Wl,--unresolved-symbols=ignore-all \
	    -o $@ $^

clean:
	$(RM) -r $(OUTDIR)
