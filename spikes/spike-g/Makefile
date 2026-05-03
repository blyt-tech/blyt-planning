## Spike G — lua_sethook Tier-1 CPU-budget enforcement overhead measurement.
##
## Reuses spike-f's emsdk symlink, vendored Lua 5.4.7 source, and bench .lua
## files.  Does not re-vendor them.
##
## Targets:
##   make hook-wasm [N=<n>]
##               Build build/hook_lua_N<n>.{js,wasm} (default N=1000, hook on).
##               N=0 builds the no-hook baseline: build/hook_lua_N0.{js,wasm}.
##
##   make node-test [BENCH=<b>] [N=<n>]
##               Run one bench under Node + WASM (default doom_tick, N=1000).
##
##   make node-test-all [N=<n>]
##               Run all nine benches under Node + WASM; write output to
##               baselines/<bench>.node-N<n>.txt.  A quick correctness check
##               at a high budget (hook enabled but never fires).
##
##   make node-fire-test
##               Build with tiny budget (100 µs) to force hook fire; run
##               fire_host on doom_tick and check it prints
##               "HOOK FIRE TEST PASS".
##
##   make node-overhead-all [N=<n>]
##               Run all nine benches under Node + WASM; write output to
##               baselines/node-N<n>/.  Used during the N sweep.
##
##   make chrome-overhead [N=<n>]
##               Drive headless Chrome via puppeteer-core; write per-cart
##               JSON to baselines/chrome-N<n>/chrome-overhead.json.
##
##   make n-sweep
##               Build all N values (0, 100, 500, 1000, 5000, 10000) and run
##               node-overhead-all for each.
##
##   make stage-web [N=<n>]
##               Copy harness + hook_lua_N<n>.{js,wasm} into demo/.
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

# Reuse spike-f's vendored Lua source and benchmark .lua files.
LUA_SRC       := $(CURDIR)/../spike-f/lua-src/src
BENCH_DIR     := $(CURDIR)/../spike-f/benchmarks

BENCHES       := binarytrees doom_tick doom_tick_gc entity_update \
                 fannkuch fasta mandelbrot nbody spectral-norm

