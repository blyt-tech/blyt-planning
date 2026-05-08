## Spike O — Rust cart on riscv32imafc-unknown-none-elf + ADR-0108 SDK validation.
##
## Stages:
##   Stage 1 — toolchain proof: stub cart loads in rv32emu (sentinel byte)
##   Stage 2 — ABI verification: f32 argument arrives as 3f000000 in stub
##   Stage 3 — SDK layer + toy cart digest gate vs C reference
##   Stage 4 — packer–Cargo integration (build.rs → OUT_DIR → include!())
##   Stage 5 — cross-host matrix + compile-fail guard tests
##
## Composite targets:
##   make docker-build                arm64 + amd64
##   make docker-build-arm64          arm64 only
##   make docker-build-amd64          amd64 only
##   make stage-1                     loader smoke test (both hosts)
##   make stage-2-abi                 ABI float witness (both hosts)
##   make stage-3-digest              SDK + digest gate (both hosts)
##   make stage-4-packer              packer–Cargo integration gate
##   make stage-5                     cross-host matrix + compile-fail guards
##   make all                         all stages; exits 0 iff everything passes
##   make clean                       remove host-side artefacts and images

DOCKER_IMAGE_ARM64 ?= fc32-spike-o-arm64
DOCKER_IMAGE_AMD64 ?= fc32-spike-o-amd64
SPIKE_K_IMAGE_ARM  ?= fc32-spike-k-arm64
SPIKE_K_IMAGE_AMD  ?= fc32-spike-k-amd64
SPIKE_I_IMAGE      ?= fc32-spike-i

RV32EMU_BIN := /spike-a/rv32emu/build/rv32emu
ELFS_DIR    := /spike-o/elfs
LIB_DIR     := /spike-o/lib

DIGESTS_DIR := digests
BUFFERS_DIR := buffers

# ─── docker build ─────────────────────────────────────────────────────────────

.PHONY: docker-build docker-build-arm64 docker-build-amd64
docker-build: docker-build-arm64 docker-build-amd64

docker-build-arm64:
	$(MAKE) -C ../spike-k docker-build-arm64
	DOCKER_BUILDKIT=1 docker build --platform linux/arm64 \
	    --build-arg SPIKE_K_IMAGE=$(SPIKE_K_IMAGE_ARM) \
	    --build-context spike-k=../spike-k \
	    --build-context spike-d=../spike-d \
	    --build-context spike-i=../spike-i \
	    -t $(DOCKER_IMAGE_ARM64) .

docker-build-amd64:
	$(MAKE) -C ../spike-k docker-build-amd64
	DOCKER_BUILDKIT=1 docker build --platform linux/amd64 \
	    --build-arg SPIKE_K_IMAGE=$(SPIKE_K_IMAGE_AMD) \
	    --build-context spike-k=../spike-k \
	    --build-context spike-d=../spike-d \
	    --build-context spike-i=../spike-i \
	    -t $(DOCKER_IMAGE_AMD64) .

.PHONY: shell-arm64 shell-amd64
shell-arm64: docker-build-arm64
	docker run --rm -it --platform linux/arm64 $(DOCKER_IMAGE_ARM64) /bin/bash

shell-amd64: docker-build-amd64
	docker run --rm -it --platform linux/amd64 $(DOCKER_IMAGE_AMD64) /bin/bash

$(DIGESTS_DIR):
	mkdir -p $@
$(BUFFERS_DIR):
	mkdir -p $@

# ─── Stage 1: loader smoke test ───────────────────────────────────────────────
# The stage-1 cart emits "INIT\n" via SYS_write, confirming fc_cart_init ran
# and rv32emu's multi-lib dynamic loader resolved libconsole_spike_o.so.

.PHONY: stage-1-load-arm64 stage-1-load-amd64 stage-1-load stage-1

stage-1-load-arm64: docker-build-arm64
	@echo "=== Stage 1 loader smoke test — arm64 ==="
	@docker run --rm --platform linux/arm64 $(DOCKER_IMAGE_ARM64) \
	    $(RV32EMU_BIN) -L $(LIB_DIR)/build $(ELFS_DIR)/rust_cart_stage1.elf 2>/dev/null \
	    | grep -q 'INIT' \
	    && echo "STAGE-1-LOAD-ARM64 PASS" \
	    || (echo "STAGE-1-LOAD-ARM64 FAIL"; exit 1)

