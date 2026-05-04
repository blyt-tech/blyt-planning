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

# ── Cross-compile the RV64 launcher for the QEMU guest ───────────────────────
# The launcher runs as a native RV64 process inside the Ubuntu RISC-V guest.
# Cross-compiling it here avoids apt-get inside the QEMU guest (slow over SLIRP).
# The binary is shared via virtfs and executed directly.

.PHONY: docker-build-launcher
docker-build-launcher: docker-build
	docker run --rm --platform $(DOCKER_PLATFORM) \
	    -v $(CURDIR)/..:/spikes -w /spikes/spike-h \
	    $(DOCKER_IMAGE) /bin/bash -c '\
	        set -eu; \
	        mkdir -p build; \
	        echo "--- launcher (riscv64-linux-gnu, dynamic) ---"; \
	        riscv64-linux-gnu-gcc -O2 -o build/launcher \
	            launcher.c seccomp_filter.c \
	            -lseccomp ; \
	        echo "--- ELF header ---"; \
	        riscv64-linux-gnu-readelf -h build/launcher \
	            | grep -E "Class:|Machine:|Type:" ; \
	        file build/launcher'

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
QEMU_IMG    := $(QEMU_DIR)/ubuntu-24.04.4-preinstalled-server-riscv64.img
QEMU_TEST_IMG := $(QEMU_DIR)/ubuntu-test.qcow2
QEMU_FW     := /opt/homebrew/share/qemu/opensbi-riscv64-generic-fw_dynamic.bin
QEMU_UBOOT  := $(QEMU_DIR)/uboot-riscv64.elf
QEMU_URL    := https://cdimage.ubuntu.com/releases/24.04/release/ubuntu-24.04.4-preinstalled-server-riscv64.img.xz
# U-Boot for QEMU RISC-V — QEMU 11 dropped U-Boot from its firmware bundle;
# download from the Ubuntu 24.04 u-boot-qemu package instead.
UBOOT_DEB_URL := http://ports.ubuntu.com/pool/main/u/u-boot/u-boot-qemu_2025.10-0ubuntu0.24.04.2_all.deb

.PHONY: qemu-image
qemu-image: $(QEMU_IMG)

$(QEMU_IMG):
	mkdir -p $(QEMU_DIR)
	@echo "Downloading Ubuntu 24.04 RISC-V preinstalled-server image (~600 MB)..."
	curl -L -o $@.xz $(QEMU_URL)
	xz -dk $@.xz -c > $@
	@echo "Resizing to 8G to give room for builds..."
	qemu-img resize -f raw $@ 8G

# ── U-Boot firmware (QEMU 11 dropped it from its bundle) ─────────────────────
# Extracts uboot.elf for qemu-riscv64_smode from the Ubuntu u-boot-qemu .deb.
# Requires: ar, zstd (brew install zstd), tar.

.PHONY: qemu-uboot
qemu-uboot: $(QEMU_UBOOT)

$(QEMU_UBOOT):
	mkdir -p $(QEMU_DIR)/uboot-tmp
	curl -sL -o $(QEMU_DIR)/uboot-tmp/u-boot-qemu.deb $(UBOOT_DEB_URL)
	cd $(QEMU_DIR)/uboot-tmp && ar x u-boot-qemu.deb data.tar.zst
	zstd -qd $(QEMU_DIR)/uboot-tmp/data.tar.zst -o $(QEMU_DIR)/uboot-tmp/data.tar
	tar -xf $(QEMU_DIR)/uboot-tmp/data.tar \
	    -C $(QEMU_DIR)/uboot-tmp \
	    ./usr/lib/u-boot/qemu-riscv64_smode/uboot.elf
	cp $(QEMU_DIR)/uboot-tmp/usr/lib/u-boot/qemu-riscv64_smode/uboot.elf $@
	rm -rf $(QEMU_DIR)/uboot-tmp

.PHONY: qemu-run
qemu-run: $(QEMU_UBOOT)
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

# ── Automated QEMU test (SSH-based) ──────────────────────────────────────────
# Boots the Ubuntu RISC-V guest, waits for SSH (port 2222), then runs
# run-guest-tests.sh via SSH.  cloud-init sets ubuntu/ubuntu credentials.
# The spike-h directory is shared as virtfs at /mnt/spike-h in the guest.
# Results land in build/results/guest-run.log.
#
# Prerequisites:
#   make qemu-image    (one-time: download Ubuntu RISC-V image)
#   make qemu-uboot    (one-time: extract U-Boot ELF)
#   sshpass            (brew install sshpass, for automated password auth)
#
# Usage: make qemu-test

