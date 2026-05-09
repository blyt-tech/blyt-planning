## Spike P — heap allocator, atomics, and on_load two-category validation.
##
## Stages:
##   Stage 1 — AtomicU32 / Once → LR/SC + AMO instruction gate
##   Stage 2 — allocator smoke test (Box alloc + dealloc, Vec growth)
##   Stage 3 — Vec + Arc alloc cart, 30-frame cross-host digest gate
##   Stage 4 — on_load two-category demonstration (straight-through vs restore)
##   Stage 5 — full cross-host matrix + write-up
##
## Composite targets:
##   make docker-build                arm64 + amd64 images
##   make docker-build-arm64
##   make docker-build-amd64
##   make stage-1                     atomics: disasm gate + cross-host digest
##   make stage-2                     heap smoke: both hosts exit 0
##   make stage-3                     alloc cart: 30-frame cross-host digest
##   make stage-4                     on_load: straight-through vs restore
##   make stage-5-matrix              full matrix summary
##   make all                         all stages; exits 0 iff everything passes
##   make clean

DOCKER_IMAGE_ARM64 ?= fc32-spike-p-arm64
DOCKER_IMAGE_AMD64 ?= fc32-spike-p-amd64
SPIKE_K_IMAGE_ARM  ?= fc32-spike-k-arm64
SPIKE_K_IMAGE_AMD  ?= fc32-spike-k-amd64
SPIKE_O_IMAGE_ARM  ?= fc32-spike-o-arm64
SPIKE_O_IMAGE_AMD  ?= fc32-spike-o-amd64

RV32EMU_BIN := /spike-a/rv32emu/build/rv32emu
ELFS_DIR    := /spike-p/elfs
LIB_DIR     := /spike-p/lib

DIGESTS_DIR := digests
STAGE4_DIR  := stage4_state

# ─── docker build ─────────────────────────────────────────────────────────────

.PHONY: docker-build docker-build-arm64 docker-build-amd64
docker-build: docker-build-arm64 docker-build-amd64

docker-build-arm64:
	$(MAKE) -C ../spike-o docker-build-arm64
	DOCKER_BUILDKIT=1 docker build --platform linux/arm64 \
	    --build-arg SPIKE_K_IMAGE=$(SPIKE_K_IMAGE_ARM) \
	    --build-arg SPIKE_O_IMAGE=$(SPIKE_O_IMAGE_ARM) \
	    --build-context spike-i=../spike-i \
	    -t $(DOCKER_IMAGE_ARM64) .

docker-build-amd64:
	$(MAKE) -C ../spike-o docker-build-amd64
	DOCKER_BUILDKIT=1 docker build --platform linux/amd64 \
	    --build-arg SPIKE_K_IMAGE=$(SPIKE_K_IMAGE_AMD) \
	    --build-arg SPIKE_O_IMAGE=$(SPIKE_O_IMAGE_AMD) \
	    --build-context spike-i=../spike-i \
	    -t $(DOCKER_IMAGE_AMD64) .

.PHONY: shell-arm64 shell-amd64
shell-arm64: docker-build-arm64
	docker run --rm -it --platform linux/arm64 $(DOCKER_IMAGE_ARM64) /bin/bash

shell-amd64: docker-build-amd64
	docker run --rm -it --platform linux/amd64 $(DOCKER_IMAGE_AMD64) /bin/bash

$(DIGESTS_DIR):
	mkdir -p $@

$(STAGE4_DIR):
	mkdir -p $@

# ─── ELF byte-identity gate ───────────────────────────────────────────────────

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
	    || (echo "ELF-BYTES FAIL"; diff $(ELF_HASH_ARM64) $(ELF_HASH_AMD64); exit 1)

# ─── Stage 1: atomics ─────────────────────────────────────────────────────────

# Disassembly gate: confirm A-extension opcodes are present.
DISASM_ARM64 := $(DIGESTS_DIR)/atomics-disasm-arm64.txt
DISASM_AMD64 := $(DIGESTS_DIR)/atomics-disasm-amd64.txt

$(DISASM_ARM64): docker-build-arm64 | $(DIGESTS_DIR)
	docker run --rm --platform linux/arm64 $(DOCKER_IMAGE_ARM64) \
	    cat $(ELFS_DIR)/atomics-disasm.txt > $@

