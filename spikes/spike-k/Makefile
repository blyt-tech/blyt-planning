## Spike K — cross-host save-state portability.
##
## Stages 1–5 add tracked-region kinds incrementally; Stage 6 closes the
## full cross-host matrix.  This Makefile orchestrates the same way Spike
## D's does: build two Docker images (linux/arm64 + linux/amd64), run
## carts in each, capture artefacts on the host, diff.
##
## Stage 1 floor case (only Stage 1 is wired in this commit):
##   make stage-1                    Build, save, cross-load, diff whetstone
##
## Composite targets:
##   make docker-build                arm64 + amd64
##   make docker-build-arm64          arm64 only
##   make docker-build-amd64          amd64 only
##   make elf-bytes                   sha256 of every cart ELF in each image; diff
##   make stage-1                     full Stage 1 — see above
##   make all                         every wired stage + diff
##   make clean                       remove host-side artefacts and images

DOCKER_IMAGE_ARM64 ?= fc32-spike-k-arm64
DOCKER_IMAGE_AMD64 ?= fc32-spike-k-amd64
SPIKE_D_IMAGE_ARM  ?= fc32-spike-d-arm64
SPIKE_D_IMAGE_AMD  ?= fc32-spike-d-amd64

RV32EMU_BIN := /spike-a/rv32emu/build/rv32emu
ELFS_DIR    := /spike-k/elfs

# Stage 1 cart pair — extend STAGE_*_CARTS as later stages wire in.
STAGE_1_SAVE_ELF := whetstone_save.elf
STAGE_1_LOAD_ELF := whetstone_load.elf

# All ELF byte-identity hashes go through here.
ALL_ELFS := whetstone_save.elf whetstone_load.elf corruption_tests.elf \
            det_shake_save.elf det_shake_load.elf det_shake_full.elf \
            det_audio_branch_save.elf det_audio_branch_load.elf det_audio_branch_full.elf \
            det_cutscene_save.elf det_cutscene_load.elf det_cutscene_full.elf

DIGESTS_DIR := digests
BUFFERS_DIR := buffers

# Frame at which Stage 1 saves.  whetstone_load.elf reads fs->frame from
# the buffer and resumes at fs->frame + 1, so we don't need to thread it
# through the load command line — the load cart is self-driving.
STAGE_1_SAVE_FRAME := 15

# ─── docker build ────────────────────────────────────────────────────────────

.PHONY: docker-build docker-build-arm64 docker-build-amd64
docker-build: docker-build-arm64 docker-build-amd64

docker-build-arm64:
	$(MAKE) -C ../spike-d docker-build-arm64
	DOCKER_BUILDKIT=1 docker build --platform linux/arm64 \
	    --build-arg SPIKE_D_IMAGE=$(SPIKE_D_IMAGE_ARM) \
	    --build-context spike-d=../spike-d \
	    -t $(DOCKER_IMAGE_ARM64) .

docker-build-amd64:
	$(MAKE) -C ../spike-d docker-build-amd64
	DOCKER_BUILDKIT=1 docker build --platform linux/amd64 \
	    --build-arg SPIKE_D_IMAGE=$(SPIKE_D_IMAGE_AMD) \
	    --build-context spike-d=../spike-d \
	    -t $(DOCKER_IMAGE_AMD64) .

# ─── shells ──────────────────────────────────────────────────────────────────

.PHONY: shell-arm64 shell-amd64
shell-arm64: docker-build-arm64
	docker run --rm -it --platform linux/arm64 $(DOCKER_IMAGE_ARM64) /bin/bash

shell-amd64: docker-build-amd64
	docker run --rm -it --platform linux/amd64 $(DOCKER_IMAGE_AMD64) /bin/bash

# ─── ELF byte-identity gate ──────────────────────────────────────────────────
# Same idea as spike-D: if the cart ELFs differ across hosts, every later
# comparison is moot.

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
	@echo "=== Spike K ELF byte identity check ==="
	@if diff -u $(ELF_HASH_ARM64) $(ELF_HASH_AMD64); then \
	    echo "ELF-BYTES PASS — cart binaries are byte-identical across hosts"; \
	else \
	    echo "ELF-BYTES FAIL — toolchain drift; fix before running carts"; \
	    exit 1; \
	fi

elf-bytes: elf-bytes-diff

# ─── Stage 1: whetstone save → cross-load → digest diff ──────────────────────
# The save cart writes one BUFFER line to stdout.  We capture only that
# line (greppable) so the harness ignores any stderr / preamble noise.

WHET_BUF_ARM64 := $(BUFFERS_DIR)/whetstone.arm64.hex
WHET_BUF_AMD64 := $(BUFFERS_DIR)/whetstone.amd64.hex

$(WHET_BUF_ARM64): docker-build-arm64 | $(BUFFERS_DIR)
	docker run --rm --platform linux/arm64 $(DOCKER_IMAGE_ARM64) \
	    $(RV32EMU_BIN) $(ELFS_DIR)/$(STAGE_1_SAVE_ELF) $(STAGE_1_SAVE_FRAME) \
	    | grep '^BUFFER ' > $@.tmp
	@if [ ! -s $@.tmp ]; then echo "FAIL: no BUFFER line emitted"; rm -f $@.tmp; exit 1; fi
	@mv $@.tmp $@
	@echo "wrote $@ ($$(wc -c < $@) bytes)"

