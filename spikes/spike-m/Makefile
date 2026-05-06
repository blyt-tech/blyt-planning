## Spike M — managed Lua coroutine save/restore: top-level orchestration.
##
## Stages 1 and 2 are wired in this iteration:
##   make stage-1                     single-frame save floor (linear cutscene)
##   make stage-2                     all-S sweep on branched cutscene
##                                    (S∈[1,N-1] × 4 directions × 2 hosts)
##
## Composite targets:
##   make docker-build                arm64 + amd64
##   make docker-build-arm64          arm64 only
##   make docker-build-amd64          amd64 only
##   make elf-bytes                   sha256 of every cart ELF; diff
##   make all                         every wired stage + diff
##   make clean                       remove host-side artefacts and images

DOCKER_IMAGE_ARM64 ?= fc32-spike-m-arm64
DOCKER_IMAGE_AMD64 ?= fc32-spike-m-amd64
SPIKE_K_IMAGE_ARM  ?= fc32-spike-k-arm64
SPIKE_K_IMAGE_AMD  ?= fc32-spike-k-amd64

RV32EMU_BIN := /spike-a/rv32emu/build/rv32emu
ELFS_DIR    := /spike-m/elfs

NFRAMES := 30

# Stage 1 — det_cutscene_linear single-frame save floor.
STAGE_1_SAVE_FRAME := 15
STAGE_1_SAVE_ELF   := det_cutscene_linear_save.elf
STAGE_1_LOAD_ELF   := det_cutscene_linear_load.elf
STAGE_1_FULL_ELF   := det_cutscene_linear_full.elf

# Stage 2 — det_cutscene_branched all-S sweep.
STAGE_2_SAVE_ELF := det_cutscene_branched_save.elf
STAGE_2_LOAD_ELF := det_cutscene_branched_load.elf
STAGE_2_FULL_ELF := det_cutscene_branched_full.elf

ALL_ELFS := $(STAGE_1_SAVE_ELF) $(STAGE_1_LOAD_ELF) $(STAGE_1_FULL_ELF) \
            $(STAGE_2_SAVE_ELF) $(STAGE_2_LOAD_ELF) $(STAGE_2_FULL_ELF)

DIGESTS_DIR := digests
BUFFERS_DIR := buffers

# ─── docker build ────────────────────────────────────────────────────────────

.PHONY: docker-build docker-build-arm64 docker-build-amd64
docker-build: docker-build-arm64 docker-build-amd64

docker-build-arm64:
	$(MAKE) -C ../spike-k docker-build-arm64
	DOCKER_BUILDKIT=1 docker build --platform linux/arm64 \
	    --build-arg SPIKE_K_IMAGE=$(SPIKE_K_IMAGE_ARM) \
	    --build-context spike-k=../spike-k \
	    --build-context spike-d=../spike-d \
	    -t $(DOCKER_IMAGE_ARM64) .

docker-build-amd64:
	$(MAKE) -C ../spike-k docker-build-amd64
	DOCKER_BUILDKIT=1 docker build --platform linux/amd64 \
	    --build-arg SPIKE_K_IMAGE=$(SPIKE_K_IMAGE_AMD) \
	    --build-context spike-k=../spike-k \
	    --build-context spike-d=../spike-d \
	    -t $(DOCKER_IMAGE_AMD64) .

.PHONY: shell-arm64 shell-amd64
shell-arm64: docker-build-arm64
	docker run --rm -it --platform linux/arm64 $(DOCKER_IMAGE_ARM64) /bin/bash
shell-amd64: docker-build-amd64
	docker run --rm -it --platform linux/amd64 $(DOCKER_IMAGE_AMD64) /bin/bash

# ─── ELF byte-identity gate ──────────────────────────────────────────────────

.PHONY: elf-bytes elf-bytes-arm64 elf-bytes-amd64 elf-bytes-diff

ELF_HASH_ARM64 := $(DIGESTS_DIR)/elfs.arm64.sha256
ELF_HASH_AMD64 := $(DIGESTS_DIR)/elfs.amd64.sha256

$(DIGESTS_DIR):
	mkdir -p $@
$(BUFFERS_DIR):
	mkdir -p $@

elf-bytes-arm64: docker-build-arm64 | $(DIGESTS_DIR)
	@docker run --rm --platform linux/arm64 $(DOCKER_IMAGE_ARM64) \
	    sh -c 'cd $(ELFS_DIR) && sha256sum $(ALL_ELFS)' \
	    | sort > $(ELF_HASH_ARM64)
	@echo "wrote $(ELF_HASH_ARM64):"
	@cat $(ELF_HASH_ARM64)

