## Spike Q — Lua+Rust hybrid binding: rv32 path and WASM call-on-demand.
##
## Stages:
##   Stage 1 — rv32 Rust cart_lua_modules + .lua_exports section gate
##   Stage 2 — rv32emu_call_fn call-on-demand: add32(3,4) and addf(1,2) gates
##   Stage 3 — WASM: Lua-direct + rv32emu call-on-demand trampoline
##   Stage 4 — Determinism: rv32/arm64 = rv32/amd64 = WASM/Node digest diff
##   Stage 5 — Cross-host matrix + write-up
##
## Composite targets:
##   make docker-build             arm64 + amd64 Docker images
##   make docker-build-arm64
##   make docker-build-amd64
##   make stage-1                  rv32 Rust binding gate (both hosts)
##   make stage-2                  call-on-demand gate (both hosts)
##   make stage-3-wasm-build       Emscripten WASM module (host-side)
##   make stage-3-wasm-node        Run WASM module under Node
##   make stage-4-determinism      diff rv32 and WASM digest streams
##   make stage-5-matrix           full gate summary
##   make all                      all stages in order; exits 0 iff all pass
##   make clean

DOCKER_IMAGE_ARM64 ?= fc32-spike-q-arm64
DOCKER_IMAGE_AMD64 ?= fc32-spike-q-amd64
SPIKE_K_IMAGE_ARM  ?= fc32-spike-k-arm64
SPIKE_K_IMAGE_AMD  ?= fc32-spike-k-amd64
SPIKE_I_IMAGE_ARM  ?= fc32-spike-i-arm64
SPIKE_I_IMAGE_AMD  ?= fc32-spike-i-amd64

RV32EMU_BIN := /spike-a/rv32emu/build/rv32emu
LIB_DIR     := /spike-i/lib
ELFS_DIR    := /spike-q/elfs

DIGESTS_DIR := digests

# ── Emscripten (reuse spike-e's emsdk) ───────────────────────────────────────
SPIKE_E_DIR  := $(abspath ../spike-e)
EMSDK_DIR    := $(SPIKE_E_DIR)/emsdk
EMCC         := $(EMSDK_DIR)/upstream/emscripten/emcc
EM_PATH      := $(EMSDK_DIR)/upstream/emscripten:$(EMSDK_DIR):$(EMSDK_DIR)/node/22.16.0_64bit/bin
NODE         := $(EMSDK_DIR)/node/22.16.0_64bit/bin/node

# Spike I Lua source path (from the spike-i image, extracted on demand)
SPIKE_I_LUA_SRC := $(abspath ../spike-i)/wasm/lua-direct

# Spike Q WASM output directory (host-side build)
WASM_DIR     := wasm-build
WASM_OUT     := $(WASM_DIR)/out

# Lua source files for WASM build (mirrors spike-i Stage 5)
LD_LUA_SRCS := lapi.c lcode.c lctype.c ldebug.c ldo.c ldump.c lfunc.c lgc.c \
               llex.c lmem.c lobject.c lopcodes.c lparser.c lstate.c lstring.c \
               ltable.c ltm.c lundump.c lvm.c lzio.c lauxlib.c lbaselib.c \
               lmathlib.c lstrlib.c ltablib.c

# Docker platform for arm64 host; override to linux/amd64 on amd64 hosts.
DOCKER_PLATFORM ?= linux/arm64

# ─── Docker build ─────────────────────────────────────────────────────────────

.PHONY: docker-build docker-build-arm64 docker-build-amd64
docker-build: docker-build-arm64 docker-build-amd64

docker-build-arm64:
	docker tag fc32-spike-i:latest fc32-spike-i-arm64 2>/dev/null || true
	DOCKER_BUILDKIT=1 docker build --platform linux/arm64 \
	    --build-arg SPIKE_K_IMAGE=$(SPIKE_K_IMAGE_ARM) \
	    --build-arg SPIKE_I_IMAGE=$(SPIKE_I_IMAGE_ARM) \
	    --build-context spike-i=../spike-i \
	    -t $(DOCKER_IMAGE_ARM64) .