$(WHET_BUF_AMD64): docker-build-amd64 | $(BUFFERS_DIR)
	docker run --rm --platform linux/amd64 $(DOCKER_IMAGE_AMD64) \
	    $(RV32EMU_BIN) $(ELFS_DIR)/$(STAGE_1_SAVE_ELF) $(STAGE_1_SAVE_FRAME) \
	    | grep '^BUFFER ' > $@.tmp
	@if [ ! -s $@.tmp ]; then echo "FAIL: no BUFFER line emitted"; rm -f $@.tmp; exit 1; fi
	@mv $@.tmp $@
	@echo "wrote $@ ($$(wc -c < $@) bytes)"

.PHONY: stage-1-save
stage-1-save: $(WHET_BUF_ARM64) $(WHET_BUF_AMD64)

.PHONY: stage-1-buffer-diff
stage-1-buffer-diff: stage-1-save
	@echo "=== Stage 1 — buffer byte identity ==="
	@sha256sum $(WHET_BUF_ARM64) $(WHET_BUF_AMD64)
	@if diff -u $(WHET_BUF_ARM64) $(WHET_BUF_AMD64); then \
	    echo "BUFFER PASS — same-host save bytes are byte-identical across hosts"; \
	else \
	    echo "BUFFER FAIL — serializer divergence; layout / NaN / endian bug"; \
	    exit 1; \
	fi

# Cross-load: feed the host-side buffer file to the load cart in each image.
# Docker -v mounts the host's buffers/ read-only into the container at
# /spike-k/buffers; the load cart's argv[1] is the in-container path.

# Same-host loads (buffer from arch X, load on arch X).
WHET_DGST_SAME_ARM64 := $(DIGESTS_DIR)/whetstone.same.arm64.f$$(($(STAGE_1_SAVE_FRAME)+1)).txt
WHET_DGST_SAME_AMD64 := $(DIGESTS_DIR)/whetstone.same.amd64.f$$(($(STAGE_1_SAVE_FRAME)+1)).txt
# Cross-host loads (buffer from arch X, load on arch Y).
WHET_DGST_CROSS_ARM64 := $(DIGESTS_DIR)/whetstone.cross.arm64.f$$(($(STAGE_1_SAVE_FRAME)+1)).txt
WHET_DGST_CROSS_AMD64 := $(DIGESTS_DIR)/whetstone.cross.amd64.f$$(($(STAGE_1_SAVE_FRAME)+1)).txt

# Macro: $1 = docker image; $2 = platform; $3 = host-side buffer path; $4 = output digests path.
define run_load
	docker run --rm --platform $(2) \
	    -v $(abspath $(BUFFERS_DIR)):/spike-k/buffers:ro \
	    $(1) \
	    $(RV32EMU_BIN) $(ELFS_DIR)/$(STAGE_1_LOAD_ELF) /spike-k/buffers/$(notdir $(3)) \
	    | grep '^DIGEST ' > $(4)
	@echo "wrote $(4) ($$$$(wc -l < $(4)) lines)"
endef

$(WHET_DGST_SAME_ARM64): $(WHET_BUF_ARM64) docker-build-arm64 | $(DIGESTS_DIR)
	$(call run_load,$(DOCKER_IMAGE_ARM64),linux/arm64,$(WHET_BUF_ARM64),$@)

$(WHET_DGST_SAME_AMD64): $(WHET_BUF_AMD64) docker-build-amd64 | $(DIGESTS_DIR)
	$(call run_load,$(DOCKER_IMAGE_AMD64),linux/amd64,$(WHET_BUF_AMD64),$@)

# Cross loads: load arm64 buffer on amd64 host and vice versa.
$(WHET_DGST_CROSS_ARM64): $(WHET_BUF_AMD64) docker-build-arm64 | $(DIGESTS_DIR)
	$(call run_load,$(DOCKER_IMAGE_ARM64),linux/arm64,$(WHET_BUF_AMD64),$@)

$(WHET_DGST_CROSS_AMD64): $(WHET_BUF_ARM64) docker-build-amd64 | $(DIGESTS_DIR)
	$(call run_load,$(DOCKER_IMAGE_AMD64),linux/amd64,$(WHET_BUF_ARM64),$@)

.PHONY: stage-1-load
stage-1-load: $(WHET_DGST_SAME_ARM64) $(WHET_DGST_SAME_AMD64) \
              $(WHET_DGST_CROSS_ARM64) $(WHET_DGST_CROSS_AMD64)

.PHONY: stage-1-diff
stage-1-diff: stage-1-load
	@echo "=== Stage 1 — continuation digest stream identity ==="
	@sha256sum $(WHET_DGST_SAME_ARM64) $(WHET_DGST_SAME_AMD64) \
	           $(WHET_DGST_CROSS_ARM64) $(WHET_DGST_CROSS_AMD64)
	@first=$(WHET_DGST_SAME_ARM64); fail=0; \
	    for f in $(WHET_DGST_SAME_AMD64) $(WHET_DGST_CROSS_ARM64) $(WHET_DGST_CROSS_AMD64); do \
	        if ! diff -u $$first $$f > /tmp/spike-k-diff 2>&1 ; then \
	            echo "DIGEST FAIL — $$f differs from $$first"; \
	            cat /tmp/spike-k-diff; \
	            fail=1; \
	        fi ; \
	    done; \
	    if [ $$fail -eq 0 ]; then \
	        echo "DIGEST PASS — all four whetstone continuations are byte-identical"; \
	    else \
	        exit 1; \
	    fi

