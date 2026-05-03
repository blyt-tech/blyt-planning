## Spike H — RV32 carts on RV64 Linux: build harness.
##
## All build targets cross-compile inside a linux/arm64 Docker container that
## carries the riscv32-linux-musl prebuilt toolchain (cart binaries) and the
## host's native build chain (RV64 launcher, when running inside a QEMU guest).
##
## Targets:
##   make docker-build          Build the spike-h Docker image
##   make docker-shell          Drop into a shell in the spike-h container
##   make docker-stage1         Cross-compile and dump readelf for hello.elf
##   make docker-build-carts    Cross-compile hello, busy_loop, adversary, lua workloads
##   make qemu-image            Download the Ubuntu 24.04 RISC-V cloud image (~600 MB)
##   make qemu-run              Boot the QEMU guest (interactive; needs qemu-image)
##   make clean                 Remove host-side artefacts
##
## QEMU full-system run:
##   The harness inside the QEMU guest builds launcher.c against the guest's
##   native gcc + libseccomp-dev (it's an RV64 process).  The cart ELFs are
##   cross-compiled on the host and shared into the guest via the cloud-init
##   user-data or a 9p/virtfs mount (see README.md §Running stages).

DOCKER_IMAGE    ?= fc32-spike-h
DOCKER_PLATFORM := linux/arm64
WORKDIR         := /spike-h

# ── Docker image ─────────────────────────────────────────────────────────────

.PHONY: docker-build
docker-build:
	docker build --platform $(DOCKER_PLATFORM) -t $(DOCKER_IMAGE) .

.PHONY: docker-shell
docker-shell: docker-build
	docker run --rm -it --platform $(DOCKER_PLATFORM) \
	    -v $(CURDIR):$(WORKDIR) -w $(WORKDIR) \
	    $(DOCKER_IMAGE) /bin/bash

# ── Stage 1: hello.elf cross-compile + ELF header sanity check ───────────────

.PHONY: docker-stage1
docker-stage1: docker-build
	docker run --rm --platform $(DOCKER_PLATFORM) \
	    -v $(CURDIR):$(WORKDIR) -w $(WORKDIR) \
	    $(DOCKER_IMAGE) /bin/bash -c '\
	        set -eu; \
	        riscv32-linux-musl-gcc -nostdlib \
	            -march=rv32imfc -mabi=ilp32f \
	            -static -o build/hello hello.S; \
	        echo "=== file ==="; file build/hello; \
	        echo "=== readelf -h ==="; \
	        riscv32-linux-musl-readelf -h build/hello | grep -E "Class|Machine|Flags"'

# ── Cross-compile all cart-side artefacts ────────────────────────────────────
# Outputs go to ./build/ on the host (mounted into the container).

.PHONY: docker-build-carts
docker-build-carts: docker-build
	docker run --rm --platform $(DOCKER_PLATFORM) \
	    -v $(CURDIR)/..:/spikes -w /spikes/spike-h \
	    $(DOCKER_IMAGE) /bin/bash -c '\
	        set -eu; \
	        mkdir -p build; \
	        echo "--- hello (-march=rv32imfc -mabi=ilp32f, no libc) ---"; \
	        riscv32-linux-musl-gcc -nostdlib -march=rv32imfc \
	            -mabi=ilp32f -static -no-pie -o build/hello hello.S; \
	        echo "--- busy_loop (default -march=rv32imac -mabi=ilp32) ---"; \
	        riscv32-linux-musl-gcc -O2 -static -no-pie -o build/busy_loop busy_loop.c; \
	        echo "--- adversary (default -march=rv32imac -mabi=ilp32) ---"; \
	        riscv32-linux-musl-gcc -O2 -static -no-pie -o build/adversary adversary.c; \
	        echo "--- Lua workloads ---"; \
	        make -C lua-workloads OUTDIR=/spikes/spike-h/build/lua \
	            LUA_DIR=/opt/lua BENCH=/spikes/spike-b/benchmarks; \
	        echo "--- ELF header summary ---"; \
	        for f in build/hello build/busy_loop build/adversary build/lua/lua_cart_*.elf; do \
	            echo "== $$f =="; \
	            riscv32-linux-musl-readelf -h "$$f" | grep -E "Class|Machine|Flags"; \
	        done'

