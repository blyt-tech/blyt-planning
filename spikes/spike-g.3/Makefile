## Spike G.3 — LUA_MASKLINE dev-mode throttle (accumulated-debt variant).
##
## Reuses spike-g's emsdk symlink, spike-f's vendored Lua 5.4.7 source, and
## bench .lua files.  Does not re-vendor them.
##
## Targets:
##   make throttle-wasm [THROTTLE_DELAY_NS=<ns>]
##               Build build/throttle_lua_D<ns>.{js,wasm} with hook enabled.
##               Default THROTTLE_DELAY_NS=0 (hook on, no debt accumulated).
##
##   make throttle-wasm THROTTLE_ENABLED=0
##               Build build/throttle_lua_nohook.{js,wasm} — no-hook baseline.
##
##   make node-linecount-wasm
##               Build build/throttle_lua_linecount.{js,wasm} (hook + counter).
##
##   make node-test [BENCH=<b>] [THROTTLE_DELAY_NS=<ns>]
##               Run one bench under Node + WASM.
##
##   make node-test-all [THROTTLE_DELAY_NS=<ns>]
##               Run all nine benches under Node + WASM; write output to
##               baselines/<bench>.node-D<ns>.txt.  Correctness check.
##
##   make chrome-cheap-overhead
##               Drive headless Chrome with THROTTLE_DELAY_NS=1 (hook on,
##               1 ns debt/line — spin trips ~once per 100,000 lines, so
##               nearly all per-line cost is the cheap-path clock_gettime
##               read); write baselines/chrome-cheap/chrome-overhead.json.
##
##   make chrome-throttle D=<ns>
##               Drive headless Chrome with THROTTLE_DELAY_NS=D; write
##               baselines/chrome-D<ns>/chrome-throttle.json.
##
##   make chrome-throttle-all D_LOW=<ns> D_MID=<ns> D_HIGH=<ns>
##               Run chrome-throttle for D=0, D_LOW, D_MID, D_HIGH.
##
##   make clean
##               Remove build/ and demo/.

# ── tooling ────────────────────────────────────────────────────────────────

EMSDK_DIR     := $(CURDIR)/emsdk
EMSDK_ENV     := $(EMSDK_DIR)/emsdk_env.sh

EM_PATH := $(EMSDK_DIR)/upstream/emscripten:$(EMSDK_DIR):$(EMSDK_DIR)/node/22.16.0_64bit/bin
NODE    := $(EMSDK_DIR)/node/22.16.0_64bit/bin/node
EMCC    := $(EMSDK_DIR)/upstream/emscripten/emcc

EM_RUN := PATH="$(EM_PATH):$$PATH" \
          EMSDK="$(EMSDK_DIR)" \
          EMSDK_NODE="$(NODE)"

# ── source layout ──────────────────────────────────────────────────────────

LUA_SRC       := $(CURDIR)/../spike-g/../spike-f/lua-src/src
BENCH_DIR     := $(CURDIR)/../spike-f/benchmarks

BENCHES       := binarytrees doom_tick doom_tick_gc entity_update \
                 fannkuch fasta mandelbrot nbody spectral-norm