.PHONY: stage-1
stage-1: elf-bytes-diff stage-1-buffer-diff stage-1-diff
	@echo "=== Spike K — Stage 1 PASS ==="

# ─── Stage 5: det_shake save → cross-load → digest diff ────────────────────
# Same harness shape as Stage 1.  Save frame is 10 (mid-shake — 13 frames
# remaining, intensity decayed to ~2.4 — matches PLAN.md Stage 5 step 24).

STAGE_5_SAVE_FRAME := 10
STAGE_5_SAVE_ELF   := det_shake_save.elf
STAGE_5_LOAD_ELF   := det_shake_load.elf
STAGE_5_FULL_ELF   := det_shake_full.elf

SHAKE_BUF_ARM64    := $(BUFFERS_DIR)/det_shake.arm64.hex
SHAKE_BUF_AMD64    := $(BUFFERS_DIR)/det_shake.amd64.hex

$(SHAKE_BUF_ARM64): docker-build-arm64 | $(BUFFERS_DIR)
	docker run --rm --platform linux/arm64 $(DOCKER_IMAGE_ARM64) \
	    $(RV32EMU_BIN) $(ELFS_DIR)/$(STAGE_5_SAVE_ELF) $(STAGE_5_SAVE_FRAME) \
	    | grep '^BUFFER ' > $@.tmp
	@if [ ! -s $@.tmp ]; then echo "FAIL: no BUFFER line emitted"; rm -f $@.tmp; exit 1; fi
	@mv $@.tmp $@
	@echo "wrote $@ ($$(wc -c < $@) bytes)"

$(SHAKE_BUF_AMD64): docker-build-amd64 | $(BUFFERS_DIR)
	docker run --rm --platform linux/amd64 $(DOCKER_IMAGE_AMD64) \
	    $(RV32EMU_BIN) $(ELFS_DIR)/$(STAGE_5_SAVE_ELF) $(STAGE_5_SAVE_FRAME) \
	    | grep '^BUFFER ' > $@.tmp
	@if [ ! -s $@.tmp ]; then echo "FAIL: no BUFFER line emitted"; rm -f $@.tmp; exit 1; fi
	@mv $@.tmp $@
	@echo "wrote $@ ($$(wc -c < $@) bytes)"

.PHONY: stage-5-save
stage-5-save: $(SHAKE_BUF_ARM64) $(SHAKE_BUF_AMD64)

.PHONY: stage-5-buffer-diff
stage-5-buffer-diff: stage-5-save
	@echo "=== Stage 5 — buffer byte identity ==="
	@sha256sum $(SHAKE_BUF_ARM64) $(SHAKE_BUF_AMD64)
	@if diff -u $(SHAKE_BUF_ARM64) $(SHAKE_BUF_AMD64); then \
	    echo "BUFFER PASS — det_shake same-host buffers byte-identical across hosts"; \
	else \
	    echo "BUFFER FAIL — screen-shake serializer divergence"; exit 1; \
	fi

SHAKE_DGST_SAME_ARM64  := $(DIGESTS_DIR)/det_shake.same.arm64.f$$(($(STAGE_5_SAVE_FRAME)+1)).txt
SHAKE_DGST_SAME_AMD64  := $(DIGESTS_DIR)/det_shake.same.amd64.f$$(($(STAGE_5_SAVE_FRAME)+1)).txt
SHAKE_DGST_CROSS_ARM64 := $(DIGESTS_DIR)/det_shake.cross.arm64.f$$(($(STAGE_5_SAVE_FRAME)+1)).txt
SHAKE_DGST_CROSS_AMD64 := $(DIGESTS_DIR)/det_shake.cross.amd64.f$$(($(STAGE_5_SAVE_FRAME)+1)).txt
SHAKE_DGST_FULL_ARM64  := $(DIGESTS_DIR)/det_shake_full.arm64.txt
SHAKE_DGST_FULL_AMD64  := $(DIGESTS_DIR)/det_shake_full.amd64.txt

define run_load_det_shake
	docker run --rm --platform $(2) \
	    -v $(abspath $(BUFFERS_DIR)):/spike-k/buffers:ro \
	    $(1) \
	    $(RV32EMU_BIN) $(ELFS_DIR)/$(STAGE_5_LOAD_ELF) /spike-k/buffers/$(notdir $(3)) \
	    | grep '^DIGEST ' > $(4)
	@echo "wrote $(4) ($$$$(wc -l < $(4)) lines)"
endef

$(SHAKE_DGST_SAME_ARM64): $(SHAKE_BUF_ARM64) docker-build-arm64 | $(DIGESTS_DIR)
	$(call run_load_det_shake,$(DOCKER_IMAGE_ARM64),linux/arm64,$(SHAKE_BUF_ARM64),$@)
$(SHAKE_DGST_SAME_AMD64): $(SHAKE_BUF_AMD64) docker-build-amd64 | $(DIGESTS_DIR)
	$(call run_load_det_shake,$(DOCKER_IMAGE_AMD64),linux/amd64,$(SHAKE_BUF_AMD64),$@)