docker-build-amd64:
	DOCKER_BUILDKIT=1 docker build --platform linux/amd64 \
	    --build-arg SPIKE_K_IMAGE=$(SPIKE_K_IMAGE_AMD) \
	    --build-arg SPIKE_I_IMAGE=$(SPIKE_I_IMAGE_AMD) \
	    --build-context spike-i=../spike-i \
	    -t $(DOCKER_IMAGE_AMD64) .

.PHONY: shell-arm64 shell-amd64
shell-arm64: docker-build-arm64
	docker run --rm -it --platform linux/arm64 $(DOCKER_IMAGE_ARM64) /bin/bash
shell-amd64: docker-build-amd64
	docker run --rm -it --platform linux/amd64 $(DOCKER_IMAGE_AMD64) /bin/bash

$(DIGESTS_DIR):
	mkdir -p $@

# ─── ELF hash gate ─────────────────────────────────────────────────────────────

ELF_HASH_ARM64 := $(DIGESTS_DIR)/elfs.arm64.sha256
ELF_HASH_AMD64 := $(DIGESTS_DIR)/elfs.amd64.sha256

$(ELF_HASH_ARM64): docker-build-arm64 | $(DIGESTS_DIR)
	docker run --rm --platform linux/arm64 $(DOCKER_IMAGE_ARM64) \
	    sh -c 'cd $(ELFS_DIR) && sha256sum *.elf' | sort > $@

$(ELF_HASH_AMD64): docker-build-amd64 | $(DIGESTS_DIR)
	docker run --rm --platform linux/amd64 $(DOCKER_IMAGE_AMD64) \
	    sh -c 'cd $(ELFS_DIR) && sha256sum *.elf' | sort > $@

.PHONY: elf-bytes
elf-bytes: $(ELF_HASH_ARM64) $(ELF_HASH_AMD64)
	@echo "=== ELF byte-identity gate ==="
	@diff -q $(ELF_HASH_ARM64) $(ELF_HASH_AMD64) \
	    && echo "ELF-BYTES PASS" \
	    || (echo "ELF-BYTES FAIL (cross-host ELF mismatch is expected — informational only)"; true)

# ─── Stage 1: rv32 Rust binding gate ──────────────────────────────────────────
#
# Run the hybrid Lua+Rust cart inside rv32emu for 10 frames on each host.
# Gate: FRAME lines present, add=7 mul=12 on both hosts, outputs byte-equal.

STAGE1_DIGEST_ARM64 := $(DIGESTS_DIR)/rv32-arm64.digest
STAGE1_DIGEST_AMD64 := $(DIGESTS_DIR)/rv32-amd64.digest
EXPORTS_ARM64       := $(DIGESTS_DIR)/lua-exports-arm64.txt
EXPORTS_AMD64       := $(DIGESTS_DIR)/lua-exports-amd64.txt

$(EXPORTS_ARM64): docker-build-arm64 | $(DIGESTS_DIR)
	docker run --rm --platform linux/arm64 $(DOCKER_IMAGE_ARM64) \
	    cat $(ELFS_DIR)/lua_exports_dump.txt > $@

$(EXPORTS_AMD64): docker-build-amd64 | $(DIGESTS_DIR)
	docker run --rm --platform linux/amd64 $(DOCKER_IMAGE_AMD64) \
	    cat $(ELFS_DIR)/lua_exports_dump.txt > $@

$(STAGE1_DIGEST_ARM64): docker-build-arm64 | $(DIGESTS_DIR)
	docker run --rm --platform linux/arm64 $(DOCKER_IMAGE_ARM64) \
	    $(RV32EMU_BIN) -L $(LIB_DIR) $(ELFS_DIR)/cart_hybrid.elf 2>/dev/null \
	    | grep '^FRAME' > $@

