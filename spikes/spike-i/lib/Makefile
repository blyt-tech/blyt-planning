## Build libconsole.so and libconsolelua.so — RV32IMFC shared objects for spike-i

CROSS   ?= riscv64-linux-gnu-gcc
ARCH    := -march=rv32imfc_zicsr -mabi=ilp32f
OUTDIR  ?= build

LUA_SRC := /spike-i/lua/src

LIB_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

CFLAGS_BASE := $(ARCH) -O2 -fPIC \
               -ffreestanding -nostdlib -fno-stack-protector -fno-common \
               -Wno-implicit-fallthrough -Wno-stringop-overflow \
               -Wno-deprecated-declarations -Wno-array-bounds

CFLAGS_LUA := $(CFLAGS_BASE) \
              -I$(LIB_DIR)/include \
              -I$(LUA_SRC)

CFLAGS_LIB := $(CFLAGS_BASE) \
              -I$(LIB_DIR)/include \
              -I$(LUA_SRC)

LUA_OMIT := lua.c luac.c linit.c \
            liolib.c loslib.c loadlib.c \
            lcorolib.c ldblib.c lutf8lib.c

LUA_ALL  := $(notdir $(wildcard $(LUA_SRC)/*.c))
LUA_SRCS := $(filter-out $(LUA_OMIT), $(LUA_ALL))
LUA_OBJS := $(patsubst %.c, $(OUTDIR)/lua/%.o, $(LUA_SRCS))

RT_OBJS  := $(OUTDIR)/lua_lib_runtime.o \
            $(OUTDIR)/lua_init_libs.o \
            $(OUTDIR)/softfloat_stubs.o

.PHONY: all clean

all: $(OUTDIR)/libconsole.so $(OUTDIR)/libconsolelua.so

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
	$(CROSS) $(CFLAGS_LIB) -c -o $@ $<

$(OUTDIR)/libconsole_rv32.o: $(LIB_DIR)/libconsole_rv32.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_LIB) -c -o $@ $<

$(OUTDIR)/libconsolelua_rv32.o: $(LIB_DIR)/libconsolelua_rv32.c | $(OUTDIR)
	$(CROSS) $(CFLAGS_LUA) -c -o $@ $<

$(OUTDIR)/libconsole.so: $(OUTDIR)/libconsole_rv32.o $(OUTDIR)/lua_lib_runtime.o $(OUTDIR)/softfloat_stubs.o
	$(CROSS) -shared -fPIC -nostdlib \
	    -march=rv32imfc_zicsr -mabi=ilp32f \
	    -Wl,-soname,libconsole.so \
	    -Wl,--unresolved-symbols=ignore-all \
	    -o $@ $^

$(OUTDIR)/libconsolelua.so: $(OUTDIR)/libconsolelua_rv32.o \
                             $(LUA_OBJS) $(RT_OBJS)
	$(CROSS) -shared -fPIC -nostdlib \
	    -march=rv32imfc_zicsr -mabi=ilp32f \
	    -Wl,-soname,libconsolelua.so \
	    -Wl,--unresolved-symbols=ignore-all \
	    -o $@ $^

clean:
	$(RM) -r $(OUTDIR)