$(SHAKE_DGST_CROSS_ARM64): $(SHAKE_BUF_AMD64) docker-build-arm64 | $(DIGESTS_DIR)
	$(call run_load_det_shake,$(DOCKER_IMAGE_ARM64),linux/arm64,$(SHAKE_BUF_AMD64),$@)
$(SHAKE_DGST_CROSS_AMD64): $(SHAKE_BUF_ARM64) docker-build-amd64 | $(DIGESTS_DIR)
	$(call run_load_det_shake,$(DOCKER_IMAGE_AMD64),linux/amd64,$(SHAKE_BUF_ARM64),$@)

$(SHAKE_DGST_FULL_ARM64): docker-build-arm64 | $(DIGESTS_DIR)
	docker run --rm --platform linux/arm64 $(DOCKER_IMAGE_ARM64) \
	    $(RV32EMU_BIN) $(ELFS_DIR)/$(STAGE_5_FULL_ELF) | grep '^DIGEST ' > $@
	@echo "wrote $@ ($$(wc -l < $@) lines)"
$(SHAKE_DGST_FULL_AMD64): docker-build-amd64 | $(DIGESTS_DIR)
	docker run --rm --platform linux/amd64 $(DOCKER_IMAGE_AMD64) \
	    $(RV32EMU_BIN) $(ELFS_DIR)/$(STAGE_5_FULL_ELF) | grep '^DIGEST ' > $@
	@echo "wrote $@ ($$(wc -l < $@) lines)"

.PHONY: stage-5-load
stage-5-load: $(SHAKE_DGST_SAME_ARM64) $(SHAKE_DGST_SAME_AMD64) \
              $(SHAKE_DGST_CROSS_ARM64) $(SHAKE_DGST_CROSS_AMD64) \
              $(SHAKE_DGST_FULL_ARM64) $(SHAKE_DGST_FULL_AMD64)

.PHONY: stage-5-diff
stage-5-diff: stage-5-load
	@echo "=== Stage 5 — continuation digest stream identity ==="
	@sha256sum $(SHAKE_DGST_SAME_ARM64) $(SHAKE_DGST_SAME_AMD64) \
	           $(SHAKE_DGST_CROSS_ARM64) $(SHAKE_DGST_CROSS_AMD64) \
	           $(SHAKE_DGST_FULL_ARM64) $(SHAKE_DGST_FULL_AMD64)
	@first=$(SHAKE_DGST_SAME_ARM64); fail=0; \
	    for f in $(SHAKE_DGST_SAME_AMD64) $(SHAKE_DGST_CROSS_ARM64) $(SHAKE_DGST_CROSS_AMD64); do \
	        if ! diff -u $$first $$f > /tmp/spike-k-diff 2>&1 ; then \
	            echo "DIGEST FAIL — $$f differs from $$first"; cat /tmp/spike-k-diff; fail=1; \
	        fi ; \
	    done; \
	    if [ $$fail -eq 0 ]; then \
	        echo "DIGEST PASS — all four det_shake continuations are byte-identical"; \
	    else exit 1; fi
	@echo "=== Stage 5 — strong gate (continuation == straight-through suffix) ==="
	@first=$(SHAKE_DGST_SAME_ARM64); \
	tail_lines=$$(wc -l < $$first); \
	tail -n $$tail_lines $(SHAKE_DGST_FULL_ARM64) > /tmp/spike-k-shake-tail; \
	if diff -u /tmp/spike-k-shake-tail $$first > /tmp/spike-k-strong 2>&1; then \
	    echo "STRONG PASS — det_shake continuation byte-equal to straight-through frame-$$(($(STAGE_5_SAVE_FRAME)+1))+"; \
	else \
	    echo "STRONG FAIL — see diff above"; cat /tmp/spike-k-strong; exit 1; \
	fi

.PHONY: stage-5
stage-5: elf-bytes-diff stage-5-buffer-diff stage-5-diff
	@echo "=== Spike K — Stage 5 PASS ==="

# ─── Stage 3: det_audio_branch save → cross-load → digest diff ──────────────
# Two save points exercised:
#   • frame 11 (default) — post-application: pending FIFO empty,
#     logical_view_bits = 0b10.  PLAN.md Stage 3 step 15.
#   • frame 5            — pending-application: pending FIFO has voice 1's
#     end event, logical_view_bits still 0b11.  PLAN.md Stage 3 step 17.

STAGE_3_SAVE_ELF := det_audio_branch_save.elf
STAGE_3_LOAD_ELF := det_audio_branch_load.elf
STAGE_3_FULL_ELF := det_audio_branch_full.elf

# Save point 1: frame 11 (post-application)
AUDIO_BUF1_ARM64 := $(BUFFERS_DIR)/det_audio_branch.f11.arm64.hex
AUDIO_BUF1_AMD64 := $(BUFFERS_DIR)/det_audio_branch.f11.amd64.hex
# Save point 2: frame 5 (pending-application)
AUDIO_BUF2_ARM64 := $(BUFFERS_DIR)/det_audio_branch.f5.arm64.hex
AUDIO_BUF2_AMD64 := $(BUFFERS_DIR)/det_audio_branch.f5.amd64.hex

