## Spike A — RV32IMFC interpreter throughput on Cortex-A53
##
## Targets:
##   make native        Build rv32emu for the local host (smoke-test, macOS)
##   make docker-build  Build rv32emu inside an arm64 Docker container (Pi-like env)
##   make docker-shell  Drop into the arm64 container with rv32emu built
##   make aarch64       Cross-compile rv32emu for aarch64-linux-gnu (for QEMU path)
##   make run-native    Run a quick sanity ELF under the native build
##   make run-qemu      Run a quick sanity ELF under qemu-aarch64
##   make clean         Remove all build artefacts
##
## Recommended path on Apple Silicon (arm64 Docker, no cross-compiler needed):
##   make docker-build    # validates the arm64 build
##   make docker-shell    # interactive: compile guest ELFs, run benchmarks
##
## QEMU path (if you prefer it):
##   brew install qemu
##   brew install messense/macos-cross-toolchains/aarch64-unknown-linux-gnu
##
## RISC-V guest toolchain (for CoreMark/Embench, installed inside Docker):
##   Provided by gcc-riscv64-linux-gnu in the Dockerfile.
##   For native macOS use, install xPack riscv-none-elf-gcc and add to PATH.

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

# ── aarch64 cross-build (QEMU user-mode path) ────────────────────────────────

.PHONY: aarch64
aarch64: $(RV32EMU_DIR)/.config
	$(MAKE) -C $(RV32EMU_DIR) OUT=build-aarch64 CC=$(AARCH64_CC)

# ── run targets ───────────────────────────────────────────────────────────────

# Placeholder: replace with a real RV32 ELF once the toolchain is set up.
SANITY_ELF ?= $(RV32EMU_DIR)/tests/hello.elf

.PHONY: run-native
run-native: native
	$(NATIVE_OUT)/rv32emu $(SANITY_ELF)

.PHONY: run-qemu
run-qemu: aarch64
	$(QEMU_AARCH64) -L $(QEMU_LD_PREFIX) $(AARCH64_OUT)/rv32emu $(SANITY_ELF)

# ── clean ─────────────────────────────────────────────────────────────────────

.PHONY: clean
clean:
	$(MAKE) -C $(RV32EMU_DIR) OUT=build-native clean || true
	$(MAKE) -C $(RV32EMU_DIR) OUT=build-aarch64 clean || true
	$(RM) $(RV32EMU_DIR)/.config