$(STAGE1_DIGEST_AMD64): docker-build-amd64 | $(DIGESTS_DIR)
	docker run --rm --platform linux/amd64 $(DOCKER_IMAGE_AMD64) \
	    $(RV32EMU_BIN) -L $(LIB_DIR) $(ELFS_DIR)/cart_hybrid.elf 2>/dev/null \
	    | grep '^FRAME' > $@

.PHONY: stage-1-lua-exports-check-arm64 stage-1-lua-exports-check-amd64
stage-1-lua-exports-check-arm64: $(EXPORTS_ARM64)
	@echo "=== Stage 1 .lua_exports gate — arm64 ==="
	@grep -q "mylib" $(EXPORTS_ARM64) \
	    && echo "LUA-EXPORTS-ARM64 PASS" \
	    || (echo "LUA-EXPORTS-ARM64 FAIL"; cat $(EXPORTS_ARM64); exit 1)

stage-1-lua-exports-check-amd64: $(EXPORTS_AMD64)
	@echo "=== Stage 1 .lua_exports gate — amd64 ==="
	@grep -q "mylib" $(EXPORTS_AMD64) \
	    && echo "LUA-EXPORTS-AMD64 PASS" \
	    || (echo "LUA-EXPORTS-AMD64 FAIL"; cat $(EXPORTS_AMD64); exit 1)

.PHONY: stage-1-rv32-arm64 stage-1-rv32-amd64 stage-1-diff stage-1
stage-1-rv32-arm64: $(STAGE1_DIGEST_ARM64)
	@echo "=== Stage 1 rv32 run — arm64 ==="
	@grep -q "add=7" $(STAGE1_DIGEST_ARM64) && grep -q "mul=12" $(STAGE1_DIGEST_ARM64) \
	    && echo "RV32-BINDING-ARM64 PASS" \
	    || (echo "RV32-BINDING-ARM64 FAIL"; cat $(STAGE1_DIGEST_ARM64); exit 1)

stage-1-rv32-amd64: $(STAGE1_DIGEST_AMD64)
	@echo "=== Stage 1 rv32 run — amd64 ==="
	@grep -q "add=7" $(STAGE1_DIGEST_AMD64) && grep -q "mul=12" $(STAGE1_DIGEST_AMD64) \
	    && echo "RV32-BINDING-AMD64 PASS" \
	    || (echo "RV32-BINDING-AMD64 FAIL"; cat $(STAGE1_DIGEST_AMD64); exit 1)

stage-1-diff: $(STAGE1_DIGEST_ARM64) $(STAGE1_DIGEST_AMD64)
	@echo "=== Stage 1 cross-host digest diff ==="
	@diff -q $(STAGE1_DIGEST_ARM64) $(STAGE1_DIGEST_AMD64) \
	    && echo "RV32-CROSS-HOST PASS" \
	    || (echo "RV32-CROSS-HOST FAIL"; diff $(STAGE1_DIGEST_ARM64) $(STAGE1_DIGEST_AMD64); exit 1)

stage-1: stage-1-lua-exports-check-arm64 stage-1-lua-exports-check-amd64 \
         stage-1-rv32-arm64 stage-1-rv32-amd64 stage-1-diff
	@echo "=== Spike Q — Stage 1 PASS ==="

# ─── Stage 2: rv32emu_call_fn call-on-demand gate ─────────────────────────────
#
# Run the standalone harness.  Gate: RESULT add32 00000007 and RESULT addf 40400000.

STAGE2_ARM64 := $(DIGESTS_DIR)/call-on-demand-arm64.txt
STAGE2_AMD64 := $(DIGESTS_DIR)/call-on-demand-amd64.txt

$(STAGE2_ARM64): docker-build-arm64 | $(DIGESTS_DIR)
	docker run --rm --platform linux/arm64 $(DOCKER_IMAGE_ARM64) \
	    $(ELFS_DIR)/test_call_on_demand $(ELFS_DIR)/test_funcs.elf 2>/dev/null \
	    > $@