QEMU_SEED := $(QEMU_DIR)/seed.iso

.PHONY: qemu-seed
qemu-seed: $(QEMU_SEED)

$(QEMU_SEED): qemu/cloud-init/user-data qemu/cloud-init/meta-data
	mkdir -p $(QEMU_DIR)/seed-tmp
	cp qemu/cloud-init/user-data $(QEMU_DIR)/seed-tmp/
	cp qemu/cloud-init/meta-data $(QEMU_DIR)/seed-tmp/
	# hdiutil makehybrid always appends .iso; pass stem only.
	hdiutil makehybrid -quiet -o $(QEMU_DIR)/seed -hfs -joliet -iso \
	    -default-volume-name cidata $(QEMU_DIR)/seed-tmp/
	rm -rf $(QEMU_DIR)/seed-tmp

.PHONY: qemu-test
qemu-test: docker-build-carts docker-build-launcher $(QEMU_UBOOT)
	@if [ ! -f $(QEMU_IMG) ]; then \
	    echo "QEMU image not found — run: make qemu-image"; exit 1; \
	fi
	@if [ ! -f $(QEMU_FW) ]; then \
	    echo "QEMU firmware not found at $(QEMU_FW)"; \
	    echo "Run: brew install qemu"; exit 1; \
	fi
	@mkdir -p build/results
	@echo "Creating fresh image copy for test run..."
	@cp -c $(QEMU_IMG) $(QEMU_TEST_IMG).raw
	@echo "Injecting SSH credentials into CIDATA partition..."
	@OFFSET=$$(python3 -c "print(227328*512)"); \
	    mcopy -i $(QEMU_TEST_IMG).raw@@$$OFFSET qemu/cloud-init/user-data ::user-data 2>/dev/null || true; \
	    mcopy -i $(QEMU_TEST_IMG).raw@@$$OFFSET qemu/cloud-init/meta-data ::meta-data 2>/dev/null || true
	@echo "Booting QEMU guest (SSH on host:2222)..."
	qemu-system-riscv64 \
	    -machine virt -nographic -m 4G -smp 4 \
	    -bios $(QEMU_FW) \
	    -kernel $(QEMU_UBOOT) \
	    -drive file=$(QEMU_TEST_IMG).raw,format=raw,if=virtio \
	    -netdev user,id=net0,hostfwd=tcp::2222-:22 \
	    -device virtio-net-device,netdev=net0 \
	    -virtfs local,path=$(CURDIR),mount_tag=spike_h,security_model=mapped-xattr,id=spike_h \
	    -daemonize -pidfile $(QEMU_DIR)/qemu-test.pid \
	    2>&1 | tee $(QEMU_DIR)/console.log; sleep 1
	@echo "Waiting for SSH (up to 3 min)..."
	@for i in $$(seq 1 90); do nc -z localhost 2222 2>/dev/null && break; sleep 2; done
	@echo "Running tests via SSH..."
	sshpass -p ubuntu ssh \
	    -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
	    -p 2222 ubuntu@localhost \
	    'sudo mkdir -p /mnt/spike-h && \
	     sudo mount -t 9p -o trans=virtio,version=9p2000.L spike_h /mnt/spike-h 2>/dev/null; \
	     sudo bash /mnt/spike-h/run-guest-tests.sh' || true
	@echo "Stopping QEMU..."
	@kill $$(cat $(QEMU_DIR)/qemu-test.pid) 2>/dev/null || true
	@rm -f $(QEMU_TEST_IMG).raw $(QEMU_DIR)/qemu-test.pid
	@echo ""
	@echo "=== Test results ==="
	@if [ -f build/results/guest-run.log ]; then \
	    grep -E "(PASS|FAIL|SUMMARY|===|Spike H)" build/results/guest-run.log || true; \
	else \
	    echo "(build/results/guest-run.log not found — check $(QEMU_DIR)/console.log)"; \
	fi

# ── clean ────────────────────────────────────────────────────────────────────

.PHONY: clean
clean:
	$(RM) -r build
	docker image rm -f $(DOCKER_IMAGE) 2>/dev/null || true
