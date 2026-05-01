## Spike B — Lua 5.4 throughput under rv32emu (RV32IMFC) on Pi-class hardware.
##
## Targets:
##   make docker-build          Build spike-b Docker image (uncapped)
##   make docker-shell          Drop into the spike-b container
##   make docker-bench-lua      Run the 6 standard Lua benchmarks
##   make docker-bench-entity   Run the entity-update workload at 64/256/1024
##   make docker-bench          Both of the above
##   make docker-build-capped   Build with ADR-0082 MIPS cap (MIPS_CAP=N)
##   make docker-bench-lua-capped
##   make docker-bench-entity-capped
##   make clean                 Remove host-side build artefacts
##
## Pre-requisite: spike-a must be built first.  We use its image as the
## base for the spike-b image and inherit its rv32emu binary + softfloat lib.
##   make -C ../spike-a docker-build           # uncapped
##   make -C ../spike-a docker-build-capped    # capped (with MIPS_CAP=N)

DOCKER_IMAGE      ?= fc32-spike-b
DOCKER_IMAGE_CAP  ?= fc32-spike-b-capped
SPIKE_A_IMAGE     ?= fc32-spike-a
SPIKE_A_IMAGE_CAP ?= fc32-spike-a-capped
DOCKER_PLATFORM   := linux/arm64
MIPS_CAP          ?= 500

RV32EMU_BIN := /spike-a/rv32emu/build/rv32emu
ELFS_DIR    := /spike-b/elfs

# ── uncapped build ───────────────────────────────────────────────────────────

.PHONY: docker-build
docker-build:
	$(MAKE) -C ../spike-a docker-build
	docker build --platform $(DOCKER_PLATFORM) \
	    --build-arg SPIKE_A_IMAGE=$(SPIKE_A_IMAGE) \
	    -t $(DOCKER_IMAGE) .

.PHONY: docker-shell
docker-shell: docker-build
	docker run --rm -it --platform $(DOCKER_PLATFORM) $(DOCKER_IMAGE) /bin/bash

# ── capped build (ADR-0082) ──────────────────────────────────────────────────

.PHONY: docker-build-capped
docker-build-capped:
	$(MAKE) -C ../spike-a docker-build-capped MIPS_CAP=$(MIPS_CAP)
	docker build --platform $(DOCKER_PLATFORM) \
	    --build-arg SPIKE_A_IMAGE=$(SPIKE_A_IMAGE_CAP) \
	    -t $(DOCKER_IMAGE_CAP) .

# ── benchmarks ───────────────────────────────────────────────────────────────

LUA_BENCHES := binarytrees fannkuch fasta mandelbrot nbody spectral-norm

# Helper: run a single benchmark ELF inside the container.  $(1) = image,
# $(2) = elf basename (no extension).
define run_one
	docker run --rm --platform $(DOCKER_PLATFORM) $(1) \
	    $(RV32EMU_BIN) $(ELFS_DIR)/lua_cart_$(2).elf

endef

.PHONY: docker-bench-lua
docker-bench-lua: docker-build
	@echo "=== Lua benchmark suite (RV32IMFC under rv32emu, arm64 Docker, uncapped) ==="
	$(foreach b,$(LUA_BENCHES),$(call run_one,$(DOCKER_IMAGE),$(b)))

.PHONY: docker-bench-entity
docker-bench-entity: docker-build
	@echo "=== Entity-update workload (uncapped) ==="
	$(call run_one,$(DOCKER_IMAGE),entity_update)

.PHONY: docker-bench-doom
docker-bench-doom: docker-build
	@echo "=== Doom-shaped tick workload (Lua, uncapped) ==="
	$(call run_one,$(DOCKER_IMAGE),doom_tick)

.PHONY: docker-bench-doom-c
docker-bench-doom-c: docker-build
	@echo "=== Doom-shaped tick workload (native C, uncapped) ==="
	docker run --rm --platform $(DOCKER_PLATFORM) $(DOCKER_IMAGE) \
	    $(RV32EMU_BIN) $(ELFS_DIR)/doom_tick_c.elf

.PHONY: docker-bench
docker-bench: docker-bench-lua docker-bench-entity docker-bench-doom docker-bench-doom-c

.PHONY: docker-bench-lua-capped
docker-bench-lua-capped: docker-build-capped
	@echo "=== Lua benchmarks (capped at $(MIPS_CAP) MIPS) ==="
	$(foreach b,$(LUA_BENCHES),$(call run_one,$(DOCKER_IMAGE_CAP),$(b)))

.PHONY: docker-bench-entity-capped
docker-bench-entity-capped: docker-build-capped
	@echo "=== Entity-update workload (capped at $(MIPS_CAP) MIPS) ==="
	$(call run_one,$(DOCKER_IMAGE_CAP),entity_update)

.PHONY: docker-bench-doom-capped
docker-bench-doom-capped: docker-build-capped
	@echo "=== Doom-shaped tick workload (capped at $(MIPS_CAP) MIPS) ==="
	$(call run_one,$(DOCKER_IMAGE_CAP),doom_tick)

# ── clean ────────────────────────────────────────────────────────────────────

.PHONY: clean
clean:
	docker image rm -f $(DOCKER_IMAGE) $(DOCKER_IMAGE_CAP) 2>/dev/null || true