define run_audio_save
$(1): docker-build-$(2) | $(BUFFERS_DIR)
	docker run --rm --platform linux/$(2) $(DOCKER_IMAGE_$(3)) \
	    $(RV32EMU_BIN) $(ELFS_DIR)/$(STAGE_3_SAVE_ELF) $(4) \
	    | grep '^BUFFER ' > $$@.tmp
	@if [ ! -s $$@.tmp ]; then echo "FAIL: no BUFFER line emitted"; rm -f $$@.tmp; exit 1; fi
	@mv $$@.tmp $$@
	@echo "wrote $$@ ($$$$(wc -c < $$@) bytes)"
endef
$(eval $(call run_audio_save,$(AUDIO_BUF1_ARM64),arm64,ARM64,11))
$(eval $(call run_audio_save,$(AUDIO_BUF1_AMD64),amd64,AMD64,11))
$(eval $(call run_audio_save,$(AUDIO_BUF2_ARM64),arm64,ARM64,5))
$(eval $(call run_audio_save,$(AUDIO_BUF2_AMD64),amd64,AMD64,5))

.PHONY: stage-3-save
stage-3-save: $(AUDIO_BUF1_ARM64) $(AUDIO_BUF1_AMD64) $(AUDIO_BUF2_ARM64) $(AUDIO_BUF2_AMD64)

.PHONY: stage-3-buffer-diff
stage-3-buffer-diff: stage-3-save
	@echo "=== Stage 3 — buffer byte identity (post-application save, frame 11) ==="
	@sha256sum $(AUDIO_BUF1_ARM64) $(AUDIO_BUF1_AMD64)
	@diff -u $(AUDIO_BUF1_ARM64) $(AUDIO_BUF1_AMD64) \
	    && echo "BUFFER PASS — det_audio_branch f11 buffers byte-identical" \
	    || (echo "BUFFER FAIL — voice-end queue serializer divergence at f11"; exit 1)
	@echo "=== Stage 3 — buffer byte identity (pending-application save, frame 5) ==="
	@sha256sum $(AUDIO_BUF2_ARM64) $(AUDIO_BUF2_AMD64)
	@diff -u $(AUDIO_BUF2_ARM64) $(AUDIO_BUF2_AMD64) \
	    && echo "BUFFER PASS — det_audio_branch f5 buffers byte-identical" \
	    || (echo "BUFFER FAIL — pending-FIFO serialization divergence at f5"; exit 1)

# 4-way matrix per save point (8 digest files total + 2 full streams).
AUDIO_DGST_F11_SAME_ARM64  := $(DIGESTS_DIR)/det_audio_branch.f11.same.arm64.f12.txt
AUDIO_DGST_F11_SAME_AMD64  := $(DIGESTS_DIR)/det_audio_branch.f11.same.amd64.f12.txt
AUDIO_DGST_F11_CROSS_ARM64 := $(DIGESTS_DIR)/det_audio_branch.f11.cross.arm64.f12.txt
AUDIO_DGST_F11_CROSS_AMD64 := $(DIGESTS_DIR)/det_audio_branch.f11.cross.amd64.f12.txt
AUDIO_DGST_F5_SAME_ARM64   := $(DIGESTS_DIR)/det_audio_branch.f5.same.arm64.f6.txt
AUDIO_DGST_F5_SAME_AMD64   := $(DIGESTS_DIR)/det_audio_branch.f5.same.amd64.f6.txt
AUDIO_DGST_F5_CROSS_ARM64  := $(DIGESTS_DIR)/det_audio_branch.f5.cross.arm64.f6.txt
AUDIO_DGST_F5_CROSS_AMD64  := $(DIGESTS_DIR)/det_audio_branch.f5.cross.amd64.f6.txt
AUDIO_DGST_FULL_ARM64      := $(DIGESTS_DIR)/det_audio_branch_full.arm64.txt
AUDIO_DGST_FULL_AMD64      := $(DIGESTS_DIR)/det_audio_branch_full.amd64.txt

define run_audio_load
$(1): $(3) docker-build-$(2) | $(DIGESTS_DIR)
	docker run --rm --platform linux/$(2) \
	    -v $$(abspath $(BUFFERS_DIR)):/spike-k/buffers:ro \
	    $(DOCKER_IMAGE_$(4)) \
	    $(RV32EMU_BIN) $(ELFS_DIR)/$(STAGE_3_LOAD_ELF) /spike-k/buffers/$$(notdir $(3)) \
	    | grep '^DIGEST ' > $$@
	@echo "wrote $$@ ($$$$(wc -l < $$@) lines)"
endef
$(eval $(call run_audio_load,$(AUDIO_DGST_F11_SAME_ARM64),arm64,$(AUDIO_BUF1_ARM64),ARM64))
$(eval $(call run_audio_load,$(AUDIO_DGST_F11_SAME_AMD64),amd64,$(AUDIO_BUF1_AMD64),AMD64))
$(eval $(call run_audio_load,$(AUDIO_DGST_F11_CROSS_ARM64),arm64,$(AUDIO_BUF1_AMD64),ARM64))
$(eval $(call run_audio_load,$(AUDIO_DGST_F11_CROSS_AMD64),amd64,$(AUDIO_BUF1_ARM64),AMD64))
$(eval $(call run_audio_load,$(AUDIO_DGST_F5_SAME_ARM64),arm64,$(AUDIO_BUF2_ARM64),ARM64))
$(eval $(call run_audio_load,$(AUDIO_DGST_F5_SAME_AMD64),amd64,$(AUDIO_BUF2_AMD64),AMD64))
$(eval $(call run_audio_load,$(AUDIO_DGST_F5_CROSS_ARM64),arm64,$(AUDIO_BUF2_AMD64),ARM64))
$(eval $(call run_audio_load,$(AUDIO_DGST_F5_CROSS_AMD64),amd64,$(AUDIO_BUF2_ARM64),AMD64))