stage-1-load-amd64: docker-build-amd64
	@echo "=== Stage 1 loader smoke test — amd64 ==="
	@docker run --rm --platform linux/amd64 $(DOCKER_IMAGE_AMD64) \
	    $(RV32EMU_BIN) -L $(LIB_DIR)/build $(ELFS_DIR)/rust_cart_stage1.elf 2>/dev/null \
	    | grep -q 'INIT' \
	    && echo "STAGE-1-LOAD-AMD64 PASS" \
	    || (echo "STAGE-1-LOAD-AMD64 FAIL"; exit 1)

stage-1-load: stage-1-load-arm64 stage-1-load-amd64
stage-1: stage-1-load
	@echo "=== Spike O — Stage 1 PASS ==="

# ─── Stage 2: ABI verification ────────────────────────────────────────────────
# The stage-2 cart calls blyt32_audio_sfx_set_volume(0, 0.5f) via raw extern "C".
# The stub emits "SET_VOL voice=00000000 vol=3f000000\n".
# Gate: vol field = "3f000000" (IEEE 754 for 0.5f under ilp32f hard-float ABI).

ABI_ARM64 := $(DIGESTS_DIR)/abi-arm64.txt
ABI_AMD64 := $(DIGESTS_DIR)/abi-amd64.txt

$(ABI_ARM64): docker-build-arm64 | $(DIGESTS_DIR)
	docker run --rm --platform linux/arm64 $(DOCKER_IMAGE_ARM64) \
	    $(RV32EMU_BIN) -L $(LIB_DIR)/build $(ELFS_DIR)/rust_cart_stage2.elf 2>/dev/null \
	    | grep 'SET_VOL' > $@ || true

$(ABI_AMD64): docker-build-amd64 | $(DIGESTS_DIR)
	docker run --rm --platform linux/amd64 $(DOCKER_IMAGE_AMD64) \
	    $(RV32EMU_BIN) -L $(LIB_DIR)/build $(ELFS_DIR)/rust_cart_stage2.elf 2>/dev/null \
	    | grep 'SET_VOL' > $@ || true

.PHONY: stage-2-abi-arm64 stage-2-abi-amd64 stage-2-diff stage-2-abi stage-2

stage-2-abi-arm64: $(ABI_ARM64)
	@echo "=== Stage 2 ABI gate — arm64 ==="
	@grep -q '3f000000' $(ABI_ARM64) \
	    && echo "ABI-ARM64 PASS (vol=3f000000)" \
	    || (echo "ABI-ARM64 FAIL"; cat $(ABI_ARM64); exit 1)

stage-2-abi-amd64: $(ABI_AMD64)
	@echo "=== Stage 2 ABI gate — amd64 ==="
	@grep -q '3f000000' $(ABI_AMD64) \
	    && echo "ABI-AMD64 PASS (vol=3f000000)" \
	    || (echo "ABI-AMD64 FAIL"; cat $(ABI_AMD64); exit 1)

stage-2-diff: stage-2-abi-arm64 stage-2-abi-amd64
	@echo "=== Stage 2 cross-host ABI diff ==="
	@diff -q $(ABI_ARM64) $(ABI_AMD64) \
	    && echo "ABI CROSS-HOST PASS" \
	    || (echo "ABI CROSS-HOST FAIL (host divergence)"; diff $(ABI_ARM64) $(ABI_AMD64); exit 1)

stage-2-abi: stage-2-abi-arm64 stage-2-abi-amd64
stage-2: stage-2-diff
	@echo "=== Spike O — Stage 2 PASS ==="

# ─── Stage 3: SDK layer + digest gate ─────────────────────────────────────────
# Four-way gate: (Rust arm64) = (C arm64) = (Rust amd64) = (C amd64).