# ── Smoke tests via qemu-user (correctness only, not isolation) ──────────────
# qemu-user does NOT exercise CONFIG_COMPAT, seccomp-bpf, namespaces, or
# cgroups — the host kernel handles syscalls directly.  These targets only
# verify the cart binaries produce the expected output before running them
# under the full-system QEMU guest.

.PHONY: docker-smoke-stage1
docker-smoke-stage1: docker-build-carts
	docker run --rm --platform $(DOCKER_PLATFORM) \
	    -v $(CURDIR):$(WORKDIR) -w $(WORKDIR) \
	    $(DOCKER_IMAGE) /bin/bash -c '\
	        echo "=== hello.elf under qemu-user ==="; \
	        out=$$(qemu-riscv32-static build/hello); rc=$$?; \
	        echo "stdout=[$$out] rc=$$rc"; \
	        test "$$out" = "OK" -a $$rc -eq 0 && echo "Stage 1 (qemu-user): PASS" \
	            || (echo "Stage 1 (qemu-user): FAIL"; exit 1)'

.PHONY: docker-smoke-busy
docker-smoke-busy: docker-build-carts
	docker run --rm --platform $(DOCKER_PLATFORM) \
	    -v $(CURDIR):$(WORKDIR) -w $(WORKDIR) \
	    $(DOCKER_IMAGE) /bin/bash -c '\
	        echo "=== busy_loop.elf under qemu-user (no cgroup) ==="; \
	        qemu-riscv32-static build/busy_loop'

.PHONY: docker-smoke-lua
docker-smoke-lua: docker-build-carts
	docker run --rm --platform $(DOCKER_PLATFORM) \
	    -v $(CURDIR):$(WORKDIR) -w $(WORKDIR) \
	    $(DOCKER_IMAGE) /bin/bash -c '\
	        echo "=== lua_cart_doom_tick under qemu-user ==="; \
	        qemu-riscv32-static build/lua/lua_cart_doom_tick.elf | tail -5; \
	        echo "=== lua_cart_entity_update under qemu-user ==="; \
	        qemu-riscv32-static build/lua/lua_cart_entity_update.elf | tail -5'

# ── QEMU full-system image (Ubuntu 24.04 RISC-V) ─────────────────────────────
# The image is downloaded on demand into ./qemu/.  ~600 MB compressed.

QEMU_DIR    := qemu
QEMU_IMG    := $(QEMU_DIR)/ubuntu-24.04.2-preinstalled-server-riscv64.img
QEMU_FW     := /opt/homebrew/share/qemu/opensbi-riscv64-generic-fw_dynamic.bin
QEMU_UBOOT  := /opt/homebrew/share/qemu/uboot-riscv64-generic.bin
QEMU_URL    := https://cdimage.ubuntu.com/releases/24.04/release/ubuntu-24.04.2-preinstalled-server-riscv64.img.xz

.PHONY: qemu-image
qemu-image: $(QEMU_IMG)

$(QEMU_IMG):
	mkdir -p $(QEMU_DIR)
	@echo "Downloading Ubuntu 24.04 RISC-V preinstalled-server image (~600 MB)..."
	curl -L -o $@.xz $(QEMU_URL)
	xz -d $@.xz
	@echo "Resizing to 8G to give room for builds..."
	qemu-img resize $@ 8G

.PHONY: qemu-run
qemu-run:
	@if [ ! -f $(QEMU_IMG) ]; then \
	    echo "QEMU image not found — run: make qemu-image"; exit 1; \
	fi
	qemu-system-riscv64 \
	    -machine virt -nographic -m 4G -smp 4 \
	    -bios $(QEMU_FW) \
	    -kernel $(QEMU_UBOOT) \
	    -drive file=$(QEMU_IMG),format=raw,if=virtio \
	    -netdev user,id=net0,hostfwd=tcp::2222-:22 \
	    -device virtio-net-device,netdev=net0 \
	    -virtfs local,path=$(CURDIR)/build,mount_tag=spike_h,security_model=passthrough,id=spike_h

# ── clean ────────────────────────────────────────────────────────────────────

.PHONY: clean
clean:
	$(RM) -r build
	docker image rm -f $(DOCKER_IMAGE) 2>/dev/null || true
