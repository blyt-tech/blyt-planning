## Spike D — cross-host bit-identity for the cart workload.
##
## Builds two Docker images (linux/arm64 and linux/amd64) from the same
## source tree, runs the three cart ELFs in each, and diffs the per-frame
## DIGEST streams.  See PLAN.md for the question this spike answers.
##
## Top-level targets:
##   make docker-build            Build both arm64 and amd64 images
##   make docker-build-arm64      arm64 image only
##   make docker-build-amd64      amd64 image only (qemu-user-amd64)
##   make elf-bytes               Hash the cart ELFs in each image and diff
##   make run                     Run carts in both images, write digests/<arch>.txt
##   make diff                    Diff the two digest files; PASS/FAIL exit
##   make all                     elf-bytes + run + diff
##   make shell-arm64             Drop into the arm64 image
##   make shell-amd64             Drop into the amd64 image
##   make clean                   Remove host-side digest/elf artefacts

DOCKER_IMAGE_ARM64 ?= fc32-spike-d-arm64
DOCKER_IMAGE_AMD64 ?= fc32-spike-d-amd64
SPIKE_B_IMAGE_ARM  ?= fc32-spike-b-arm64
SPIKE_B_IMAGE_AMD  ?= fc32-spike-b-amd64

RV32EMU_BIN := /spike-a/rv32emu/build/rv32emu
ELFS_DIR    := /spike-d/elfs

CARTS := lua_cart_det_doom_tick.elf lua_cart_det_entity_update.elf whetstone.elf

DIGESTS_DIR := digests

.PHONY: docker-build docker-build-arm64 docker-build-amd64
docker-build: docker-build-arm64 docker-build-amd64

# Spike-b's image is built first (it inherits from spike-a).  We invoke
# the spike-b makefile with arch-tagged image names so each platform gets
# its own image.

docker-build-arm64:
	$(MAKE) -C ../spike-b docker-build \
	    DOCKER_IMAGE=$(SPIKE_B_IMAGE_ARM) \
	    DOCKER_PLATFORM=linux/arm64
	docker build --platform linux/arm64 \
	    --build-arg SPIKE_B_IMAGE=$(SPIKE_B_IMAGE_ARM) \
	    -t $(DOCKER_IMAGE_ARM64) .

docker-build-amd64:
	# Spike-a build under qemu-user-amd64 hits a GCC LTO crash; disable LTO
	# via FC32_DISABLE_LTO=1.  Build spike-a, spike-b, spike-d directly here
	# (skipping spike-a/spike-b makefile cascades) so we can thread the
	# build-arg cleanly.
	docker build --platform linux/amd64 \
	    --build-arg FC32_DISABLE_LTO=1 \
	    -t fc32-spike-a-amd64 ../spike-a
	docker build --platform linux/amd64 \
	    --build-arg SPIKE_A_IMAGE=fc32-spike-a-amd64 \
	    -t $(SPIKE_B_IMAGE_AMD) ../spike-b
	docker build --platform linux/amd64 \
	    --build-arg SPIKE_B_IMAGE=$(SPIKE_B_IMAGE_AMD) \
	    -t $(DOCKER_IMAGE_AMD64) .

# ── shells ───────────────────────────────────────────────────────────────────

.PHONY: shell-arm64 shell-amd64
shell-arm64: docker-build-arm64
	docker run --rm -it --platform linux/arm64 $(DOCKER_IMAGE_ARM64) /bin/bash

shell-amd64: docker-build-amd64
	docker run --rm -it --platform linux/amd64 $(DOCKER_IMAGE_AMD64) /bin/bash

# ── ELF byte-identity check ──────────────────────────────────────────────────
# This is the cheap first check.  If the cart ELFs differ across hosts,
# the runtime comparison is moot: the toolchain is producing different
# binaries.  Resolve by pinning the cross-compiler version explicitly.

.PHONY: elf-bytes elf-bytes-arm64 elf-bytes-amd64 elf-bytes-diff

