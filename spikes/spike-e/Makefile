## Spike E — rv32emu + Lua-cart, compiled to WebAssembly via Emscripten.
##
## Targets:
##   make emsdk            One-time emsdk install (Emscripten 3.1.51) under ./emsdk
##   make elfs             Extract spike-b cart ELFs from the fc32-spike-b image
##   make wasm             Build rv32emu.{js,wasm,worker.js} + elf_list.js
##   make node-test BENCH=lua_cart_doom_tick.elf
##                         Run a single cart under Node + WASM, byte-comparable
##                         to docker run output (modulo timing values).
##   make start-web        Build, stage the spike_e harness into demo/, serve
##                         on http://127.0.0.1:8000.
##   make clean            Remove the staged rv32emu copy and built WASM.
##
## Layout:
##   emsdk/                Emscripten 3.1.51 install (gitignored)
##   elfs/                 Spike B cart ELFs (extracted from fc32-spike-b)
##   configs/              Kconfig overlay for the WASM build
##   wasm.mk               Replacement for rv32emu/mk/wasm.mk (drops upstream
##                         demo embeds, adds our cart embeds)
##   web/                  spike_e.html / spike_e.js — measurement harness
##   rv32emu/              Working copy of upstream rv32emu source (created by
##                         `make wasm`, gitignored)
##   build-wasm/           Output of the WASM build (rv32emu.js, .wasm, ...)
##   demo/                 Staged page served by start-web

# ── tooling ────────────────────────────────────────────────────────────────

EMSDK_DIR     := $(CURDIR)/emsdk
EMSDK_VERSION := 3.1.51
EMSDK_ENV     := $(EMSDK_DIR)/emsdk_env.sh

# Use emsdk's bundled python so we get the version emcc was tested against.
EM_PATH := $(EMSDK_DIR)/upstream/emscripten:$(EMSDK_DIR):$(EMSDK_DIR)/node/22.16.0_64bit/bin

# ── source layout ──────────────────────────────────────────────────────────

UPSTREAM_RV32EMU := $(CURDIR)/../spike-a/rv32emu
RV32EMU_DIR      := $(CURDIR)/rv32emu
WASM_OUT         := build-wasm
WASM_BIN         := $(RV32EMU_DIR)/$(WASM_OUT)/rv32emu.js

CART_SRC_DIR := $(CURDIR)/elfs
CARTS := \
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

# ── one-time emsdk install ─────────────────────────────────────────────────

.PHONY: emsdk
emsdk: $(EMSDK_ENV)

$(EMSDK_ENV):
	@if [ ! -d "$(EMSDK_DIR)" ]; then \
	    git clone https://github.com/emscripten-core/emsdk.git "$(EMSDK_DIR)"; \
	fi
	cd "$(EMSDK_DIR)" && ./emsdk install $(EMSDK_VERSION) && ./emsdk activate $(EMSDK_VERSION)

# ── extract cart ELFs from the spike-b Docker image ────────────────────────

.PHONY: elfs
elfs: $(CARTS:%=$(CART_SRC_DIR)/%)

$(CART_SRC_DIR)/%.elf:
	@mkdir -p "$(CART_SRC_DIR)"
	@echo "Extracting $* from fc32-spike-b ..."
	@docker image inspect fc32-spike-b >/dev/null 2>&1 || $(MAKE) -C ../spike-b docker-build
	@CID=$$(docker create --platform linux/arm64 fc32-spike-b) \
	    && docker cp "$$CID:/spike-b/elfs/." "$(CART_SRC_DIR)/" \
	    && docker rm "$$CID" >/dev/null

# ── stage the rv32emu source tree under spike-e ────────────────────────────
# We copy (not symlink) so we can patch wasm.mk without dirtying spike-a.
# rsync to keep the source consistent with upstream while preserving our
# overrides (.config, mk/wasm.mk, build-wasm/).

.PHONY: rv32emu-stage
rv32emu-stage: $(RV32EMU_DIR)/.spike-e-staged

$(RV32EMU_DIR)/.spike-e-staged: wasm.mk configs/wasm_spike_e.config $(UPSTREAM_RV32EMU)/Makefile
	@mkdir -p "$(RV32EMU_DIR)"
	rsync -a --delete \
	    --exclude=build/ \
	    --exclude=build-native/ \
	    --exclude=build-aarch64/ \
	    --exclude=build-wasm/ \
	    --exclude=demo/ \
	    --exclude=.config \
	    --exclude=.spike-e-staged \
	    "$(UPSTREAM_RV32EMU)/" "$(RV32EMU_DIR)/"
	cp wasm.mk "$(RV32EMU_DIR)/mk/wasm.mk"
	cp configs/wasm_spike_e.config "$(RV32EMU_DIR)/.config"
	touch "$(RV32EMU_DIR)/.spike-e-staged"

# ── stage the cart ELFs into the build dir so emcc can embed them ──────────

.PHONY: cart-stage
cart-stage: rv32emu-stage $(CARTS:%=$(RV32EMU_DIR)/$(WASM_OUT)/%)

$(RV32EMU_DIR)/$(WASM_OUT)/%.elf: $(CART_SRC_DIR)/%.elf
	@mkdir -p "$(RV32EMU_DIR)/$(WASM_OUT)"
	cp "$<" "$@"

