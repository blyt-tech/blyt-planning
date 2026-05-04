# Spike I — rv32emu/mk/wasm.mk override.
#
# Embeds the spike-i C-cart artefacts (cart_a, cart_b, libconsole.so) into
# the in-WASM filesystem at the absolute paths the patched fc32_dynload
# expects, so `rv32emu -L /spike-i/lib /spike-i/cases/case_X/cart_X` opens
# them via fopen() inside the WASM environment.
#
# Lua carts (cases c, d) are handled by Stage 5 — Lua-direct WASM — and are
# NOT embedded here; rv32emu-WASM is for the C carts only.

ifndef _MK_WASM_INCLUDED
_MK_WASM_INCLUDED := 1

CFLAGS_emcc ?=
deps_emcc :=
ASSETS := assets/wasm
WEB_HTML_RESOURCES := $(ASSETS)/html
WEB_JS_RESOURCES := $(ASSETS)/js
EXPORTED_FUNCS := _main,_indirect_rv_halt,_get_input_buf,_get_input_buf_cap,_set_input_buf_size

WEB_FILES := $(BIN).js $(BIN).wasm $(BIN).worker.js

ifeq ("$(CC_IS_EMCC)", "1")

BIN := $(BIN).js

CFLAGS += -mtail-call

# Spike-I embed list. Files are staged into $(OUT)/embed/ by the spike-i
# Makefile before build, then mounted into the in-WASM filesystem at the
# absolute paths fc32_dynload reads them from at runtime.
SPIKE_I_EMBED_FLAGS := \
    --embed-file $(OUT)/embed/libconsole.so@/spike-i/lib/libconsole.so \
    --embed-file $(OUT)/embed/libconsolelua.so@/spike-i/lib/libconsolelua.so \
    --embed-file $(OUT)/embed/cart_a@/spike-i/cases/case_a/cart_a \
    --embed-file $(OUT)/embed/cart_b@/spike-i/cases/case_b/cart_b \
    --embed-file $(OUT)/embed/cart_c@/spike-i/cases/case_c/cart_c \
    --embed-file $(OUT)/embed/cart_d@/spike-i/cases/case_d/cart_d

## MEM_SIZE: spike-i's multi-dynload loads libconsole.so at guest address
## 0x08000000 (128 MB). Libraries plus their PT_LOAD extents need addressable
## memory above that, so guest RAM is 256 MB (vs spike-e's 32 MB which only
## had to hold a single statically-linked cart). INITIAL_MEMORY bumped to
## match (rv32emu mallocs the guest memory_t array up-front).
CFLAGS_emcc += -sINITIAL_MEMORY=512MB \
               -sALLOW_MEMORY_GROWTH \
               -s"EXPORTED_FUNCTIONS=$(EXPORTED_FUNCS)" \
               -sSTACK_SIZE=4MB \
               $(SPIKE_I_EMBED_FLAGS) \
               -DMEM_SIZE=0x10000000 \
               -DCYCLE_PER_STEP=2000000 \
               -O3 \
               -w \
               --pre-js $(WEB_JS_RESOURCES)/user-pre.js

# mimalloc support detection (mirrors upstream)
MIMALLOC_SUPPORT_SINCE_MAJOR := 3
MIMALLOC_SUPPORT_SINCE_MINOR := 1
MIMALLOC_SUPPORT_SINCE_PATCH := 50
ifeq ($(call version_gte,$(EMCC_MAJOR),$(EMCC_MINOR),$(EMCC_PATCH),$(MIMALLOC_SUPPORT_SINCE_MAJOR),$(MIMALLOC_SUPPORT_SINCE_MINOR),$(MIMALLOC_SUPPORT_SINCE_PATCH)), 1)
    CFLAGS_emcc += -sMALLOC=mimalloc
endif

deps_emcc += $(OUT)/embed/libconsole.so $(OUT)/embed/libconsolelua.so \
             $(OUT)/embed/cart_a $(OUT)/embed/cart_b \
             $(OUT)/embed/cart_c $(OUT)/embed/cart_d

# spike-e wasm.mk used to compute Chrome/Firefox versions for TCO support
# detection, which is informational only; harmless to omit here.

endif

endif # _MK_WASM_INCLUDED