$(DISASM_AMD64): docker-build-amd64 | $(DIGESTS_DIR)
	docker run --rm --platform linux/amd64 $(DOCKER_IMAGE_AMD64) \
	    cat $(ELFS_DIR)/atomics-disasm.txt > $@

ATOM_DIGEST_ARM64 := $(DIGESTS_DIR)/atomics-arm64.digest
ATOM_DIGEST_AMD64 := $(DIGESTS_DIR)/atomics-amd64.digest

$(ATOM_DIGEST_ARM64): docker-build-arm64 | $(DIGESTS_DIR)
	docker run --rm --platform linux/arm64 $(DOCKER_IMAGE_ARM64) \
	    $(RV32EMU_BIN) -L $(LIB_DIR)/build $(ELFS_DIR)/atomics_cart.elf 2>/dev/null \
	    | grep '^DIGEST' > $@

$(ATOM_DIGEST_AMD64): docker-build-amd64 | $(DIGESTS_DIR)
	docker run --rm --platform linux/amd64 $(DOCKER_IMAGE_AMD64) \
	    $(RV32EMU_BIN) -L $(LIB_DIR)/build $(ELFS_DIR)/atomics_cart.elf 2>/dev/null \
	    | grep '^DIGEST' > $@

.PHONY: stage-1-disasm-arm64 stage-1-disasm-amd64 stage-1-disasm
stage-1-disasm-arm64: $(DISASM_ARM64)
	@echo "=== Stage 1 disasm gate — arm64 ==="
	@grep -qEi '0x[0-9a-f]+[2a]f$$' $(DISASM_ARM64) \
	    && echo "DISASM-ARM64 PASS" \
	    || (echo "DISASM-ARM64 FAIL (no A-extension opcodes)"; cat $(DISASM_ARM64); exit 1)

stage-1-disasm-amd64: $(DISASM_AMD64)
	@echo "=== Stage 1 disasm gate — amd64 ==="
	@grep -qEi '0x[0-9a-f]+[2a]f$$' $(DISASM_AMD64) \
	    && echo "DISASM-AMD64 PASS" \
	    || (echo "DISASM-AMD64 FAIL (no A-extension opcodes)"; cat $(DISASM_AMD64); exit 1)

stage-1-disasm: stage-1-disasm-arm64 stage-1-disasm-amd64

.PHONY: stage-1-atomics-arm64 stage-1-atomics-amd64 stage-1-diff stage-1
stage-1-atomics-arm64: $(ATOM_DIGEST_ARM64)
	@test -s $(ATOM_DIGEST_ARM64) \
	    && echo "ATOMICS-RUN-ARM64 PASS" \
	    || (echo "ATOMICS-RUN-ARM64 FAIL (no digest output)"; exit 1)

stage-1-atomics-amd64: $(ATOM_DIGEST_AMD64)
	@test -s $(ATOM_DIGEST_AMD64) \
	    && echo "ATOMICS-RUN-AMD64 PASS" \
	    || (echo "ATOMICS-RUN-AMD64 FAIL (no digest output)"; exit 1)

stage-1-diff: $(ATOM_DIGEST_ARM64) $(ATOM_DIGEST_AMD64)
	@echo "=== Stage 1 cross-host digest diff ==="
	@diff -q $(ATOM_DIGEST_ARM64) $(ATOM_DIGEST_AMD64) \
	    && echo "ATOMICS-CROSS-HOST PASS" \
	    || (echo "ATOMICS-CROSS-HOST FAIL"; diff $(ATOM_DIGEST_ARM64) $(ATOM_DIGEST_AMD64); exit 1)

stage-1: stage-1-disasm stage-1-atomics-arm64 stage-1-atomics-amd64 stage-1-diff
	@echo "=== Spike P — Stage 1 PASS ==="

# ─── Stage 2: heap smoke ──────────────────────────────────────────────────────

.PHONY: stage-2-smoke-arm64 stage-2-smoke-amd64 stage-2
stage-2-smoke-arm64: docker-build-arm64
	@echo "=== Stage 2 heap smoke — arm64 ==="
	@docker run --rm --platform linux/arm64 $(DOCKER_IMAGE_ARM64) \
	    $(RV32EMU_BIN) -L $(LIB_DIR)/build $(ELFS_DIR)/heap_smoke_cart.elf 2>/dev/null \
	    | grep -q 'OK' \
	    && echo "HEAP-SMOKE-ARM64 PASS" \
	    || (echo "HEAP-SMOKE-ARM64 FAIL"; exit 1)

