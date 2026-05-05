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
ALL_ELFS := whetstone_save.elf whetstone_load.elf corruption_tests.elf

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
all: stage-1 corruption-tests

# ─── clean ───────────────────────────────────────────────────────────────────

.PHONY: clean
clean:
	$(RM) -r $(DIGESTS_DIR) $(BUFFERS_DIR)
	docker image rm -f $(DOCKER_IMAGE_ARM64) $(DOCKER_IMAGE_AMD64) 2>/dev/null || true
