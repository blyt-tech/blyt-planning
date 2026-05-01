# Spike E variant of rv32emu/mk/wasm.mk.
#
# Differences from upstream:
#   * No DOOM / Quake / Timidity / smolnes / fibonacci / ... embeds.
#   * No `artifact` prereq — we do not need the prebuilt blobs.
#   * Embeds the Spike B Lua-cart ELFs (lua_cart_*.elf) plus doom_tick_c.elf
#     instead.  Each is bundled at /<basename>.elf in the in-WASM filesystem.
#   * INITIAL_MEMORY trimmed from 2 GB to 256 MB (mid-range Android browsers
#     refuse 2 GB allocations; Lua carts only need a few MB of guest RAM).
#   * MEM_SIZE (rv32 guest RAM) trimmed from 512 MB to 32 MB — lua_runtime.c
#     uses an 8 MB heap, the cart ELFs total well under 2 MB each.
#
# This file is copied into rv32emu/mk/wasm.mk by the spike-e Makefile before
# the WASM build runs.

ifndef _MK_WASM_INCLUDED
_MK_WASM_INCLUDED := 1

CFLAGS_emcc ?=
deps_emcc :=
ASSETS := assets/wasm
WEB_HTML_RESOURCES := $(ASSETS)/html
WEB_JS_RESOURCES := $(ASSETS)/js
EXPORTED_FUNCS := _main,_indirect_rv_halt,_get_input_buf,_get_input_buf_cap,_set_input_buf_size
DEMO_DIR := demo

WEB_FILES := $(BIN).js \
             $(BIN).wasm \
             $(BIN).worker.js \
             $(OUT)/elf_list.js

ifeq ("$(CC_IS_EMCC)", "1")

BIN := $(BIN).js

# Tail-call optimization
CFLAGS += -mtail-call

# Spike-E embed list.  The cart ELFs are staged into $(OUT)/ by the spike-e
# Makefile before build, so here they appear as build/lua_cart_*.elf.
SPIKE_E_CARTS := \
    lua_cart_binarytrees.elf \
    lua_cart_doom_tick.elf \
    lua_cart_doom_tick_gc.elf \
    lua_cart_entity_update.elf \
    lua_cart_fannkuch.elf \
    lua_cart_fasta.elf \
    lua_cart_mandelbrot.elf \
    lua_cart_nbody.elf \
    lua_cart_spectral-norm.elf \
    doom_tick_c.elf

SPIKE_E_EMBED_FLAGS := $(foreach c,$(SPIKE_E_CARTS),--embed-file $(OUT)/$(c)@/$(c))

# Emscripten build flags.
# We deliberately drop -sPTHREAD_POOL_SIZE=... from the upstream wasm.mk:
# our config has no SDL, no JIT, no T2C, so the build never references pthread
# functions.  Dropping pthreads removes the SharedArrayBuffer requirement,
# which means the page no longer needs COOP+COEP cross-origin isolation —
# critical for the mobile test, where the COI service-worker shim does a
# noticeable reload-on-first-load that Puppeteer-style automation chokes on.
CFLAGS_emcc += -sINITIAL_MEMORY=256MB \
               -sALLOW_MEMORY_GROWTH \
               -s"EXPORTED_FUNCTIONS=$(EXPORTED_FUNCS)" \
               -sSTACK_SIZE=4MB \
               $(SPIKE_E_EMBED_FLAGS) \
               -DMEM_SIZE=0x2000000 \
               -DCYCLE_PER_STEP=2000000 \
               -O3 \
               -w \
               --pre-js $(WEB_JS_RESOURCES)/user-pre.js

# mimalloc support detection (kept to match upstream; harmless if disabled)
MIMALLOC_SUPPORT_SINCE_MAJOR := 3
MIMALLOC_SUPPORT_SINCE_MINOR := 1
MIMALLOC_SUPPORT_SINCE_PATCH := 50
ifeq ($(call version_gte,$(EMCC_MAJOR),$(EMCC_MINOR),$(EMCC_PATCH),$(MIMALLOC_SUPPORT_SINCE_MAJOR),$(MIMALLOC_SUPPORT_SINCE_MINOR),$(MIMALLOC_SUPPORT_SINCE_PATCH)), 1)
    CFLAGS_emcc += -sMALLOC=mimalloc