$(AUDIO_DGST_FULL_ARM64): docker-build-arm64 | $(DIGESTS_DIR)
	docker run --rm --platform linux/arm64 $(DOCKER_IMAGE_ARM64) \
	    $(RV32EMU_BIN) $(ELFS_DIR)/$(STAGE_3_FULL_ELF) | grep '^DIGEST ' > $@
	@echo "wrote $@ ($$(wc -l < $@) lines)"
$(AUDIO_DGST_FULL_AMD64): docker-build-amd64 | $(DIGESTS_DIR)
	docker run --rm --platform linux/amd64 $(DOCKER_IMAGE_AMD64) \
	    $(RV32EMU_BIN) $(ELFS_DIR)/$(STAGE_3_FULL_ELF) | grep '^DIGEST ' > $@
	@echo "wrote $@ ($$(wc -l < $@) lines)"

.PHONY: stage-3-load
stage-3-load: $(AUDIO_DGST_F11_SAME_ARM64) $(AUDIO_DGST_F11_SAME_AMD64) \
              $(AUDIO_DGST_F11_CROSS_ARM64) $(AUDIO_DGST_F11_CROSS_AMD64) \
              $(AUDIO_DGST_F5_SAME_ARM64) $(AUDIO_DGST_F5_SAME_AMD64) \
              $(AUDIO_DGST_F5_CROSS_ARM64) $(AUDIO_DGST_F5_CROSS_AMD64) \
              $(AUDIO_DGST_FULL_ARM64) $(AUDIO_DGST_FULL_AMD64)

.PHONY: stage-3-diff
stage-3-diff: stage-3-load
	@echo "=== Stage 3 — continuation digest stream identity (f11 save) ==="
	@sha256sum $(AUDIO_DGST_F11_SAME_ARM64) $(AUDIO_DGST_F11_SAME_AMD64) \
	           $(AUDIO_DGST_F11_CROSS_ARM64) $(AUDIO_DGST_F11_CROSS_AMD64)
	@first=$(AUDIO_DGST_F11_SAME_ARM64); fail=0; \
	    for f in $(AUDIO_DGST_F11_SAME_AMD64) $(AUDIO_DGST_F11_CROSS_ARM64) $(AUDIO_DGST_F11_CROSS_AMD64); do \
	        diff -u $$first $$f > /tmp/spike-k-diff 2>&1 || { echo "DIGEST FAIL — $$f"; cat /tmp/spike-k-diff; fail=1; }; \
	    done; [ $$fail -eq 0 ] && echo "DIGEST PASS — det_audio_branch f11 4-way byte-identical" || exit 1
	@echo "=== Stage 3 — continuation digest stream identity (f5 save, pending FIFO) ==="
	@sha256sum $(AUDIO_DGST_F5_SAME_ARM64) $(AUDIO_DGST_F5_SAME_AMD64) \
	           $(AUDIO_DGST_F5_CROSS_ARM64) $(AUDIO_DGST_F5_CROSS_AMD64)
	@first=$(AUDIO_DGST_F5_SAME_ARM64); fail=0; \
	    for f in $(AUDIO_DGST_F5_SAME_AMD64) $(AUDIO_DGST_F5_CROSS_ARM64) $(AUDIO_DGST_F5_CROSS_AMD64); do \
	        diff -u $$first $$f > /tmp/spike-k-diff 2>&1 || { echo "DIGEST FAIL — $$f"; cat /tmp/spike-k-diff; fail=1; }; \
	    done; [ $$fail -eq 0 ] && echo "DIGEST PASS — det_audio_branch f5 4-way byte-identical" || exit 1
	@echo "=== Stage 3 — strong gate (continuations == straight-through suffix) ==="
	@first=$(AUDIO_DGST_F11_SAME_ARM64); tail_lines=$$(wc -l < $$first); \
	    tail -n $$tail_lines $(AUDIO_DGST_FULL_ARM64) > /tmp/spike-k-audio-tail; \
	    diff -u /tmp/spike-k-audio-tail $$first > /tmp/spike-k-strong 2>&1 \
	        && echo "STRONG PASS — det_audio_branch f11 continuation matches straight-through" \
	        || (echo "STRONG FAIL"; cat /tmp/spike-k-strong; exit 1)
	@first=$(AUDIO_DGST_F5_SAME_ARM64); tail_lines=$$(wc -l < $$first); \
	    tail -n $$tail_lines $(AUDIO_DGST_FULL_ARM64) > /tmp/spike-k-audio-tail; \
	    diff -u /tmp/spike-k-audio-tail $$first > /tmp/spike-k-strong 2>&1 \
	        && echo "STRONG PASS — det_audio_branch f5 continuation matches straight-through" \
	        || (echo "STRONG FAIL"; cat /tmp/spike-k-strong; exit 1)

.PHONY: stage-3
stage-3: elf-bytes-diff stage-3-buffer-diff stage-3-diff
	@echo "=== Spike K — Stage 3 PASS ==="