$(STAGE2_AMD64): docker-build-amd64 | $(DIGESTS_DIR)
	docker run --rm --platform linux/amd64 $(DOCKER_IMAGE_AMD64) \
	    $(ELFS_DIR)/test_call_on_demand $(ELFS_DIR)/test_funcs.elf 2>/dev/null \
	    > $@

.PHONY: stage-2-call-on-demand-arm64 stage-2-call-on-demand-amd64 stage-2

stage-2-call-on-demand-arm64: $(STAGE2_ARM64)
	@echo "=== Stage 2 call-on-demand gate — arm64 ==="
	@grep -q "RESULT add32 00000007" $(STAGE2_ARM64) \
	    && grep -q "RESULT addf  40400000" $(STAGE2_ARM64) \
	    && echo "CALL-ON-DEMAND-ARM64 PASS" \
	    || (echo "CALL-ON-DEMAND-ARM64 FAIL"; cat $(STAGE2_ARM64); exit 1)

stage-2-call-on-demand-amd64: $(STAGE2_AMD64)
	@echo "=== Stage 2 call-on-demand gate — amd64 ==="
	@grep -q "RESULT add32 00000007" $(STAGE2_AMD64) \
	    && grep -q "RESULT addf  40400000" $(STAGE2_AMD64) \
	    && echo "CALL-ON-DEMAND-AMD64 PASS" \
	    || (echo "CALL-ON-DEMAND-AMD64 FAIL"; cat $(STAGE2_AMD64); exit 1)

stage-2: stage-2-call-on-demand-arm64 stage-2-call-on-demand-amd64
	@echo "=== Spike Q — Stage 2 PASS ==="

# ─── Stage 3: WASM Lua-direct + rv32emu trampoline (host-side build) ─────────
#
# Stages rv32emu sources locally, applies both the multi-dynload and call_fn
# patches, then compiles everything into a single WASM module via emcc.
# Requires spike-e's emsdk: run `make -C ../spike-e emsdk` first.

SPIKE_A_DIR    := $(abspath ../spike-a)
RV32EMU_SRC    := $(SPIKE_A_DIR)/rv32emu

WASM_RUST_ELF  := $(WASM_DIR)/rust_cart.elf
WASM_JS        := $(WASM_OUT)/consolelua-hybrid.js
WASM_CART_LUA  := $(WASM_DIR)/cart_lua_bytes.inc
WASM_RV32EMU   := $(WASM_DIR)/rv32emu  # local rv32emu copy for WASM build

# rv32emu WASM-build config (minimal: interpreter-only, user-mode, no JIT/SDL)
WASM_RV32EMU_CONFIG_SRC := $(abspath cases/wasm_trampoline/wasm_rv32emu.config)

# Step 1: stage all build inputs (Lua sources, rv32emu sources, rust_cart.elf)
$(WASM_DIR)/.staged: docker-build-arm64 $(WASM_RV32EMU_CONFIG_SRC)
	@mkdir -p $(WASM_DIR)/lua $(WASM_OUT)
	docker run --rm --platform $(DOCKER_PLATFORM) $(DOCKER_IMAGE_ARM64) \
	    cat $(ELFS_DIR)/rust_cart.elf > $(WASM_RUST_ELF)
	docker run --rm --platform $(DOCKER_PLATFORM) $(DOCKER_IMAGE_ARM64) \
	    tar -C /spike-i/lua -cf - src \
	    | tar -C $(WASM_DIR)/lua -xf -
	@mkdir -p $(WASM_RV32EMU)
	rsync -a --delete \
	    --exclude=build/ --exclude=build-native/ --exclude=build-wasm/ \
	    --exclude=.config --exclude=demo/ \
	    $(RV32EMU_SRC)/ $(WASM_RV32EMU)/
	cp $(WASM_RV32EMU_CONFIG_SRC) $(WASM_RV32EMU)/.config
	cd $(WASM_RV32EMU) && python3 $(abspath ../spike-i/patches/apply-multi-dynload.py)
	cd $(WASM_RV32EMU) && python3 $(abspath patches/apply_call_fn.py)
	@touch $@