stage-2-smoke-amd64: docker-build-amd64
	@echo "=== Stage 2 heap smoke — amd64 ==="
	@docker run --rm --platform linux/amd64 $(DOCKER_IMAGE_AMD64) \
	    $(RV32EMU_BIN) -L $(LIB_DIR)/build $(ELFS_DIR)/heap_smoke_cart.elf 2>/dev/null \
	    | grep -q 'OK' \
	    && echo "HEAP-SMOKE-AMD64 PASS" \
	    || (echo "HEAP-SMOKE-AMD64 FAIL"; exit 1)

stage-2: stage-2-smoke-arm64 stage-2-smoke-amd64
	@echo "=== Spike P — Stage 2 PASS ==="

# ─── Stage 3: alloc cart (Vec + Arc, 30 frames) ───────────────────────────────

ALLOC_DIGEST_ARM64 := $(DIGESTS_DIR)/alloc-arm64.digest
ALLOC_DIGEST_AMD64 := $(DIGESTS_DIR)/alloc-amd64.digest

$(ALLOC_DIGEST_ARM64): docker-build-arm64 | $(DIGESTS_DIR)
	docker run --rm --platform linux/arm64 $(DOCKER_IMAGE_ARM64) \
	    $(RV32EMU_BIN) -L $(LIB_DIR)/build $(ELFS_DIR)/alloc_cart.elf 2>/dev/null \
	    | grep '^DIGEST' > $@

$(ALLOC_DIGEST_AMD64): docker-build-amd64 | $(DIGESTS_DIR)
	docker run --rm --platform linux/amd64 $(DOCKER_IMAGE_AMD64) \
	    $(RV32EMU_BIN) -L $(LIB_DIR)/build $(ELFS_DIR)/alloc_cart.elf 2>/dev/null \
	    | grep '^DIGEST' > $@

.PHONY: stage-3-alloc-arm64 stage-3-alloc-amd64 stage-3-diff stage-3
stage-3-alloc-arm64: $(ALLOC_DIGEST_ARM64)
	@test -s $(ALLOC_DIGEST_ARM64) \
	    && echo "ALLOC-ARM64 PASS" \
	    || (echo "ALLOC-ARM64 FAIL (no digest output)"; exit 1)

stage-3-alloc-amd64: $(ALLOC_DIGEST_AMD64)
	@test -s $(ALLOC_DIGEST_AMD64) \
	    && echo "ALLOC-AMD64 PASS" \
	    || (echo "ALLOC-AMD64 FAIL (no digest output)"; exit 1)

stage-3-diff: $(ALLOC_DIGEST_ARM64) $(ALLOC_DIGEST_AMD64)
	@echo "=== Stage 3 cross-host digest diff (30 frames) ==="
	@diff -q $(ALLOC_DIGEST_ARM64) $(ALLOC_DIGEST_AMD64) \
	    && echo "ALLOC-CROSS-HOST PASS" \
	    || (echo "ALLOC-CROSS-HOST FAIL"; diff $(ALLOC_DIGEST_ARM64) $(ALLOC_DIGEST_AMD64); exit 1)

stage-3: stage-3-alloc-arm64 stage-3-alloc-amd64 stage-3-diff
	@echo "=== Spike P — Stage 3 PASS ==="

# ─── Stage 4: on_load two-category demonstration ──────────────────────────────
#
# Three runs on arm64:
#   straight  — no save dir mounted; blyt_test_save_now() is a no-op.
#   save      — save dir mounted; save file written at S_CTR=5.
#   restore   — save dir mounted (read-only) with existing file; load mode.
#
# Comparison: frames 5-9 (DIGEST lines 6-10, 0-indexed frame 5 is line 6)
# from the straight run must equal frames 5-9 from the restore run.
#
# Cross-host: save on arm64, restore on amd64.