# ─── Stage 4: det_cutscene save → cross-load → digest diff ──────────────────
# Coroutine save-blob region exercised by a C cart that simulates the
# blyt32.coroutine.create{start, save, restore} pattern via the slot
# table.  PLAN.md Stage 4 step 20.

STAGE_4_SAVE_FRAME := 7
STAGE_4_SAVE_ELF := det_cutscene_save.elf
STAGE_4_LOAD_ELF := det_cutscene_load.elf
STAGE_4_FULL_ELF := det_cutscene_full.elf

CUTS_BUF_ARM64 := $(BUFFERS_DIR)/det_cutscene.arm64.hex
CUTS_BUF_AMD64 := $(BUFFERS_DIR)/det_cutscene.amd64.hex

$(CUTS_BUF_ARM64): docker-build-arm64 | $(BUFFERS_DIR)
	docker run --rm --platform linux/arm64 $(DOCKER_IMAGE_ARM64) \
	    $(RV32EMU_BIN) $(ELFS_DIR)/$(STAGE_4_SAVE_ELF) $(STAGE_4_SAVE_FRAME) \
	    | grep '^BUFFER ' > $@.tmp
	@if [ ! -s $@.tmp ]; then echo "FAIL: no BUFFER line emitted"; rm -f $@.tmp; exit 1; fi
	@mv $@.tmp $@
	@echo "wrote $@ ($$(wc -c < $@) bytes)"
$(CUTS_BUF_AMD64): docker-build-amd64 | $(BUFFERS_DIR)
	docker run --rm --platform linux/amd64 $(DOCKER_IMAGE_AMD64) \
	    $(RV32EMU_BIN) $(ELFS_DIR)/$(STAGE_4_SAVE_ELF) $(STAGE_4_SAVE_FRAME) \
	    | grep '^BUFFER ' > $@.tmp
	@if [ ! -s $@.tmp ]; then echo "FAIL: no BUFFER line emitted"; rm -f $@.tmp; exit 1; fi
	@mv $@.tmp $@
	@echo "wrote $@ ($$(wc -c < $@) bytes)"

.PHONY: stage-4-save
stage-4-save: $(CUTS_BUF_ARM64) $(CUTS_BUF_AMD64)

.PHONY: stage-4-buffer-diff
stage-4-buffer-diff: stage-4-save
	@echo "=== Stage 4 — buffer byte identity ==="
	@sha256sum $(CUTS_BUF_ARM64) $(CUTS_BUF_AMD64)
	@diff -u $(CUTS_BUF_ARM64) $(CUTS_BUF_AMD64) \
	    && echo "BUFFER PASS — det_cutscene buffers byte-identical" \
	    || (echo "BUFFER FAIL — coroutine save-blob serializer divergence"; exit 1)

CUTS_DGST_SAME_ARM64  := $(DIGESTS_DIR)/det_cutscene.same.arm64.f$$(($(STAGE_4_SAVE_FRAME)+1)).txt
CUTS_DGST_SAME_AMD64  := $(DIGESTS_DIR)/det_cutscene.same.amd64.f$$(($(STAGE_4_SAVE_FRAME)+1)).txt
CUTS_DGST_CROSS_ARM64 := $(DIGESTS_DIR)/det_cutscene.cross.arm64.f$$(($(STAGE_4_SAVE_FRAME)+1)).txt
CUTS_DGST_CROSS_AMD64 := $(DIGESTS_DIR)/det_cutscene.cross.amd64.f$$(($(STAGE_4_SAVE_FRAME)+1)).txt
CUTS_DGST_FULL_ARM64  := $(DIGESTS_DIR)/det_cutscene_full.arm64.txt
CUTS_DGST_FULL_AMD64  := $(DIGESTS_DIR)/det_cutscene_full.amd64.txt

define run_load_det_cutscene
	docker run --rm --platform $(2) \
	    -v $(abspath $(BUFFERS_DIR)):/spike-k/buffers:ro \
	    $(1) \
	    $(RV32EMU_BIN) $(ELFS_DIR)/$(STAGE_4_LOAD_ELF) /spike-k/buffers/$(notdir $(3)) \
	    | grep '^DIGEST ' > $(4)
	@echo "wrote $(4) ($$$$(wc -l < $(4)) lines)"
endef

$(CUTS_DGST_SAME_ARM64): $(CUTS_BUF_ARM64) docker-build-arm64 | $(DIGESTS_DIR)
	$(call run_load_det_cutscene,$(DOCKER_IMAGE_ARM64),linux/arm64,$(CUTS_BUF_ARM64),$@)
$(CUTS_DGST_SAME_AMD64): $(CUTS_BUF_AMD64) docker-build-amd64 | $(DIGESTS_DIR)
	$(call run_load_det_cutscene,$(DOCKER_IMAGE_AMD64),linux/amd64,$(CUTS_BUF_AMD64),$@)
$(CUTS_DGST_CROSS_ARM64): $(CUTS_BUF_AMD64) docker-build-arm64 | $(DIGESTS_DIR)
	$(call run_load_det_cutscene,$(DOCKER_IMAGE_ARM64),linux/arm64,$(CUTS_BUF_AMD64),$@)
