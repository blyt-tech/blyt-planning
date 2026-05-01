## Spike F — Lua 5.4 compiled directly to WebAssembly via Emscripten.
##
## The point of this spike is to skip the rv32emu layer entirely:
## host.c links against Lua 5.4 source, the result is a WASM module, and
## the same Spike B benchmark .lua files run inside it.
##
## Targets:
##   make lua-src         Fetch Lua 5.4.7, verify checksum, sed LUA_32BITS=1
##   make lua-wasm        Build build/lua.{js,wasm} with bench .lua embedded
##   make node-test BENCH=doom_tick
##                        Run a single bench under Node + WASM.  Prints the
##                        same FRAME / SUMMARY format as spike-b/spike-e.
##   make node-test-all   Same, all benches; writes baselines/<bench>.node-wasm.txt
##   make stage-web       Build, copy harness + lua.js/wasm into demo/.
##   make start-web       stage-web + http.server on 127.0.0.1:8000
##   make start-web-lan   stage-web + http.server on 0.0.0.0:8000 (phone testing)
##   make chrome-bench    Drive headless Chrome via puppeteer-core, capture
##                        per-cart histograms into baselines/chrome-desktop.json
##   make clean           Remove build/ and demo/
##   make distclean       clean + remove lua-src/ and emsdk symlink target
##
## Layout:
##   emsdk/               Symlink to ../spike-e/emsdk (Emscripten 3.1.51)
##   lua-src/             Vendored Lua 5.4.7 source (gitignored, fetched)
##   benchmarks/          .lua files copied from ../spike-b/benchmarks/
##   build/               Output (lua.js, lua.wasm, *.o) — gitignored
##   web/                 Harness + driver scripts
##   baselines/           Captured per-bench output and JSON

# ── tooling ────────────────────────────────────────────────────────────────

EMSDK_DIR     := $(CURDIR)/emsdk
EMSDK_VERSION := 3.1.51
EMSDK_ENV     := $(EMSDK_DIR)/emsdk_env.sh

# Use emsdk's bundled python / node so versions match what Spike E used.
EM_PATH := $(EMSDK_DIR)/upstream/emscripten:$(EMSDK_DIR):$(EMSDK_DIR)/node/22.16.0_64bit/bin
NODE    := $(EMSDK_DIR)/node/22.16.0_64bit/bin/node
EMCC    := $(EMSDK_DIR)/upstream/emscripten/emcc

EM_RUN := PATH="$(EM_PATH):$$PATH" \
          EMSDK="$(EMSDK_DIR)" \
          EMSDK_NODE="$(NODE)"

# ── source layout ──────────────────────────────────────────────────────────

LUA_VERSION   := 5.4.7
LUA_TARBALL   := lua-$(LUA_VERSION).tar.gz
LUA_URL       := https://www.lua.org/ftp/$(LUA_TARBALL)
LUA_SHA256    := 9fbf5e28ef86c69858f6d3d34eccc32e911c1a28b4120ff3e84aaa70cfbf1e30
LUA_SRC_DIR   := $(CURDIR)/lua-src
LUA_SRC       := $(LUA_SRC_DIR)/src

BUILD         := build
LUA_OUT       := $(BUILD)/lua.js
HOST_C        := host.c