endif

# ELF list generator — emits a JS array of the embedded cart names.
$(OUT)/elf_list.js: $(SPIKE_E_CARTS:%=$(OUT)/%)
	$(Q)python3 -c 'import sys, json; print("const elfFiles = " + json.dumps(sys.argv[1:]) + ";")' \
	    $(SPIKE_E_CARTS) > $@

# No `artifact` here — we have no prebuilt blob dependency.
deps_emcc += $(OUT)/elf_list.js $(SPIKE_E_CARTS:%=$(OUT)/%)

# Browser TCO Support Detection (informational only)
CHROME_SUPPORT_TCO_AT_MAJOR := 112
FIREFOX_SUPPORT_TCO_AT_MAJOR := 121

ifeq ($(UNAME_S),Darwin)
    CHROME_MAJOR := $(shell "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --version 2>/dev/null | awk '{print $$3}' | cut -f1 -d.)
    FIREFOX_MAJOR := $(shell /Applications/Firefox.app/Contents/MacOS/firefox --version 2>/dev/null | awk '{print $$3}' | cut -f1 -d.)
else ifeq ($(UNAME_S),Linux)
    CHROME_MAJOR := $(shell google-chrome --version 2>/dev/null | awk '{print $$3}' | cut -f1 -d.)
    FIREFOX_MAJOR := $(shell firefox -v 2>/dev/null | awk '{print $$3}' | cut -f1 -d.)
endif

ifneq ($(CHROME_MAJOR),)
ifeq ($(call version_gte,$(CHROME_MAJOR),,,$(CHROME_SUPPORT_TCO_AT_MAJOR),,), 1)
    $(info $(call noticex, Chrome $(CHROME_MAJOR) supports TCO))
else
    $(warning Chrome $(CHROME_MAJOR) does not support TCO (requires $(CHROME_SUPPORT_TCO_AT_MAJOR)+))
endif
endif

ifneq ($(FIREFOX_MAJOR),)
ifeq ($(call version_gte,$(FIREFOX_MAJOR),,,$(FIREFOX_SUPPORT_TCO_AT_MAJOR),,), 1)
    $(info $(call noticex, Firefox $(FIREFOX_MAJOR) supports TCO))
else
    $(warning Firefox $(FIREFOX_MAJOR) does not support TCO (requires $(FIREFOX_SUPPORT_TCO_AT_MAJOR)+))
endif
endif

# Web Demo Server — kept for parity with upstream `make start-web`.
# spike-e/Makefile provides its own start-web that copies the spike_e harness.
DEMO_IP := 127.0.0.1
DEMO_PORT := 8000

check-demo-dir-exist:
	$(Q)if [ ! -d "$(DEMO_DIR)" ]; then mkdir -p "$(DEMO_DIR)"; fi

define cp-web-file
    $(Q)cp $(1) $(DEMO_DIR)
    $(info)
endef

STATIC_WEB_FILES := $(WEB_JS_RESOURCES)/coi-serviceworker.min.js \
                    $(WEB_HTML_RESOURCES)/user.html

start_web_deps := check-demo-dir-exist $(BIN)

start-web: $(start_web_deps)
	$(Q)rm -f $(DEMO_DIR)/*.html
	$(foreach T, $(WEB_FILES), $(call cp-web-file, $(T)))
	$(foreach T, $(STATIC_WEB_FILES), $(call cp-web-file, $(T)))
	$(Q)mv $(DEMO_DIR)/*.html $(DEMO_DIR)/index.html
	$(Q)python3 -m http.server --bind $(DEMO_IP) $(DEMO_PORT) --directory $(DEMO_DIR)

.PHONY: check-demo-dir-exist start-web

endif # CC_IS_EMCC

endif # _MK_WASM_INCLUDED