ELF_HASH_ARM64 := $(DIGESTS_DIR)/elfs.arm64.sha256
ELF_HASH_AMD64 := $(DIGESTS_DIR)/elfs.amd64.sha256

$(DIGESTS_DIR):
	mkdir -p $@

elf-bytes-arm64: docker-build-arm64 | $(DIGESTS_DIR)
	@docker run --rm --platform linux/arm64 $(DOCKER_IMAGE_ARM64) \
	    sh -c 'cd $(ELFS_DIR) && sha256sum $(CARTS)' \
	    | sort > $(ELF_HASH_ARM64)
	@echo "wrote $(ELF_HASH_ARM64):"
	@cat $(ELF_HASH_ARM64)

elf-bytes-amd64: docker-build-amd64 | $(DIGESTS_DIR)
	@docker run --rm --platform linux/amd64 $(DOCKER_IMAGE_AMD64) \
	    sh -c 'cd $(ELFS_DIR) && sha256sum $(CARTS)' \
	    | sort > $(ELF_HASH_AMD64)
	@echo "wrote $(ELF_HASH_AMD64):"
	@cat $(ELF_HASH_AMD64)

elf-bytes-diff: elf-bytes-arm64 elf-bytes-amd64
	@echo "=== ELF byte identity check ==="
	@if diff -u $(ELF_HASH_ARM64) $(ELF_HASH_AMD64); then \
	    echo "ELF-BYTES PASS — cart binaries are byte-identical across hosts"; \
	else \
	    echo "ELF-BYTES FAIL — toolchain drift; pin cross-compiler before running carts"; \
	    exit 1; \
	fi

elf-bytes: elf-bytes-diff

# ── digest run ───────────────────────────────────────────────────────────────

.PHONY: run run-arm64 run-amd64

DIGESTS_ARM64 := $(DIGESTS_DIR)/digests.arm64.txt
DIGESTS_AMD64 := $(DIGESTS_DIR)/digests.amd64.txt

# The container invocation runs each cart in turn, prefixing a `=== <name> ===`
# header.  The cart itself emits its own header line plus the DIGEST stream;
# we redirect both to a single host-side file.
define run_carts
	@docker run --rm --platform $(2) $(1) sh -c '\
	    for f in $(CARTS); do \
	        $(RV32EMU_BIN) $(ELFS_DIR)/$$f; \
	    done' > $(3)
	@echo "wrote $(3)  ($$(wc -l < $(3)) lines)"
endef

run-arm64: docker-build-arm64 | $(DIGESTS_DIR)
	$(call run_carts,$(DOCKER_IMAGE_ARM64),linux/arm64,$(DIGESTS_ARM64))

run-amd64: docker-build-amd64 | $(DIGESTS_DIR)
	$(call run_carts,$(DOCKER_IMAGE_AMD64),linux/amd64,$(DIGESTS_AMD64))

run: run-arm64 run-amd64

# ── digest diff ──────────────────────────────────────────────────────────────

.PHONY: diff
diff: $(DIGESTS_ARM64) $(DIGESTS_AMD64)
	@echo "=== Digest stream diff ==="
	@sha256sum $(DIGESTS_ARM64) $(DIGESTS_AMD64)
	@if diff -u $(DIGESTS_ARM64) $(DIGESTS_AMD64); then \
	    echo "DIGEST PASS — per-frame digest streams are bit-identical across hosts"; \
	else \
	    echo "DIGEST FAIL — see diff above; first divergent frame names the layer"; \
	    exit 1; \
	fi

# ── all ──────────────────────────────────────────────────────────────────────

.PHONY: all
all: elf-bytes run diff

# ── clean ────────────────────────────────────────────────────────────────────

.PHONY: clean
clean:
	$(RM) -r $(DIGESTS_DIR)
	docker image rm -f $(DOCKER_IMAGE_ARM64) $(DOCKER_IMAGE_AMD64) 2>/dev/null || true