RUST_DIGEST_ARM64 := $(DIGESTS_DIR)/rust-cart-arm64.digest
RUST_DIGEST_AMD64 := $(DIGESTS_DIR)/rust-cart-amd64.digest
CREF_DIGEST_ARM64 := $(DIGESTS_DIR)/c-ref-arm64.digest
CREF_DIGEST_AMD64 := $(DIGESTS_DIR)/c-ref-amd64.digest

$(RUST_DIGEST_ARM64): docker-build-arm64 | $(DIGESTS_DIR)
	docker run --rm --platform linux/arm64 $(DOCKER_IMAGE_ARM64) \
	    $(RV32EMU_BIN) -L $(LIB_DIR)/build $(ELFS_DIR)/rust_cart.elf 2>/dev/null \
	    | grep '^DIGEST' > $@

$(RUST_DIGEST_AMD64): docker-build-amd64 | $(DIGESTS_DIR)
	docker run --rm --platform linux/amd64 $(DOCKER_IMAGE_AMD64) \
	    $(RV32EMU_BIN) -L $(LIB_DIR)/build $(ELFS_DIR)/rust_cart.elf 2>/dev/null \
	    | grep '^DIGEST' > $@

$(CREF_DIGEST_ARM64): docker-build-arm64 | $(DIGESTS_DIR)
	docker run --rm --platform linux/arm64 $(DOCKER_IMAGE_ARM64) \
	    $(RV32EMU_BIN) -L $(LIB_DIR)/build $(ELFS_DIR)/c_ref_cart.elf 2>/dev/null \
	    | grep '^DIGEST' > $@

$(CREF_DIGEST_AMD64): docker-build-amd64 | $(DIGESTS_DIR)
	docker run --rm --platform linux/amd64 $(DOCKER_IMAGE_AMD64) \
	    $(RV32EMU_BIN) -L $(LIB_DIR)/build $(ELFS_DIR)/c_ref_cart.elf 2>/dev/null \
	    | grep '^DIGEST' > $@

.PHONY: stage-3-digest-arm64 stage-3-digest-amd64 stage-3-diff stage-3-digest stage-3

stage-3-digest-arm64: $(RUST_DIGEST_ARM64) $(CREF_DIGEST_ARM64)
	@echo "=== Stage 3 digest gate — arm64 ==="
	@diff -q $(RUST_DIGEST_ARM64) $(CREF_DIGEST_ARM64) \
	    && echo "DIGEST-ARM64 PASS (Rust == C reference)" \
	    || (echo "DIGEST-ARM64 FAIL"; diff $(RUST_DIGEST_ARM64) $(CREF_DIGEST_ARM64); exit 1)

stage-3-digest-amd64: $(RUST_DIGEST_AMD64) $(CREF_DIGEST_AMD64)
	@echo "=== Stage 3 digest gate — amd64 ==="
	@diff -q $(RUST_DIGEST_AMD64) $(CREF_DIGEST_AMD64) \
	    && echo "DIGEST-AMD64 PASS (Rust == C reference)" \
	    || (echo "DIGEST-AMD64 FAIL"; diff $(RUST_DIGEST_AMD64) $(CREF_DIGEST_AMD64); exit 1)

stage-3-diff: $(RUST_DIGEST_ARM64) $(RUST_DIGEST_AMD64) $(CREF_DIGEST_ARM64) $(CREF_DIGEST_AMD64)
	@echo "=== Stage 3 four-way digest diff ==="
	@diff -q $(RUST_DIGEST_ARM64) $(RUST_DIGEST_AMD64) \
	    && echo "Rust cart: arm64 == amd64" \
	    || (echo "Rust cart CROSS-HOST FAIL"; diff $(RUST_DIGEST_ARM64) $(RUST_DIGEST_AMD64); exit 1)
	@diff -q $(CREF_DIGEST_ARM64) $(CREF_DIGEST_AMD64) \
	    && echo "C reference: arm64 == amd64" \
	    || (echo "C reference CROSS-HOST FAIL"; diff $(CREF_DIGEST_ARM64) $(CREF_DIGEST_AMD64); exit 1)
	@diff -q $(RUST_DIGEST_ARM64) $(CREF_DIGEST_ARM64) \
	    && echo "FOUR-WAY DIGEST PASS (A=B=C=D)" \
	    || (echo "FOUR-WAY DIGEST FAIL"; exit 1)