STRAIGHT_ARM64  := $(DIGESTS_DIR)/onload-straight-arm64.digest
STRAIGHT_AMD64  := $(DIGESTS_DIR)/onload-straight-amd64.digest
RESTORE_ARM64   := $(DIGESTS_DIR)/onload-restore-arm64.digest
RESTORE_AMD64   := $(DIGESTS_DIR)/onload-restore-amd64.digest
CROSS_RESTORE   := $(DIGESTS_DIR)/onload-cross-amd64.digest
# Use /tmp/ for save dirs — Docker Desktop for Mac requires pre-existing dirs for bind mounts.
SAVE_DIR_ARM64 := /tmp/spike-p-stage4-arm64
SAVE_DIR_AMD64 := /tmp/spike-p-stage4-amd64
SAVE_FILE_ARM64 := $(STAGE4_DIR)/arm64/save-state.bin
SAVE_FILE_AMD64 := $(STAGE4_DIR)/amd64/save-state.bin

$(STAGE4_DIR)/arm64 $(STAGE4_DIR)/amd64:
	mkdir -p $@

# Straight-through: no volume mount → blyt_test_save_now is a no-op.
$(STRAIGHT_ARM64): docker-build-arm64 | $(DIGESTS_DIR)
	docker run --rm --platform linux/arm64 $(DOCKER_IMAGE_ARM64) \
	    $(RV32EMU_BIN) -L $(LIB_DIR)/build $(ELFS_DIR)/alloc_cart.elf 2>/dev/null \
	    | grep '^DIGEST' > $@

$(STRAIGHT_AMD64): docker-build-amd64 | $(DIGESTS_DIR)
	docker run --rm --platform linux/amd64 $(DOCKER_IMAGE_AMD64) \
	    $(RV32EMU_BIN) -L $(LIB_DIR)/build $(ELFS_DIR)/alloc_cart.elf 2>/dev/null \
	    | grep '^DIGEST' > $@

# Save run: mount /tmp dir as /tmp/spike-p → write save-state.bin.
$(SAVE_FILE_ARM64): docker-build-arm64 | $(STAGE4_DIR)/arm64 $(DIGESTS_DIR)
	mkdir -p $(SAVE_DIR_ARM64)
	rm -f $(SAVE_DIR_ARM64)/save-state.bin
	docker run --rm --platform linux/arm64 \
	    -v $(SAVE_DIR_ARM64):/tmp/spike-p \
	    $(DOCKER_IMAGE_ARM64) \
	    $(RV32EMU_BIN) -L $(LIB_DIR)/build $(ELFS_DIR)/alloc_cart.elf 2>/dev/null \
	    | grep '^DIGEST' > $(DIGESTS_DIR)/onload-save-arm64.digest
	@test -f $(SAVE_DIR_ARM64)/save-state.bin \
	    || (echo "SAVE FAILED: save-state.bin not written to $(SAVE_DIR_ARM64)"; exit 1)
	cp $(SAVE_DIR_ARM64)/save-state.bin $@

# Restore run (arm64): copy save to /tmp read-only dir → load mode.
$(RESTORE_ARM64): $(SAVE_FILE_ARM64) | $(DIGESTS_DIR)
	mkdir -p $(SAVE_DIR_ARM64)
	cp $(SAVE_FILE_ARM64) $(SAVE_DIR_ARM64)/save-state.bin
	docker run --rm --platform linux/arm64 \
	    -v $(SAVE_DIR_ARM64):/tmp/spike-p:ro \
	    $(DOCKER_IMAGE_ARM64) \
	    $(RV32EMU_BIN) -L $(LIB_DIR)/build $(ELFS_DIR)/alloc_cart.elf 2>/dev/null \
	    | grep '^DIGEST' > $@

# Cross-host restore (arm64 save → amd64 restore).
$(CROSS_RESTORE): $(SAVE_FILE_ARM64) docker-build-amd64 | $(DIGESTS_DIR)
	mkdir -p $(SAVE_DIR_AMD64)
	cp $(SAVE_FILE_ARM64) $(SAVE_DIR_AMD64)/save-state.bin
	docker run --rm --platform linux/amd64 \
	    -v $(SAVE_DIR_AMD64):/tmp/spike-p:ro \
	    $(DOCKER_IMAGE_AMD64) \
	    $(RV32EMU_BIN) -L $(LIB_DIR)/build $(ELFS_DIR)/alloc_cart.elf 2>/dev/null \
	    | grep '^DIGEST' > $@

# Extract frames 5-9 (DIGEST lines where frame field is 5..9).
# Frame field is the first number after "DIGEST ".
define filter_frames_5_9
    grep '^DIGEST' $(1) | awk '{if ($$2+0 >= 5 && $$2+0 <= 9) print}' > $(2)
endef