LUA_OMIT      := lua.c luac.c
LUA_ALL_SRCS  := $(notdir $(wildcard $(LUA_SRC)/*.c))
LUA_SRCS      := $(filter-out $(LUA_OMIT), $(LUA_ALL_SRCS))
LUA_C_SRCS    := $(patsubst %, $(LUA_SRC)/%, $(LUA_SRCS))

BUILD         := build

EMBED_FLAGS   := $(foreach b,$(BENCHES),--embed-file $(BENCH_DIR)/$(b).lua@/$(b).lua)

# ── build parameters ───────────────────────────────────────────────────────

THROTTLE_DELAY_NS ?= 0
THROTTLE_ENABLED  ?= 1
LINECOUNT_ENABLED ?= 0

# Derive output suffix and compiler flags from parameters.
ifeq ($(THROTTLE_ENABLED),0)
  THROTTLE_FLAGS := -DTHROTTLE_ENABLED=0
  OUT_SUFFIX     := nohook
else ifeq ($(LINECOUNT_ENABLED),1)
  THROTTLE_FLAGS := -DTHROTTLE_ENABLED=1 -DTHROTTLE_DELAY_NS=0 -DLINECOUNT_ENABLED=1
  OUT_SUFFIX     := linecount
else
  THROTTLE_FLAGS := -DTHROTTLE_ENABLED=1 -DTHROTTLE_DELAY_NS=$(THROTTLE_DELAY_NS)
  OUT_SUFFIX     := D$(THROTTLE_DELAY_NS)
endif

THROTTLE_OUT := $(BUILD)/throttle_lua_$(OUT_SUFFIX).js

# ── common Emscripten flags ────────────────────────────────────────────────

LUA_INCLUDE := -I$(LUA_SRC)
EMCC_DEFS   := -DLUA_USE_POSIX -DLUA_32BITS

EMCC_FLAGS := -O3 \
              -sINITIAL_MEMORY=64MB \
              -sALLOW_MEMORY_GROWTH \
              -sEXPORTED_FUNCTIONS=_main \
              -sEXPORTED_RUNTIME_METHODS=callMain,FS \
              -sFORCE_FILESYSTEM=1 \
              --pre-js web/user-pre.js \
              -w \
              $(EMBED_FLAGS)

# ── WASM builds ────────────────────────────────────────────────────────────

.PHONY: throttle-wasm
throttle-wasm: $(THROTTLE_OUT)

$(THROTTLE_OUT): throttle_host.c web/user-pre.js $(LUA_SRC)/lua.h
	@mkdir -p "$(BUILD)"
	$(EM_RUN) "$(EMCC)" $(EMCC_FLAGS) $(EMCC_DEFS) $(LUA_INCLUDE) \
	    $(THROTTLE_FLAGS) \
	    -o $(THROTTLE_OUT) throttle_host.c $(LUA_C_SRCS)

.PHONY: node-linecount-wasm
node-linecount-wasm:
	$(MAKE) throttle-wasm LINECOUNT_ENABLED=1

# ── Node targets ───────────────────────────────────────────────────────────

BENCH ?= doom_tick

.PHONY: node-test
node-test: throttle-wasm
	@cp web/node_run.cjs "$(BUILD)/"
	@echo "=== Node + WASM throttle $(OUT_SUFFIX) running $(BENCH) ==="
	cd "$(BUILD)" && "$(NODE)" node_run.cjs "$(BENCH)" "$(OUT_SUFFIX)"

.PHONY: node-test-all
node-test-all: throttle-wasm
	@cp web/node_run.cjs "$(BUILD)/"
	@mkdir -p baselines
	@for b in $(BENCHES); do \
	    echo "=== $$b ($(OUT_SUFFIX)) ==="; \
	    (cd "$(BUILD)" && "$(NODE)" node_run.cjs "$$b" "$(OUT_SUFFIX)") \
	        | tee "baselines/$$b.node-$(OUT_SUFFIX).txt" | tail -1; \
	done

.PHONY: node-linecount
node-linecount: node-linecount-wasm
	@cp web/node_run.cjs "$(BUILD)/"
	@mkdir -p baselines/linecount
	@echo "=== $(BENCH) (linecount) ==="
	@(cd "$(BUILD)" && "$(NODE)" node_run.cjs "$(BENCH)" "linecount" 2>&1) \
	    | tee "baselines/linecount/$(BENCH).txt" | grep -E "(SUMMARY|LINE_TOTAL)"

.PHONY: node-linecount-all
node-linecount-all: node-linecount-wasm
	@cp web/node_run.cjs "$(BUILD)/"
	@mkdir -p baselines/linecount
	@for b in $(BENCHES); do \
	    echo "=== $$b (linecount) ==="; \
	    (cd "$(BUILD)" && "$(NODE)" node_run.cjs "$$b" "linecount" 2>&1) \
	        | tee "baselines/linecount/$$b.txt" | grep -E "(SUMMARY|LINE_TOTAL)"; \
	done

# ── Web / Chrome targets ────────────────────────────────────────────────────

DEMO_DIR  := demo
DEMO_PORT ?= 8000
DEMO_IP   ?= 127.0.0.1

# For the web demo we build directly to throttle_lua.{js,wasm} so the JS
# references the correct .wasm filename (Emscripten bakes the base name in).
DEMO_OUT := $(BUILD)/throttle_lua.js

.PHONY: demo-wasm
demo-wasm:
	@mkdir -p "$(BUILD)"
	$(EM_RUN) "$(EMCC)" $(EMCC_FLAGS) $(EMCC_DEFS) $(LUA_INCLUDE) \
	    $(THROTTLE_FLAGS) \
	    -o $(DEMO_OUT) throttle_host.c $(LUA_C_SRCS)

.PHONY: bench-list
bench-list: $(BUILD)/bench_list.js

$(BUILD)/bench_list.js: Makefile
	@mkdir -p "$(BUILD)"
	@python3 -c 'import sys, json; print("const benchFiles = " + json.dumps(sys.argv[1:]) + ";")' \
	    $(BENCHES) > "$@"

.PHONY: stage-web
stage-web: demo-wasm $(BUILD)/bench_list.js
	@rm -rf "$(DEMO_DIR)"
	@mkdir -p "$(DEMO_DIR)"
	cp web/spike_g2.html         "$(DEMO_DIR)/index.html"
	cp web/spike_g2.js           "$(DEMO_DIR)/"
	cp web/spike_g2_worker.js    "$(DEMO_DIR)/"
	cp "$(DEMO_OUT)"             "$(DEMO_DIR)/throttle_lua.js"
	cp "$(BUILD)/throttle_lua.wasm" "$(DEMO_DIR)/throttle_lua.wasm"
	cp "$(BUILD)/bench_list.js"  "$(DEMO_DIR)/"

CHROME_PORT    ?= 8767
CHROME_BENCHES ?= doom_tick,entity_update,binarytrees,mandelbrot,spectral-norm

# Ensure puppeteer-core is available (reuse spike-g's node_modules if present).
node_modules/puppeteer-core/package.json:
	@if [ -d "../spike-g/node_modules/puppeteer-core" ]; then \
	    mkdir -p node_modules && \
	    ln -sf "$(CURDIR)/../spike-g/node_modules/puppeteer-core" node_modules/puppeteer-core; \
	else \
	    npm install --no-fund --no-audit puppeteer-core@22; \
	fi

# Stage 1': hook active with D=1 — measures dispatch + cheap-path clock_gettime
# read.  At D=1 the spin trips ~once per 100,000 lines, so per-line cost is
# dominated by the cheap-path read.  (D=0 lets us also reproduce G.2's noop.)
.PHONY: chrome-cheap-overhead
chrome-cheap-overhead: node_modules/puppeteer-core/package.json
	$(MAKE) stage-web THROTTLE_DELAY_NS=1 THROTTLE_ENABLED=1
	@mkdir -p "baselines/chrome-cheap"
	@python3 -m http.server --bind 127.0.0.1 $(CHROME_PORT) --directory "$(DEMO_DIR)" >/dev/null 2>&1 & \
	  echo $$! > /tmp/spike-g3-chrome-http.pid; \
	  sleep 0.5; \
	  node web/run_chrome.cjs --port $(CHROME_PORT) \
	      --bench "$(CHROME_BENCHES)" \
	      --out "baselines/chrome-cheap/chrome-overhead.json"; \
	  kill $$(cat /tmp/spike-g3-chrome-http.pid) 2>/dev/null || true; \
	  rm -f /tmp/spike-g3-chrome-http.pid

# G.2-equivalent noop overhead (hook installed, debt never accumulates).
.PHONY: chrome-noop-overhead
chrome-noop-overhead: node_modules/puppeteer-core/package.json
	$(MAKE) stage-web THROTTLE_DELAY_NS=0 THROTTLE_ENABLED=1
	@mkdir -p "baselines/chrome-noop"
	@python3 -m http.server --bind 127.0.0.1 $(CHROME_PORT) --directory "$(DEMO_DIR)" >/dev/null 2>&1 & \
	  echo $$! > /tmp/spike-g3-chrome-http.pid; \
	  sleep 0.5; \
	  node web/run_chrome.cjs --port $(CHROME_PORT) \
	      --bench "$(CHROME_BENCHES)" \
	      --out "baselines/chrome-noop/chrome-overhead.json"; \
	  kill $$(cat /tmp/spike-g3-chrome-http.pid) 2>/dev/null || true; \
	  rm -f /tmp/spike-g3-chrome-http.pid

# Stage 1 nohook baseline via Chrome (for comparison).
.PHONY: chrome-nohook
chrome-nohook: node_modules/puppeteer-core/package.json
	$(MAKE) stage-web THROTTLE_ENABLED=0
	@mkdir -p "baselines/chrome-nohook"
	@python3 -m http.server --bind 127.0.0.1 $(CHROME_PORT) --directory "$(DEMO_DIR)" >/dev/null 2>&1 & \
	  echo $$! > /tmp/spike-g3-chrome-http.pid; \
	  sleep 0.5; \
	  node web/run_chrome.cjs --port $(CHROME_PORT) \
	      --bench "$(CHROME_BENCHES)" \
	      --out "baselines/chrome-nohook/chrome-overhead.json"; \
	  kill $$(cat /tmp/spike-g3-chrome-http.pid) 2>/dev/null || true; \
	  rm -f /tmp/spike-g3-chrome-http.pid

# Stage 4: throttled measurement for one delay variant.
D ?= 0

.PHONY: chrome-throttle
chrome-throttle: node_modules/puppeteer-core/package.json
	$(MAKE) stage-web THROTTLE_DELAY_NS=$(D) THROTTLE_ENABLED=1
	@mkdir -p "baselines/chrome-D$(D)"
	@python3 -m http.server --bind 127.0.0.1 $(CHROME_PORT) --directory "$(DEMO_DIR)" >/dev/null 2>&1 & \
	  echo $$! > /tmp/spike-g3-chrome-http.pid; \
	  sleep 0.5; \
	  node web/run_chrome.cjs --port $(CHROME_PORT) \
	      --bench "$(CHROME_BENCHES)" \
	      --out "baselines/chrome-D$(D)/chrome-throttle.json"; \
	  kill $$(cat /tmp/spike-g3-chrome-http.pid) 2>/dev/null || true; \
	  rm -f /tmp/spike-g3-chrome-http.pid

# Stage 4 sweep: run all calibrated variants.
# Set D_LOW, D_MID, D_HIGH from computed calibration constants.
D_MID  ?= 0
D_LOW  ?= 0
D_HIGH ?= 0

.PHONY: chrome-throttle-all
chrome-throttle-all: node_modules/puppeteer-core/package.json
	$(MAKE) chrome-throttle D=0
	$(MAKE) chrome-throttle D=$(D_LOW)
	$(MAKE) chrome-throttle D=$(D_MID)
	$(MAKE) chrome-throttle D=$(D_HIGH)

# ── housekeeping ───────────────────────────────────────────────────────────

.PHONY: clean
clean:
	$(RM) -r "$(BUILD)" "$(DEMO_DIR)"
