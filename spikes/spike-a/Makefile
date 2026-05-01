## Spike A — RV32IMFC interpreter throughput on Cortex-A53
##
## Targets:
##   make native            Build rv32emu for the local host (smoke-test, macOS)
##   make docker-build      Build rv32emu + CoreMark + Embench inside an arm64 Docker container
##   make docker-shell      Drop into the arm64 container (rv32emu + benchmarks built)
##   make docker-bench      Run CoreMark under rv32emu inside Docker, print results
##   make docker-embench    Run all Embench benchmarks under rv32emu inside Docker
##   make aarch64           Cross-compile rv32emu for aarch64-linux-gnu (QEMU path)
##   make clean             Remove all build artefacts
##
## Recommended path on Apple Silicon (arm64 Docker, no cross-compiler needed):
##   make docker-bench      # builds everything and runs CoreMark
##   make docker-embench    # runs all 18 Embench benchmarks
##   make docker-shell      # interactive shell for exploration
##
## QEMU path (if you prefer it):
##   brew install qemu
##   brew install messense/macos-cross-toolchains/aarch64-unknown-linux-gnu

RV32EMU_DIR    := rv32emu
RV32EMU_CONFIG := configs/rv32emu.config

AARCH64_CC     ?= aarch64-unknown-linux-gnu-gcc
QEMU_AARCH64   ?= qemu-aarch64
QEMU_LD_PREFIX ?= $(shell $(AARCH64_CC) -print-sysroot 2>/dev/null)

DOCKER_IMAGE   ?= fc32-spike-a
DOCKER_PLATFORM := linux/arm64

NATIVE_OUT  := $(RV32EMU_DIR)/build-native
AARCH64_OUT := $(RV32EMU_DIR)/build-aarch64

# ── helpers ───────────────────────────────────────────────────────────────────

$(RV32EMU_DIR)/.config: $(RV32EMU_CONFIG)
	cp $< $@

# ── native build (macOS host, for fast iteration) ─────────────────────────────

.PHONY: native
native: $(RV32EMU_DIR)/.config
	$(MAKE) -C $(RV32EMU_DIR) OUT=build-native

# ── Docker arm64 build (Pi-like environment, preferred on Apple Silicon) ──────

.PHONY: docker-build
docker-build:
	docker build --platform $(DOCKER_PLATFORM) -t $(DOCKER_IMAGE) .

.PHONY: docker-shell
docker-shell: docker-build
	docker run --rm -it --platform $(DOCKER_PLATFORM) $(DOCKER_IMAGE) /bin/bash

.PHONY: docker-bench
docker-bench: docker-build
	docker run --rm --platform $(DOCKER_PLATFORM) $(DOCKER_IMAGE) \
	    /spike-a/rv32emu/build/rv32emu /spike-a/coremark/coremark.elf

# ── Embench ───────────────────────────────────────────────────────────────────

EMBENCH_ELFS_GLOB := /spike-a/ports/rv32emu/embench/build/embench-*.elf
RV32EMU_BIN       := /spike-a/rv32emu/build/rv32emu

.PHONY: docker-embench
docker-embench: docker-build
	@echo "=== Embench-IoT benchmarks (RV32IMFC under rv32emu, arm64 Docker) ==="
	docker run --rm --platform $(DOCKER_PLATFORM) $(DOCKER_IMAGE) \
	    sh -c 'for f in $(EMBENCH_ELFS_GLOB); do $(RV32EMU_BIN) "$$f"; done'

# ── aarch64 cross-build (QEMU user-mode path) ────────────────────────────────

.PHONY: aarch64
aarch64: $(RV32EMU_DIR)/.config
	$(MAKE) -C $(RV32EMU_DIR) OUT=build-aarch64 CC=$(AARCH64_CC)

# ── clean ─────────────────────────────────────────────────────────────────────

.PHONY: clean
clean:
	$(MAKE) -C $(RV32EMU_DIR) OUT=build-native clean || true
	$(MAKE) -C $(RV32EMU_DIR) OUT=build-aarch64 clean || true
	$(RM) $(RV32EMU_DIR)/.config
	$(MAKE) -C coremark PORT_DIR=../ports/rv32emu clean || true
	$(MAKE) -C ports/rv32emu/embench clean || true