stage-3-digest: stage-3-digest-arm64 stage-3-digest-amd64 stage-3-diff
stage-3: stage-3-digest
	@echo "=== Spike O — Stage 3 PASS ==="

# ─── Stage 4: packer–Cargo integration ────────────────────────────────────────
# Validates build.rs → stub_packer_rust → OUT_DIR → include!() chain.
# The packer-generated cart must produce the same digest as the hardcoded-
# constant cart (Stage 3), proving the constant values are identical.

PACKER_DIGEST_ARM64 := $(DIGESTS_DIR)/packer-rust-cart-arm64.digest
PACKER_DIGEST_AMD64 := $(DIGESTS_DIR)/packer-rust-cart-amd64.digest

$(PACKER_DIGEST_ARM64): docker-build-arm64 | $(DIGESTS_DIR)
	docker run --rm --platform linux/arm64 $(DOCKER_IMAGE_ARM64) \
	    $(RV32EMU_BIN) -L $(LIB_DIR)/build $(ELFS_DIR)/rust_cart.elf 2>/dev/null \
	    | grep '^DIGEST' > $@

$(PACKER_DIGEST_AMD64): docker-build-amd64 | $(DIGESTS_DIR)
	docker run --rm --platform linux/amd64 $(DOCKER_IMAGE_AMD64) \
	    $(RV32EMU_BIN) -L $(LIB_DIR)/build $(ELFS_DIR)/rust_cart.elf 2>/dev/null \
	    | grep '^DIGEST' > $@

.PHONY: stage-4-packer-arm64 stage-4-packer-amd64 stage-4-diff stage-4-packer stage-4

stage-4-packer-arm64: $(PACKER_DIGEST_ARM64) $(RUST_DIGEST_ARM64)
	@echo "=== Stage 4 packer integration gate — arm64 ==="
	@diff -q $(PACKER_DIGEST_ARM64) $(RUST_DIGEST_ARM64) \
	    && echo "PACKER-ARM64 PASS (packer-generated constants == hardcoded)" \
	    || (echo "PACKER-ARM64 FAIL"; diff $(PACKER_DIGEST_ARM64) $(RUST_DIGEST_ARM64); exit 1)

stage-4-packer-amd64: $(PACKER_DIGEST_AMD64) $(RUST_DIGEST_AMD64)
	@echo "=== Stage 4 packer integration gate — amd64 ==="
	@diff -q $(PACKER_DIGEST_AMD64) $(RUST_DIGEST_AMD64) \
	    && echo "PACKER-AMD64 PASS (packer-generated constants == hardcoded)" \
	    || (echo "PACKER-AMD64 FAIL"; diff $(PACKER_DIGEST_AMD64) $(RUST_DIGEST_AMD64); exit 1)

stage-4-diff: stage-4-packer-arm64 stage-4-packer-amd64
	@echo "STAGE-4 CROSS-HOST PASS"

stage-4-packer: stage-4-packer-arm64 stage-4-packer-amd64
stage-4: stage-4-diff
	@echo "=== Spike O — Stage 4 PASS ==="

# ─── Stage 5: cross-host matrix + compile-fail guards ─────────────────────────

GUARD_WB_ARM64 := $(DIGESTS_DIR)/guard-wrong-buffer-arm64.stderr
GUARD_WB_AMD64 := $(DIGESTS_DIR)/guard-wrong-buffer-amd64.stderr
GUARD_CH_ARM64 := $(DIGESTS_DIR)/guard-closure-handler-arm64.stderr
GUARD_CH_AMD64 := $(DIGESTS_DIR)/guard-closure-handler-amd64.stderr

$(GUARD_WB_ARM64): docker-build-arm64 | $(DIGESTS_DIR)
	docker run --rm --platform linux/arm64 $(DOCKER_IMAGE_ARM64) \
	    cat /spike-o/guard_wrong_buffer.stderr > $@

$(GUARD_WB_AMD64): docker-build-amd64 | $(DIGESTS_DIR)
	docker run --rm --platform linux/amd64 $(DOCKER_IMAGE_AMD64) \
	    cat /spike-o/guard_wrong_buffer.stderr > $@