# ── WASM build ─────────────────────────────────────────────────────────────

.PHONY: wasm
wasm: $(WASM_BIN)

$(WASM_BIN): emsdk cart-stage
	cd "$(RV32EMU_DIR)" && \
	    PATH="$(EM_PATH):$$PATH" \
	    EMSDK="$(EMSDK_DIR)" EMSDK_NODE="$(EMSDK_DIR)/node/22.16.0_64bit/bin/node" \
	    $(MAKE) OUT=$(WASM_OUT)

# ── Node smoke-test ────────────────────────────────────────────────────────
# Exercises rv32emu.js under Node — same WASM bytes that the browser will run.
# web/node_run.cjs swaps in our Module hooks so we can call run_user()
# without touching user-pre.js.

BENCH ?= lua_cart_doom_tick.elf
NODE  := $(EMSDK_DIR)/node/22.16.0_64bit/bin/node

.PHONY: node-test
node-test: wasm web/node_run.cjs
	@cp web/node_run.cjs "$(RV32EMU_DIR)/$(WASM_OUT)/"
	@echo "=== Node + WASM rv32emu running $(BENCH) ==="
	cd "$(RV32EMU_DIR)/$(WASM_OUT)" && \
	    "$(NODE)" node_run.cjs "$(BENCH)"

.PHONY: node-test-all
node-test-all: wasm web/node_run.cjs
	@cp web/node_run.cjs "$(RV32EMU_DIR)/$(WASM_OUT)/"
	@mkdir -p baselines
	@for c in $(CARTS); do \
	    echo "=== $$c ==="; \
	    (cd "$(RV32EMU_DIR)/$(WASM_OUT)" && "$(NODE)" node_run.cjs "$$c") \
	        | tee "baselines/$${c%.elf}.node-wasm.txt" | tail -1; \
	done

# ── desktop dev server with the spike_e harness ────────────────────────────

DEMO_DIR := demo
DEMO_PORT ?= 8000
DEMO_IP   ?= 127.0.0.1

.PHONY: stage-web
stage-web: wasm web/spike_e.html web/spike_e.js web/spike_e_worker.js
	@rm -rf "$(DEMO_DIR)"
	@mkdir -p "$(DEMO_DIR)"
	cp web/spike_e.html        "$(DEMO_DIR)/index.html"
	cp web/spike_e.js          "$(DEMO_DIR)/"
	cp web/spike_e_worker.js   "$(DEMO_DIR)/"
	cp "$(RV32EMU_DIR)/$(WASM_OUT)/rv32emu.js"        "$(DEMO_DIR)/"
	cp "$(RV32EMU_DIR)/$(WASM_OUT)/rv32emu.wasm"      "$(DEMO_DIR)/"
	cp "$(RV32EMU_DIR)/$(WASM_OUT)/rv32emu.worker.js" "$(DEMO_DIR)/" 2>/dev/null || true
	cp "$(RV32EMU_DIR)/$(WASM_OUT)/elf_list.js"       "$(DEMO_DIR)/"

.PHONY: start-web
start-web: stage-web
	@echo "Serving $(DEMO_DIR) on http://$(DEMO_IP):$(DEMO_PORT)/"
	cd "$(DEMO_DIR)" && python3 -m http.server --bind $(DEMO_IP) $(DEMO_PORT)

# Bind to all interfaces — convenience for testing on a phone via LAN.
.PHONY: start-web-lan
start-web-lan: stage-web
	@echo "Serving $(DEMO_DIR) on http://0.0.0.0:$(DEMO_PORT)/ (LAN)"
	cd "$(DEMO_DIR)" && python3 -m http.server --bind 0.0.0.0 $(DEMO_PORT)

# ── headless Chrome capture (puppeteer-core driving system Chrome) ─────────

CHROME_PORT  ?= 8765
CHROME_CARTS ?= lua_cart_doom_tick.elf,lua_cart_entity_update.elf,doom_tick_c.elf,lua_cart_binarytrees.elf

# One-off install of puppeteer-core (no Chromium bundle — we drive system Chrome).
node_modules/puppeteer-core/package.json:
	npm install --no-fund --no-audit puppeteer-core@22

.PHONY: chrome-bench
chrome-bench: stage-web node_modules/puppeteer-core/package.json
	@mkdir -p baselines
	@# Spin up a transient http server on $(CHROME_PORT) for the duration.
	@(cd "$(DEMO_DIR)" && python3 -m http.server --bind 127.0.0.1 $(CHROME_PORT) >/dev/null 2>&1 & \
	  echo $$! > /tmp/spike-e-chrome-http.pid; \
	  sleep 0.5; \
	  node web/run_chrome.cjs --port $(CHROME_PORT) --cart "$(CHROME_CARTS)" \
	      --out baselines/chrome-desktop.json; \
	  kill $$(cat /tmp/spike-e-chrome-http.pid) 2>/dev/null || true; \
	  rm -f /tmp/spike-e-chrome-http.pid)

# ── housekeeping ───────────────────────────────────────────────────────────

.PHONY: clean
clean:
	$(RM) -r "$(RV32EMU_DIR)" "$(DEMO_DIR)"

.PHONY: distclean
distclean: clean
	$(RM) -r "$(EMSDK_DIR)" "$(CART_SRC_DIR)"