STRAIGHT_F5_9_ARM64 := $(DIGESTS_DIR)/straight-f5-9-arm64.digest
RESTORE_F5_9_ARM64  := $(DIGESTS_DIR)/restore-f5-9-arm64.digest
CROSS_F5_9          := $(DIGESTS_DIR)/cross-f5-9-amd64.digest

$(STRAIGHT_F5_9_ARM64): $(STRAIGHT_ARM64)
	$(call filter_frames_5_9,$<,$@)

$(RESTORE_F5_9_ARM64): $(RESTORE_ARM64)
	$(call filter_frames_5_9,$<,$@)

$(CROSS_F5_9): $(CROSS_RESTORE)
	$(call filter_frames_5_9,$<,$@)

.PHONY: stage-4-straight-arm64 stage-4-straight-amd64
stage-4-straight-arm64: $(STRAIGHT_ARM64)
	@test -s $(STRAIGHT_ARM64) && echo "STAGE4-STRAIGHT-ARM64 PASS" \
	    || (echo "STAGE4-STRAIGHT-ARM64 FAIL"; exit 1)

stage-4-straight-amd64: $(STRAIGHT_AMD64)
	@test -s $(STRAIGHT_AMD64) && echo "STAGE4-STRAIGHT-AMD64 PASS" \
	    || (echo "STAGE4-STRAIGHT-AMD64 FAIL"; exit 1)

.PHONY: stage-4-restore-arm64 stage-4-cross stage-4
stage-4-restore-arm64: $(STRAIGHT_F5_9_ARM64) $(RESTORE_F5_9_ARM64)
	@echo "=== Stage 4 on_load restore gate — arm64 ==="
	@diff -q $(STRAIGHT_F5_9_ARM64) $(RESTORE_F5_9_ARM64) \
	    && echo "ONLOAD-RESTORE-ARM64 PASS" \
	    || (echo "ONLOAD-RESTORE-ARM64 FAIL"; \
	        echo "-- straight --"; cat $(STRAIGHT_F5_9_ARM64); \
	        echo "-- restore --"; cat $(RESTORE_F5_9_ARM64); exit 1)

stage-4-cross: $(STRAIGHT_F5_9_ARM64) $(CROSS_F5_9)
	@echo "=== Stage 4 cross-host restore gate (arm64 save → amd64 load) ==="
	@diff -q $(STRAIGHT_F5_9_ARM64) $(CROSS_F5_9) \
	    && echo "ONLOAD-CROSS-HOST PASS" \
	    || (echo "ONLOAD-CROSS-HOST FAIL"; \
	        echo "-- arm64 straight --"; cat $(STRAIGHT_F5_9_ARM64); \
	        echo "-- amd64 cross-restore --"; cat $(CROSS_F5_9); exit 1)

stage-4: stage-4-straight-arm64 stage-4-straight-amd64 \
         stage-4-restore-arm64 stage-4-cross
	@echo "=== Spike P — Stage 4 PASS ==="

# ─── Stage 5: full cross-host matrix ──────────────────────────────────────────

.PHONY: stage-5-matrix stage-5
stage-5-matrix: stage-1-diff stage-3-diff stage-4-restore-arm64 stage-4-cross
	@echo ""
	@echo "=== Stage 5 Cross-Host Matrix ==="
	@echo "  atomics   (10 frames) arm64=arm64  PASS (stage-1-diff)"
	@echo "  alloc     (30 frames) arm64=amd64  PASS (stage-3-diff)"
	@echo "  on_load straight-through            PASS (stage-4-straight)"
	@echo "  on_load save/restore arm64          PASS (stage-4-restore)"
	@echo "  on_load cross-host arm64→amd64      PASS (stage-4-cross)"
	@echo "STAGE-5-MATRIX PASS"

stage-5: stage-5-matrix
	@echo "=== Spike P — Stage 5 PASS ==="

# ─── all / clean ──────────────────────────────────────────────────────────────

.PHONY: all
all: stage-1 stage-2 stage-3 stage-4 stage-5
	@echo "=== Spike P — Stages 1-5 ALL PASS ==="

.PHONY: clean
clean:
	$(RM) -r $(DIGESTS_DIR) $(STAGE4_DIR)
	-docker image rm $(DOCKER_IMAGE_ARM64) $(DOCKER_IMAGE_AMD64) 2>/dev/null || true