# Lua source files we compile.  We omit:
#   lua.c, luac.c           — interpreter / compiler entry points (we have host.c)
#   linit.c                 — we open libs explicitly via luaL_openlibs
#   liolib.c, loslib.c,     — no FS / no os time (we use clock_gettime in host)
#   loadlib.c               — no dynamic loading
#   lcorolib.c, ldblib.c,   — not exercised by the benchmarks; trim like spike-b
#   lutf8lib.c
LUA_OMIT      := lua.c luac.c
LUA_ALL_SRCS  := $(notdir $(wildcard $(LUA_SRC)/*.c))
LUA_SRCS      := $(filter-out $(LUA_OMIT), $(LUA_ALL_SRCS))

BENCH_DIR     := $(CURDIR)/benchmarks
BENCH_SRC     := $(CURDIR)/../spike-b/benchmarks
BENCHES       := binarytrees doom_tick doom_tick_gc entity_update \
                 fannkuch fasta mandelbrot nbody spectral-norm
BENCH_FILES   := $(patsubst %, $(BENCH_DIR)/%.lua, $(BENCHES))

EMBED_FLAGS   := $(foreach b,$(BENCHES),--embed-file $(BENCH_DIR)/$(b).lua@/$(b).lua)

# ── Lua 5.4 source vendoring ───────────────────────────────────────────────

.PHONY: lua-src
lua-src: $(LUA_SRC)/lua.h

$(LUA_SRC)/lua.h:
	@mkdir -p "$(LUA_SRC_DIR)"
	@cd "$(LUA_SRC_DIR)" && \
	    curl -fsSL -o "$(LUA_TARBALL)" "$(LUA_URL)" && \
	    echo "$(LUA_SHA256)  $(LUA_TARBALL)" | shasum -a 256 -c - && \
	    tar xf "$(LUA_TARBALL)" --strip-components=1 && \
	    rm "$(LUA_TARBALL)" && \
	    sed -i.bak -E 's|^#define LUA_32BITS([[:space:]]+)0$$|#define LUA_32BITS\11|' \
	            src/luaconf.h && \
	    rm -f src/luaconf.h.bak && \
	    grep '^#define LUA_32BITS' src/luaconf.h | grep -q ' 1$$\|	1$$' || \
	        (echo "ERROR: LUA_32BITS sed did not flip 0→1" >&2; exit 1)

# ── benchmark staging ──────────────────────────────────────────────────────
# Copy the .lua benchmarks from spike-b so emcc embed paths are stable.

$(BENCH_DIR)/%.lua: $(BENCH_SRC)/%.lua
	@mkdir -p "$(BENCH_DIR)"
	cp "$<" "$@"

.PHONY: benchmarks
benchmarks: $(BENCH_FILES)

# ── WASM build ─────────────────────────────────────────────────────────────

LUA_INCLUDE := -I$(LUA_SRC)

# Lua's source assumes the host platform; under Emscripten we mark it as
# POSIX so that lauxlib + lstring can use POSIX features that emscripten
# implements.  No DLOPEN (we don't load shared libs).
EMCC_DEFS  := -DLUA_USE_POSIX -DLUA_32BITS

# Emscripten flags.
#  * -O3 — match spike-e's optimisation level so the comparison is fair.
#  * -sINITIAL_MEMORY=64MB — well above what Lua + the bench scripts need
#    (spike-e ran 256 MB but most of that was rv32emu's guest memory).
#  * -sALLOW_MEMORY_GROWTH — defensive; shouldn't be needed at this size.
#  * -sEXPORTED_FUNCTIONS=_main — main is what we expose; user-pre.js calls
#    callMain([bench_name]).  No _run_user export needed because callMain
#    is part of Emscripten's runtime helpers.
#  * -sEXPORTED_RUNTIME_METHODS=callMain,FS — callMain for harness use, FS
#    only as a debugging aid in Node.
#  * No -sPTHREAD_POOL_SIZE — same lesson as spike-e (no SAB, no COOP/COEP).
#  * --pre-js web/user-pre.js — sets noInitialRun=true and exposes run_user.
EMCC_FLAGS := -O3 \
              -sINITIAL_MEMORY=64MB \
              -sALLOW_MEMORY_GROWTH \
              -sEXPORTED_FUNCTIONS=_main \
              -sEXPORTED_RUNTIME_METHODS=callMain,FS \
              -sFORCE_FILESYSTEM=1 \
              --pre-js web/user-pre.js \
              $(EMBED_FLAGS) \
              -w

LUA_C_SRCS  := $(patsubst %, $(LUA_SRC)/%, $(LUA_SRCS))

.PHONY: lua-wasm
lua-wasm: $(LUA_OUT)

$(LUA_OUT): $(LUA_SRC)/lua.h $(BENCH_FILES) $(HOST_C) web/user-pre.js
	@mkdir -p "$(BUILD)"
	$(EM_RUN) "$(EMCC)" $(EMCC_FLAGS) $(EMCC_DEFS) $(LUA_INCLUDE) \
	    -o $(LUA_OUT) \
	    $(HOST_C) $(LUA_C_SRCS)

# ── Node smoke-test ────────────────────────────────────────────────────────

BENCH ?= doom_tick

.PHONY: node-test
node-test: lua-wasm web/node_run.cjs
	@cp web/node_run.cjs "$(BUILD)/"
	@echo "=== Node + WASM Lua-direct running $(BENCH) ==="
	cd "$(BUILD)" && "$(NODE)" node_run.cjs "$(BENCH)"

.PHONY: node-test-all
node-test-all: lua-wasm web/node_run.cjs
	@cp web/node_run.cjs "$(BUILD)/"
	@mkdir -p baselines
	@for b in $(BENCHES); do \
	    echo "=== $$b ==="; \
	    (cd "$(BUILD)" && "$(NODE)" node_run.cjs "$$b") \
	        | tee "baselines/$$b.node-wasm.txt" | tail -1; \
	done

# ── desktop dev server with the spike_f harness ────────────────────────────

DEMO_DIR  := demo
DEMO_PORT ?= 8000
DEMO_IP   ?= 127.0.0.1

.PHONY: bench-list
bench-list: $(BUILD)/bench_list.js

$(BUILD)/bench_list.js: Makefile
	@mkdir -p "$(BUILD)"
	@python3 -c 'import sys, json; print("const benchFiles = " + json.dumps(sys.argv[1:]) + ";")' \
	    $(BENCHES) > "$@"

.PHONY: stage-web
stage-web: lua-wasm $(BUILD)/bench_list.js web/spike_f.html web/spike_f.js web/spike_f_worker.js
	@rm -rf "$(DEMO_DIR)"
	@mkdir -p "$(DEMO_DIR)"
	cp web/spike_f.html         "$(DEMO_DIR)/index.html"
	cp web/spike_f.js           "$(DEMO_DIR)/"
	cp web/spike_f_worker.js    "$(DEMO_DIR)/"
	cp "$(BUILD)/lua.js"        "$(DEMO_DIR)/"
	cp "$(BUILD)/lua.wasm"      "$(DEMO_DIR)/"
	cp "$(BUILD)/bench_list.js" "$(DEMO_DIR)/"

.PHONY: start-web
start-web: stage-web
	@echo "Serving $(DEMO_DIR) on http://$(DEMO_IP):$(DEMO_PORT)/"
	cd "$(DEMO_DIR)" && python3 -m http.server --bind $(DEMO_IP) $(DEMO_PORT)

# Bind to all interfaces — convenience for phone testing via LAN.
.PHONY: start-web-lan
start-web-lan: stage-web
	@echo "Serving $(DEMO_DIR) on http://0.0.0.0:$(DEMO_PORT)/ (LAN)"
	cd "$(DEMO_DIR)" && python3 -m http.server --bind 0.0.0.0 $(DEMO_PORT)

# ── headless Chrome capture (puppeteer-core driving system Chrome) ─────────

CHROME_PORT     ?= 8765
CHROME_BENCHES  ?= doom_tick,entity_update,binarytrees

node_modules/puppeteer-core/package.json:
	npm install --no-fund --no-audit puppeteer-core@22

.PHONY: chrome-bench
chrome-bench: stage-web node_modules/puppeteer-core/package.json
	@mkdir -p baselines
	@(cd "$(DEMO_DIR)" && python3 -m http.server --bind 127.0.0.1 $(CHROME_PORT) >/dev/null 2>&1 & \
	  echo $$! > /tmp/spike-f-chrome-http.pid; \
	  sleep 0.5; \
	  node web/run_chrome.cjs --port $(CHROME_PORT) --bench "$(CHROME_BENCHES)" \
	      --out baselines/chrome-desktop.json; \
	  kill $$(cat /tmp/spike-f-chrome-http.pid) 2>/dev/null || true; \
	  rm -f /tmp/spike-f-chrome-http.pid)

# ── housekeeping ───────────────────────────────────────────────────────────

.PHONY: clean
clean:
	$(RM) -r "$(BUILD)" "$(DEMO_DIR)"

.PHONY: distclean
distclean: clean
	$(RM) -r "$(LUA_SRC_DIR)" "$(BENCH_DIR)"