$(GUARD_CH_ARM64): docker-build-arm64 | $(DIGESTS_DIR)
	docker run --rm --platform linux/arm64 $(DOCKER_IMAGE_ARM64) \
	    cat /spike-o/guard_closure_handler.stderr > $@

$(GUARD_CH_AMD64): docker-build-amd64 | $(DIGESTS_DIR)
	docker run --rm --platform linux/amd64 $(DOCKER_IMAGE_AMD64) \
	    cat /spike-o/guard_closure_handler.stderr > $@

.PHONY: stage-5-guard-wrong-buffer-arm64 stage-5-guard-wrong-buffer-amd64
.PHONY: stage-5-guard-closure-handler-arm64 stage-5-guard-closure-handler-amd64
.PHONY: stage-5-guard-wrong-buffer stage-5-guard-closure-handler
.PHONY: stage-5-matrix stage-5

stage-5-guard-wrong-buffer-arm64: $(GUARD_WB_ARM64)
	@echo "=== Stage 5 guard wrong-buffer — arm64 ==="
	@grep -q 'FieldHandle\|mismatched types\|type mismatch' $(GUARD_WB_ARM64) \
	    && echo "GUARD-WRONG-BUFFER-ARM64 PASS" \
	    || (echo "GUARD-WRONG-BUFFER-ARM64 FAIL (expected type error not found)"; \
	        cat $(GUARD_WB_ARM64); exit 1)

stage-5-guard-wrong-buffer-amd64: $(GUARD_WB_AMD64)
	@echo "=== Stage 5 guard wrong-buffer — amd64 ==="
	@grep -q 'FieldHandle\|mismatched types\|type mismatch' $(GUARD_WB_AMD64) \
	    && echo "GUARD-WRONG-BUFFER-AMD64 PASS" \
	    || (echo "GUARD-WRONG-BUFFER-AMD64 FAIL (expected type error not found)"; \
	        cat $(GUARD_WB_AMD64); exit 1)

stage-5-guard-closure-handler-arm64: $(GUARD_CH_ARM64)
	@echo "=== Stage 5 guard closure-handler — arm64 ==="
	@grep -q 'fn pointer\|expected fn item\|closure\|captures' $(GUARD_CH_ARM64) \
	    && echo "GUARD-CLOSURE-HANDLER-ARM64 PASS" \
	    || (echo "GUARD-CLOSURE-HANDLER-ARM64 FAIL (expected closure error not found)"; \
	        cat $(GUARD_CH_ARM64); exit 1)

stage-5-guard-closure-handler-amd64: $(GUARD_CH_AMD64)
	@echo "=== Stage 5 guard closure-handler — amd64 ==="
	@grep -q 'fn pointer\|expected fn item\|closure\|captures' $(GUARD_CH_AMD64) \
	    && echo "GUARD-CLOSURE-HANDLER-AMD64 PASS" \
	    || (echo "GUARD-CLOSURE-HANDLER-AMD64 FAIL (expected closure error not found)"; \
	        cat $(GUARD_CH_AMD64); exit 1)

stage-5-guard-wrong-buffer: stage-5-guard-wrong-buffer-arm64 stage-5-guard-wrong-buffer-amd64
stage-5-guard-closure-handler: stage-5-guard-closure-handler-arm64 stage-5-guard-closure-handler-amd64

# The four-way digest matrix is already validated by stage-3-diff.
stage-5-matrix: stage-3-diff
	@echo "STAGE-5-MATRIX PASS (four-way digest equality confirmed by Stage 3)"

stage-5: stage-5-matrix stage-5-guard-wrong-buffer stage-5-guard-closure-handler
	@echo "=== Spike O — Stage 5 PASS ==="

# ─── all / clean ──────────────────────────────────────────────────────────────

.PHONY: all
all: stage-1 stage-2 stage-3 stage-4 stage-5
	@echo "=== Spike O — Stages 1-5 ALL PASS ==="

.PHONY: clean
clean:
	$(RM) -r $(DIGESTS_DIR) $(BUFFERS_DIR)
	-docker image rm $(DOCKER_IMAGE_ARM64) $(DOCKER_IMAGE_AMD64) 2>/dev/null || true