elf-bytes-amd64: docker-build-amd64 | $(DIGESTS_DIR)
	@docker run --rm --platform linux/amd64 $(DOCKER_IMAGE_AMD64) \
	    sh -c 'cd $(ELFS_DIR) && sha256sum $(ALL_ELFS)' \
	    | sort > $(ELF_HASH_AMD64)
	@echo "wrote $(ELF_HASH_AMD64):"
	@cat $(ELF_HASH_AMD64)

elf-bytes-diff: elf-bytes-arm64 elf-bytes-amd64
	@echo "=== Spike M ELF byte identity check ==="
	@if diff -u $(ELF_HASH_ARM64) $(ELF_HASH_AMD64); then \
	    echo "ELF-BYTES PASS"; \
	else \
	    echo "ELF-BYTES FAIL"; exit 1; \
	fi

elf-bytes: elf-bytes-diff

# ─── Stage 1: det_cutscene_linear save → cross-load → digest diff ─────────

DCL_BUF_ARM64 := $(BUFFERS_DIR)/det_cutscene_linear.arm64.hex
DCL_BUF_AMD64 := $(BUFFERS_DIR)/det_cutscene_linear.amd64.hex

$(DCL_BUF_ARM64): docker-build-arm64 | $(BUFFERS_DIR)
	docker run --rm --platform linux/arm64 $(DOCKER_IMAGE_ARM64) \
	    $(RV32EMU_BIN) $(ELFS_DIR)/$(STAGE_1_SAVE_ELF) $(STAGE_1_SAVE_FRAME) \
	    | grep '^BUFFER ' > $@.tmp
	@if [ ! -s $@.tmp ]; then echo "FAIL: no BUFFER line"; rm -f $@.tmp; exit 1; fi
	@mv $@.tmp $@

$(DCL_BUF_AMD64): docker-build-amd64 | $(BUFFERS_DIR)
	docker run --rm --platform linux/amd64 $(DOCKER_IMAGE_AMD64) \
	    $(RV32EMU_BIN) $(ELFS_DIR)/$(STAGE_1_SAVE_ELF) $(STAGE_1_SAVE_FRAME) \
	    | grep '^BUFFER ' > $@.tmp
	@if [ ! -s $@.tmp ]; then echo "FAIL: no BUFFER line"; rm -f $@.tmp; exit 1; fi
	@mv $@.tmp $@

.PHONY: stage-1-save stage-1-buffer-diff stage-1-load stage-1-diff stage-1
stage-1-save: $(DCL_BUF_ARM64) $(DCL_BUF_AMD64)

stage-1-buffer-diff: stage-1-save
	@echo "=== Stage 1 — buffer byte identity ==="
	@diff -q $(DCL_BUF_ARM64) $(DCL_BUF_AMD64) && echo "BUFFER PASS" || (echo "BUFFER FAIL"; exit 1)

DCL_S := $(STAGE_1_SAVE_FRAME)
DCL_F := $(shell echo $$(($(STAGE_1_SAVE_FRAME)+1)))

DCL_DGST_SAME_ARM64  := $(DIGESTS_DIR)/det_cutscene_linear.same.arm64.f$(DCL_F).txt
DCL_DGST_SAME_AMD64  := $(DIGESTS_DIR)/det_cutscene_linear.same.amd64.f$(DCL_F).txt
DCL_DGST_CROSS_ARM64 := $(DIGESTS_DIR)/det_cutscene_linear.cross.arm64.f$(DCL_F).txt
DCL_DGST_CROSS_AMD64 := $(DIGESTS_DIR)/det_cutscene_linear.cross.amd64.f$(DCL_F).txt

define run_load
	docker run --rm --platform $(2) \
	    -v $(abspath $(BUFFERS_DIR)):/spike-m/buffers:ro \
	    $(1) \
	    $(RV32EMU_BIN) $(ELFS_DIR)/$(STAGE_1_LOAD_ELF) /spike-m/buffers/$(notdir $(3)) \
	    | grep '^DIGEST ' > $(4)
endef

$(DCL_DGST_SAME_ARM64): $(DCL_BUF_ARM64) docker-build-arm64 | $(DIGESTS_DIR)
	$(call run_load,$(DOCKER_IMAGE_ARM64),linux/arm64,$(DCL_BUF_ARM64),$@)
$(DCL_DGST_SAME_AMD64): $(DCL_BUF_AMD64) docker-build-amd64 | $(DIGESTS_DIR)
	$(call run_load,$(DOCKER_IMAGE_AMD64),linux/amd64,$(DCL_BUF_AMD64),$@)
$(DCL_DGST_CROSS_ARM64): $(DCL_BUF_AMD64) docker-build-arm64 | $(DIGESTS_DIR)
	$(call run_load,$(DOCKER_IMAGE_ARM64),linux/arm64,$(DCL_BUF_AMD64),$@)
$(DCL_DGST_CROSS_AMD64): $(DCL_BUF_ARM64) docker-build-amd64 | $(DIGESTS_DIR)
	$(call run_load,$(DOCKER_IMAGE_AMD64),linux/amd64,$(DCL_BUF_ARM64),$@)