$(CUTS_DGST_CROSS_AMD64): $(CUTS_BUF_ARM64) docker-build-amd64 | $(DIGESTS_DIR)
	$(call run_load_det_cutscene,$(DOCKER_IMAGE_AMD64),linux/amd64,$(CUTS_BUF_ARM64),$@)

$(CUTS_DGST_FULL_ARM64): docker-build-arm64 | $(DIGESTS_DIR)
	docker run --rm --platform linux/arm64 $(DOCKER_IMAGE_ARM64) \
	    $(RV32EMU_BIN) $(ELFS_DIR)/$(STAGE_4_FULL_ELF) | grep '^DIGEST ' > $@
	@echo "wrote $@ ($$(wc -l < $@) lines)"
$(CUTS_DGST_FULL_AMD64): docker-build-amd64 | $(DIGESTS_DIR)
	docker run --rm --platform linux/amd64 $(DOCKER_IMAGE_AMD64) \
	    $(RV32EMU_BIN) $(ELFS_DIR)/$(STAGE_4_FULL_ELF) | grep '^DIGEST ' > $@
	@echo "wrote $@ ($$(wc -l < $@) lines)"

.PHONY: stage-4-load
stage-4-load: $(CUTS_DGST_SAME_ARM64) $(CUTS_DGST_SAME_AMD64) \
              $(CUTS_DGST_CROSS_ARM64) $(CUTS_DGST_CROSS_AMD64) \
              $(CUTS_DGST_FULL_ARM64) $(CUTS_DGST_FULL_AMD64)

.PHONY: stage-4-diff
stage-4-diff: stage-4-load
	@echo "=== Stage 4 — continuation digest stream identity ==="
	@sha256sum $(CUTS_DGST_SAME_ARM64) $(CUTS_DGST_SAME_AMD64) \
	           $(CUTS_DGST_CROSS_ARM64) $(CUTS_DGST_CROSS_AMD64) \
	           $(CUTS_DGST_FULL_ARM64) $(CUTS_DGST_FULL_AMD64)
	@first=$(CUTS_DGST_SAME_ARM64); fail=0; \
	    for f in $(CUTS_DGST_SAME_AMD64) $(CUTS_DGST_CROSS_ARM64) $(CUTS_DGST_CROSS_AMD64); do \
	        diff -u $$first $$f > /tmp/spike-k-diff 2>&1 || { echo "DIGEST FAIL — $$f"; cat /tmp/spike-k-diff; fail=1; }; \
	    done; [ $$fail -eq 0 ] && echo "DIGEST PASS — det_cutscene 4-way byte-identical" || exit 1
	@echo "=== Stage 4 — strong gate (continuation == straight-through suffix) ==="
	@first=$(CUTS_DGST_SAME_ARM64); tail_lines=$$(wc -l < $$first); \
	    tail -n $$tail_lines $(CUTS_DGST_FULL_ARM64) > /tmp/spike-k-cuts-tail; \
	    diff -u /tmp/spike-k-cuts-tail $$first > /tmp/spike-k-strong 2>&1 \
	        && echo "STRONG PASS — det_cutscene continuation matches straight-through" \
	        || (echo "STRONG FAIL"; cat /tmp/spike-k-strong; exit 1)

.PHONY: stage-4
stage-4: elf-bytes-diff stage-4-buffer-diff stage-4-diff
	@echo "=== Spike K — Stage 4 PASS ==="

# ─── corruption-detection tests (Stage 6 step 27) ────────────────────────────
# Runs the in-cart negative-test driver in each Docker image and checks every
# expected rejection fires.  The cart prints `CORRUPTION PASS` lines on success
# and a summary line; any FAIL line trips the harness.

.PHONY: corruption-tests corruption-tests-arm64 corruption-tests-amd64

corruption-tests-arm64: docker-build-arm64
	@echo "=== Corruption tests — arm64 ==="
	@out=$$(docker run --rm --platform linux/arm64 $(DOCKER_IMAGE_ARM64) \
	    $(RV32EMU_BIN) $(ELFS_DIR)/corruption_tests.elf); \
	    echo "$$out"; \
	    if echo "$$out" | grep -q '^CORRUPTION FAIL'; then \
	        echo "CORRUPTION TESTS FAIL on arm64"; exit 1; \
	    fi

corruption-tests-amd64: docker-build-amd64
	@echo "=== Corruption tests — amd64 ==="
	@out=$$(docker run --rm --platform linux/amd64 $(DOCKER_IMAGE_AMD64) \
	    $(RV32EMU_BIN) $(ELFS_DIR)/corruption_tests.elf); \
	    echo "$$out"; \
	    if echo "$$out" | grep -q '^CORRUPTION FAIL'; then \
	        echo "CORRUPTION TESTS FAIL on amd64"; exit 1; \
	    fi

corruption-tests: corruption-tests-arm64 corruption-tests-amd64
	@echo "=== Corruption tests PASS on both hosts ==="

# ─── all (only Stage 1 wired in this commit) ─────────────────────────────────

.PHONY: all
all: stage-1 stage-3 stage-4 stage-5 corruption-tests

# ─── clean ───────────────────────────────────────────────────────────────────

.PHONY: clean
clean:
	$(RM) -r $(DIGESTS_DIR) $(BUFFERS_DIR)
	docker image rm -f $(DOCKER_IMAGE_ARM64) $(DOCKER_IMAGE_AMD64) 2>/dev/null || true