# Step 2: compile Lua bytecode (reuse spike-q's Docker image for the host luac)
$(WASM_CART_LUA): docker-build-arm64 $(WASM_DIR)/.staged
	docker run --rm --platform $(DOCKER_PLATFORM) -v $(abspath $(WASM_DIR)):/wasm \
	    $(DOCKER_IMAGE_ARM64) \
	    bash -c 'luac -o /wasm/cart.luac /spike-q/cases/rv32_binding/cart.lua && \
	             xxd -i /wasm/cart.luac | sed "s/^unsigned.*//" | sed "/^$$/d" \
	                 | sed "s/.*{//" | sed "s/}.*$$//" > /wasm/cart_lua_bytes.inc'

# rv32emu WASM source files — same as spike-i Stage 4 (interpreter-only, user-mode).
# call_fn.c is added by the apply_call_fn.py patch.
WASM_RV32EMU_SRCS := $(WASM_RV32EMU)/src/map.c \
                      $(WASM_RV32EMU)/src/utils.c \
                      $(WASM_RV32EMU)/src/decode.c \
                      $(WASM_RV32EMU)/src/io.c \
                      $(WASM_RV32EMU)/src/syscall.c \
                      $(WASM_RV32EMU)/src/call_fn.c \
                      $(WASM_RV32EMU)/src/emulate.c \
                      $(WASM_RV32EMU)/src/riscv.c \
                      $(WASM_RV32EMU)/src/log.c \
                      $(WASM_RV32EMU)/src/elf.c \
                      $(WASM_RV32EMU)/src/cache.c \
                      $(WASM_RV32EMU)/src/mpool.c \
                      $(WASM_RV32EMU)/src/em_runtime.c \
                      $(WASM_RV32EMU)/src/main.c

# Step 3: build the WASM module using emcc
$(WASM_JS): $(WASM_DIR)/.staged $(WASM_CART_LUA) \
            cases/wasm_trampoline/libconsolelua_wasm_hybrid.c \
            $(SPIKE_I_LUA_SRC)/libconsole_wasm.c \
            $(SPIKE_I_LUA_SRC)/wasm_main.c
	@if [ ! -x "$(EMCC)" ]; then \
	    echo "Emscripten missing — run: make -C $(SPIKE_E_DIR) emsdk"; exit 1; \
	fi
	@mkdir -p $(WASM_OUT)
	@cp $(WASM_RUST_ELF) $(WASM_OUT)/rust_cart.elf
	cd $(WASM_DIR)/lua && PATH="$(EM_PATH):$$PATH" EMSDK="$(EMSDK_DIR)" \
	    "$(EMCC)" -O2 -DLUA_32BITS \
	    -include $(abspath $(WASM_RV32EMU)/src/common.h) \
	    -Isrc \
	    -I$(abspath $(WASM_RV32EMU)/src) \
	    $(abspath $(SPIKE_I_LUA_SRC)/libconsole_wasm.c) \
	    $(abspath $(SPIKE_I_LUA_SRC)/wasm_main.c) \
	    $(abspath cases/wasm_trampoline/libconsolelua_wasm_hybrid.c) \
	    $(addprefix src/,$(LD_LUA_SRCS)) \
	    $(WASM_RV32EMU_SRCS) \
	    -sEXIT_RUNTIME=1 -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=128MB \
	    --preload-file $(abspath $(WASM_OUT)/rust_cart.elf)@/rust_cart.elf \
	    -o $(abspath $@)

.PHONY: stage-3-wasm-build
stage-3-wasm-build: $(WASM_JS)

WASM_DIGEST := $(DIGESTS_DIR)/wasm-node.digest
WASM_OVERHEAD := $(DIGESTS_DIR)/wasm-overhead.txt

$(WASM_DIGEST): $(WASM_JS) | $(DIGESTS_DIR)
	cd $(WASM_OUT) && "$(NODE)" consolelua-hybrid.js 2>/dev/null \
	    | grep '^FRAME' > $(abspath $@)