stage-1-load: $(DCL_DGST_SAME_ARM64) $(DCL_DGST_SAME_AMD64) \
              $(DCL_DGST_CROSS_ARM64) $(DCL_DGST_CROSS_AMD64)

stage-1-diff: stage-1-load
	@echo "=== Stage 1 — continuation digest stream identity ==="
	@first=$(DCL_DGST_SAME_ARM64); fail=0; \
	    for f in $(DCL_DGST_SAME_AMD64) $(DCL_DGST_CROSS_ARM64) $(DCL_DGST_CROSS_AMD64); do \
	        diff -q $$first $$f >/dev/null || { echo "DIGEST FAIL — $$f differs"; fail=1; }; \
	    done; \
	    [ $$fail -eq 0 ] && echo "DIGEST PASS — 4 streams byte-identical" || exit 1

stage-1: elf-bytes-diff stage-1-buffer-diff stage-1-diff
	@echo "=== Spike M — Stage 1 PASS ==="

# ─── per-stage sweep harnesses ──────────────────────────────────────────────
#
# Each stage's all-S save-frame sweep is wrapped by scripts/run-sweep.sh,
# which iterates S in [S_MIN..S_MAX], saves on both hosts, runs the
# four load combinations, and asserts (a) byte-identical buffers,
# (b) byte-identical 4-way digest streams, and (c) match against the
# same-host straight-through suffix from frame S+1.
#
# The Stage 5 transient negative test has its own harness because the
# headline gate is STDERR byte-equality alongside DIGEST byte-equality.

S_MAX := $(shell echo $$(($(NFRAMES) - 1)))

.PHONY: stage-2 stage-2-sweep
stage-2-sweep: docker-build-arm64 docker-build-amd64 | $(BUFFERS_DIR) $(DIGESTS_DIR)
	./scripts/run-sweep.sh det_cutscene_branched 1 $(S_MAX)

stage-2: stage-2-sweep
	@echo "=== Spike M — Stage 2 PASS ==="

# ─── Stage 3: multi-script + entity-handle pattern ──────────────────────────

.PHONY: stage-3 stage-3-two-scripts stage-3-with-entities stage-3-destroy stage-3-short

stage-3-two-scripts: docker-build-arm64 docker-build-amd64 | $(BUFFERS_DIR) $(DIGESTS_DIR)
	./scripts/run-sweep.sh det_two_scripts 1 $(S_MAX)

stage-3-with-entities: docker-build-arm64 docker-build-amd64 | $(BUFFERS_DIR) $(DIGESTS_DIR)
	./scripts/run-sweep.sh det_scripts_with_entities 1 $(S_MAX)

stage-3-destroy: docker-build-arm64 docker-build-amd64 | $(BUFFERS_DIR) $(DIGESTS_DIR)
	./scripts/run-sweep.sh det_two_scripts_destroy 1 $(S_MAX)

stage-3-short: docker-build-arm64 docker-build-amd64 | $(BUFFERS_DIR) $(DIGESTS_DIR)
	./scripts/run-sweep.sh det_short_script 1 $(S_MAX)

stage-3: stage-3-two-scripts stage-3-with-entities stage-3-destroy stage-3-short
	@echo "=== Spike M — Stage 3 PASS ==="

# ─── Stage 4: constrained-shape flattener cross-host ────────────────────────

.PHONY: stage-4
stage-4: docker-build-arm64 docker-build-amd64 | $(BUFFERS_DIR) $(DIGESTS_DIR)
	./scripts/run-sweep.sh det_table_shapes 1 $(S_MAX)
	@echo "=== Spike M — Stage 4 PASS ==="

# ─── Stage 5: transient-coroutine boundary throw ────────────────────────────

.PHONY: stage-5
stage-5: docker-build-arm64 docker-build-amd64 | $(BUFFERS_DIR) $(DIGESTS_DIR)
	./scripts/run-transient-negative.sh
	@echo "=== Spike M — Stage 5 PASS ==="

# ─── Stage 6: negative tests (mechanism error paths) ────────────────────────

.PHONY: stage-6
stage-6: docker-build-arm64 | $(BUFFERS_DIR) $(DIGESTS_DIR)
	./scripts/run-negative-tests.sh
	@echo "=== Spike M — Stage 6 PASS ==="

# ─── full / clean ───────────────────────────────────────────────────────────

.PHONY: all
all: stage-1 stage-2 stage-3 stage-4 stage-5 stage-6
	@echo "=== Spike M — Stages 1–6 PASS ==="

.PHONY: clean
clean:
	$(RM) -r $(BUFFERS_DIR) $(DIGESTS_DIR) elfs
	-docker image rm $(DOCKER_IMAGE_ARM64) $(DOCKER_IMAGE_AMD64) 2>/dev/null || true