LUA_OMIT      := lua.c luac.c
LUA_ALL_SRCS  := $(notdir $(wildcard $(LUA_SRC)/*.c))
LUA_SRCS      := $(filter-out $(LUA_OMIT), $(LUA_ALL_SRCS))
LUA_C_SRCS    := $(patsubst %, $(LUA_SRC)/%, $(LUA_SRCS))

BUILD         := build

EMBED_FLAGS   := $(foreach b,$(BENCHES),--embed-file $(BENCH_DIR)/$(b).lua@/$(b).lua)

# ── N parameter (override on command line: make hook-wasm N=500) ───────────

N ?= 1000

# Compute HOOK_ENABLED from N: N=0 means no-hook baseline.
ifeq ($(N),0)
  HOOK_ENABLED_FLAG := -DHOOK_ENABLED=0
  OUT_SUFFIX        := N0
else
  HOOK_ENABLED_FLAG := -DHOOK_ENABLED=1 -DHOOK_COUNT=$(N)
  OUT_SUFFIX        := N$(N)
endif

HOOK_OUT := $(BUILD)/hook_lua_$(OUT_SUFFIX).js

# ── common Emscripten flags ────────────────────────────────────────────────

LUA_INCLUDE := -I$(LUA_SRC)
EMCC_DEFS   := -DLUA_USE_POSIX -DLUA_32BITS

EMCC_FLAGS_BASE := -O3 \
              -sINITIAL_MEMORY=64MB \
              -sALLOW_MEMORY_GROWTH \
              -sEXPORTED_FUNCTIONS=_main \
              -sEXPORTED_RUNTIME_METHODS=callMain,FS \
              -sFORCE_FILESYSTEM=1 \
              --pre-js web/user-pre.js \
              -w

EMCC_FLAGS := $(EMCC_FLAGS_BASE) $(EMBED_FLAGS)

# ── WASM build ─────────────────────────────────────────────────────────────

.PHONY: hook-wasm
hook-wasm: $(HOOK_OUT)

$(BUILD)/hook_lua_N%.js: hook_host.c web/user-pre.js $(LUA_SRC)/lua.h
	@mkdir -p "$(BUILD)"
	$(eval _N := $(patsubst hook_lua_N%.js,%,$(notdir $@)))
	$(eval _HOOK := $(if $(filter 0,$(_N)),-DHOOK_ENABLED=0,-DHOOK_ENABLED=1 -DHOOK_COUNT=$(_N)))
	$(EM_RUN) "$(EMCC)" $(EMCC_FLAGS) $(EMCC_DEFS) $(LUA_INCLUDE) $(_HOOK) \
	    -o $@ hook_host.c $(LUA_C_SRCS)

# Make the default target also work without the pattern (N=1000 → hook_lua_N1000.js).
$(HOOK_OUT): hook_host.c web/user-pre.js $(LUA_SRC)/lua.h
	@mkdir -p "$(BUILD)"
	$(EM_RUN) "$(EMCC)" $(EMCC_FLAGS) $(EMCC_DEFS) $(LUA_INCLUDE) \
	    $(HOOK_ENABLED_FLAG) \
	    -DHOOK_BUDGET_NS=16667000ULL \
	    -o $(HOOK_OUT) hook_host.c $(LUA_C_SRCS)

# ── stub-clock diagnostic (hook enabled N=1000, clock stubbed to 0) ───────
# Measures the hook dispatch overhead without clock_gettime cost.

STUBCLOCK_OUT := $(BUILD)/hook_lua_stubclock.js

.PHONY: stub-clock-wasm
stub-clock-wasm: $(STUBCLOCK_OUT)

$(STUBCLOCK_OUT): hook_host.c web/user-pre.js $(LUA_SRC)/lua.h
	@mkdir -p "$(BUILD)"
	$(EM_RUN) "$(EMCC)" $(EMCC_FLAGS) $(EMCC_DEFS) $(LUA_INCLUDE) \
	    -DHOOK_ENABLED=1 -DHOOK_COUNT=1000 \
	    -DHOOK_BUDGET_NS=16667000ULL \
	    -DSTUB_CLOCK=1 \
	    -o $(STUBCLOCK_OUT) hook_host.c $(LUA_C_SRCS)

.PHONY: node-stubclock-test
node-stubclock-test: stub-clock-wasm
	@cp web/node_run.cjs "$(BUILD)/"
	@mkdir -p "baselines/node-stubclock"
	@for b in $(BENCHES); do \
	    echo "=== $$b (stubclock) ==="; \
	    (cd "$(BUILD)" && "$(NODE)" node_run.cjs "$$b" "stubclock") \
	        | tee "baselines/node-stubclock/$$b.txt" | tail -1; \
	done

# ── fire-test WASM (tiny budget — guaranteed to fire) ─────────────────────

FIRE_OUT := $(BUILD)/hook_lua_fire.js

.PHONY: fire-wasm
fire-wasm: $(FIRE_OUT)

$(FIRE_OUT): fire_host.c web/user-pre.js $(LUA_SRC)/lua.h
	@mkdir -p "$(BUILD)"
	$(EM_RUN) "$(EMCC)" $(EMCC_FLAGS) $(EMCC_DEFS) $(LUA_INCLUDE) \
	    -DHOOK_COUNT=100 -DHOOK_BUDGET_NS=100000ULL \
	    -o $(FIRE_OUT) fire_host.c $(LUA_C_SRCS)

# ── Node targets ───────────────────────────────────────────────────────────

BENCH ?= doom_tick

.PHONY: node-test
node-test: hook-wasm
	@cp web/node_run.cjs "$(BUILD)/"
	@echo "=== Node + WASM hook N=$(N) running $(BENCH) ==="
	cd "$(BUILD)" && "$(NODE)" node_run.cjs "$(BENCH)" "$(OUT_SUFFIX)"

.PHONY: node-test-all
node-test-all: hook-wasm
	@cp web/node_run.cjs "$(BUILD)/"
	@mkdir -p baselines
	@for b in $(BENCHES); do \
	    echo "=== $$b (N=$(N)) ==="; \
	    (cd "$(BUILD)" && "$(NODE)" node_run.cjs "$$b" "$(OUT_SUFFIX)") \
	        | tee "baselines/$$b.node-$(OUT_SUFFIX).txt" | tail -1; \
	done

.PHONY: node-fire-test
node-fire-test: fire-wasm
	@cp web/node_run.cjs "$(BUILD)/"
	@echo "=== Fire test: hook N=100 budget=100µs on doom_tick ==="
	@result=$$(cd "$(BUILD)" && "$(NODE)" node_run.cjs "doom_tick" "fire" 2>&1); \
	echo "$$result"; \
	if echo "$$result" | grep -q "HOOK FIRE TEST PASS"; then \
	    echo "node-fire-test: PASS"; exit 0; \
	else \
	    echo "node-fire-test: FAIL"; exit 1; \
	fi

.PHONY: node-overhead-all
node-overhead-all: hook-wasm
	@cp web/node_run.cjs "$(BUILD)/"
	@mkdir -p "baselines/node-$(OUT_SUFFIX)"
	@for b in $(BENCHES); do \
	    echo "=== $$b (N=$(N)) ==="; \
	    (cd "$(BUILD)" && "$(NODE)" node_run.cjs "$$b" "$(OUT_SUFFIX)") \
	        | tee "baselines/node-$(OUT_SUFFIX)/$$b.txt" | tail -1; \
	done

# ── N sweep ────────────────────────────────────────────────────────────────

SWEEP_NS := 0 100 500 1000 5000 10000

.PHONY: n-sweep
n-sweep:
	@for n in $(SWEEP_NS); do \
	    echo ""; \
	    echo "━━━ sweep N=$$n ━━━"; \
	    $(MAKE) node-overhead-all N=$$n; \
	done
	@echo ""
	@echo "N sweep complete. Results in baselines/node-N*/"

# ── Web / Chrome targets ────────────────────────────────────────────────────

DEMO_DIR  := demo
DEMO_PORT ?= 8000
DEMO_IP   ?= 127.0.0.1

# For the web demo we build directly to hook_lua.{js,wasm} so the JS
# references the correct .wasm filename (Emscripten bakes the base name in).
DEMO_OUT  := $(BUILD)/hook_lua.js

.PHONY: demo-wasm
demo-wasm: $(DEMO_OUT)

$(DEMO_OUT): hook_host.c web/user-pre.js $(LUA_SRC)/lua.h
	@mkdir -p "$(BUILD)"
	$(EM_RUN) "$(EMCC)" $(EMCC_FLAGS) $(EMCC_DEFS) $(LUA_INCLUDE) \
	    $(HOOK_ENABLED_FLAG) \
	    -DHOOK_BUDGET_NS=16667000ULL \
	    -o $(DEMO_OUT) hook_host.c $(LUA_C_SRCS)

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
	cp web/spike_g.html         "$(DEMO_DIR)/index.html"
	cp web/spike_g.js           "$(DEMO_DIR)/"
	cp web/spike_g_worker.js    "$(DEMO_DIR)/"
	cp "$(DEMO_OUT)"            "$(DEMO_DIR)/hook_lua.js"
	cp "$(BUILD)/hook_lua.wasm" "$(DEMO_DIR)/hook_lua.wasm"
	cp "$(BUILD)/bench_list.js" "$(DEMO_DIR)/"

.PHONY: start-web
start-web: stage-web
	@echo "Serving $(DEMO_DIR) on http://$(DEMO_IP):$(DEMO_PORT)/"
	cd "$(DEMO_DIR)" && python3 -m http.server --bind $(DEMO_IP) $(DEMO_PORT)

CHROME_PORT    ?= 8766
CHROME_BENCHES ?= doom_tick,entity_update,binarytrees

node_modules/puppeteer-core/package.json:
	npm install --no-fund --no-audit puppeteer-core@22

.PHONY: chrome-overhead
chrome-overhead: stage-web node_modules/puppeteer-core/package.json
	@mkdir -p "baselines/chrome-$(OUT_SUFFIX)"
	@python3 -m http.server --bind 127.0.0.1 $(CHROME_PORT) --directory "$(DEMO_DIR)" >/dev/null 2>&1 & \
	  echo $$! > /tmp/spike-g-chrome-http.pid; \
	  sleep 0.5; \
	  node web/run_chrome.cjs --port $(CHROME_PORT) \
	      --bench "$(CHROME_BENCHES)" \
	      --out "baselines/chrome-$(OUT_SUFFIX)/chrome-overhead.json"; \
	  kill $$(cat /tmp/spike-g-chrome-http.pid) 2>/dev/null || true; \
	  rm -f /tmp/spike-g-chrome-http.pid

# ── WebKit (Playwright) overhead measurement ──────────────────────────────
#
# Uses Playwright's WebKit build (same JSC engine as Safari).
# For actual Safari via safaridriver, use chrome-overhead-safari.

WEBKIT_PORT ?= 8768

.PHONY: webkit-overhead
webkit-overhead: stage-web
	@mkdir -p "baselines/webkit-$(OUT_SUFFIX)"
	@python3 -m http.server --bind 127.0.0.1 $(WEBKIT_PORT) --directory "$(DEMO_DIR)" >/dev/null 2>&1 & \
	  echo $$! > /tmp/spike-g-webkit-http.pid; \
	  sleep 0.5; \
	  node web/run_webkit.cjs --port $(WEBKIT_PORT) \
	      --bench "$(CHROME_BENCHES)" \
	      --out "baselines/webkit-$(OUT_SUFFIX)/webkit-overhead.json"; \
	  kill $$(cat /tmp/spike-g-webkit-http.pid) 2>/dev/null || true; \
	  rm -f /tmp/spike-g-webkit-http.pid

# ── Safari (safaridriver) overhead measurement ─────────────────────────────
#
# Requires: safaridriver --enable (once, needs admin) and Safari open.
# Uses run_safari.cjs (selenium-webdriver + safaridriver).

SAFARI_PORT ?= 8769

.PHONY: safari-overhead
safari-overhead: stage-web
	@mkdir -p "baselines/safari-$(OUT_SUFFIX)"
	@python3 -m http.server --bind 127.0.0.1 $(SAFARI_PORT) --directory "$(DEMO_DIR)" >/dev/null 2>&1 & \
	  echo $$! > /tmp/spike-g-safari-http.pid; \
	  sleep 0.5; \
	  node web/run_safari.cjs --port $(SAFARI_PORT) \
	      --bench "$(CHROME_BENCHES)" \
	      --out "baselines/safari-$(OUT_SUFFIX)/safari-overhead.json"; \
	  kill $$(cat /tmp/spike-g-safari-http.pid) 2>/dev/null || true; \
	  rm -f /tmp/spike-g-safari-http.pid

# ── housekeeping ───────────────────────────────────────────────────────────

.PHONY: clean
clean:
	$(RM) -r "$(BUILD)" "$(DEMO_DIR)"