$(WASM_OVERHEAD): $(WASM_JS) | $(DIGESTS_DIR)
	cd $(WASM_OUT) && "$(NODE)" consolelua-hybrid.js 2>/dev/null \
	    | grep '^OVERHEAD' > $(abspath $@)

.PHONY: stage-3-wasm-node stage-3
stage-3-wasm-node: $(WASM_DIGEST) $(WASM_OVERHEAD)
	@echo "=== Stage 3 WASM Node run ==="
	@test -s $(WASM_DIGEST) \
	    && echo "WASM-NODE PASS" \
	    || (echo "WASM-NODE FAIL (no FRAME output)"; exit 1)
	@grep -q "add=7" $(WASM_DIGEST) && grep -q "mul=12" $(WASM_DIGEST) \
	    && echo "WASM-VALUES PASS" \
	    || (echo "WASM-VALUES FAIL"; cat $(WASM_DIGEST); exit 1)
	@cat $(WASM_OVERHEAD) || true

stage-3: stage-3-wasm-build stage-3-wasm-node
	@echo "=== Spike Q — Stage 3 PASS ==="

# ─── Stage 4: Determinism cross-check ─────────────────────────────────────────

.PHONY: stage-4-determinism stage-4
stage-4-determinism: $(STAGE1_DIGEST_ARM64) $(STAGE1_DIGEST_AMD64) $(WASM_DIGEST)
	@echo "=== Stage 4 rv32/arm64 vs rv32/amd64 ==="
	@diff -q $(STAGE1_DIGEST_ARM64) $(STAGE1_DIGEST_AMD64) \
	    && echo "DETERMINISM-RV32 PASS" \
	    || (echo "DETERMINISM-RV32 FAIL"; diff $(STAGE1_DIGEST_ARM64) $(STAGE1_DIGEST_AMD64); exit 1)
	@echo "=== Stage 4 rv32/arm64 vs WASM/Node ==="
	@diff -q $(STAGE1_DIGEST_ARM64) $(WASM_DIGEST) \
	    && echo "DETERMINISM-WASM PASS" \
	    || (echo "DETERMINISM-WASM FAIL"; \
	        echo "-- rv32/arm64 --"; cat $(STAGE1_DIGEST_ARM64); \
	        echo "-- WASM/Node --";  cat $(WASM_DIGEST); exit 1)

stage-4: stage-4-determinism
	@echo "=== Spike Q — Stage 4 PASS ==="

# ─── Stage 5: Cross-host matrix ───────────────────────────────────────────────

.PHONY: stage-5-matrix stage-5
stage-5-matrix: stage-1 stage-2 stage-3 stage-4
	@echo ""
	@echo "=== Stage 5 Cross-Host Matrix ==="
	@echo "  Configuration              Stage  arm64  amd64  WASM/Node  Gate"
	@echo "  rv32 Rust binding              1    A      B       —        A=B=7/12"
	@echo "  .lua_exports section           1    ✓      ✓       —        present+mylib"
	@echo "  call-on-demand add32           2    A      B       —        A=B=7"
	@echo "  call-on-demand addf            2    A      B       —        A=B=0x40400000"
	@echo "  WASM trampoline                3    —      —       C        C=A=B"
	@echo "  determinism                    4    A      B       C        A=B=C"
	@echo "STAGE-5-MATRIX PASS"

stage-5: stage-5-matrix
	@echo "=== Spike Q — Stage 5 PASS ==="

# ─── all / clean ──────────────────────────────────────────────────────────────

.PHONY: all
all: stage-1 stage-2 stage-3 stage-4 stage-5
	@echo "=== Spike Q — Stages 1-5 ALL PASS ==="

.PHONY: clean
clean:
	$(RM) -r $(DIGESTS_DIR) $(WASM_DIR)
	-docker image rm $(DOCKER_IMAGE_ARM64) $(DOCKER_IMAGE_AMD64) 2>/dev/null || true
