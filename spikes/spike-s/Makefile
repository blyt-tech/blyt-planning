## Spike S — hardware trusted native-exec seccomp validation.
##
## Validates that an LP64 launcher can exec an ILP32 cart binary natively,
## arch-dispatch seccomp filters work across the exec boundary, and
## libblyt32.so's constructor can install a restrictive phase-2 filter.
##
## PREREQUISITE: A RISC-V kernel with the ILP32 ELF binary loader ("u32ilp32"
## compat mode).  CONFIG_COMPAT merged in Linux 5.19 provides only compat
## syscall wrappers — it does NOT include the ILP32 ELF binfmt handler.
## The stock Fedora 42 kernel (6.16.4) therefore returns ENOEXEC on ILP32 exec.
##
## IMPORTANT — use the correct patchset:
##   The RFC V2/V3 rv64ilp32 patchset makes the KERNEL run in ILP32 mode
##   (CONFIG_ARCH_RV64ILP32 selects 32BIT).  That is NOT what Spike S needs.
##
##   What we need is the "64ilp64+u32ilp32" model: 64-bit LP64 kernel that can
##   exec ILP32 user-space ELF binaries (analogous to arm64 running 32-bit ARM).
##   This is implemented in the c-sky csky-linux tree:
##     https://github.com/c-sky/csky-linux.git
##     branch: sg2042-master-qspinlock-64ilp32_v4  (Linux 6.5-rc1 base)
##
##   Key additions over mainline:
##     fs/compat_binfmt_elf.c — ILP32 ELF binary loader (generic, auto-enabled
##       when CONFIG_COMPAT=y, which is the default for 64-bit builds)
##     arch/riscv/include/asm/elf.h — compat_elf_check_arch(), SET_PERSONALITY
##       (sets TIF_32BIT for ELFCLASS32 → seccomp_data.arch = AUDIT_ARCH_RISCV32)
##
##   Use `make kernel-build` to fetch, configure for QEMU, and build it.
##   The build runs inside the Docker container (riscv64-linux-gnu-gcc already
##   present) and drops the Image at $(ILP32_KERNEL).
##
## Once ILP32_KERNEL is set, boot the Fedora 42 rootfs under the custom kernel:
##   make qemu-stage0-s    # Confirms ILP32 exec + arch=RISCV32
##   make qemu-stage1-s    # Arch-dispatch filter across exec boundary
##   make qemu-stage2-s    # ld.so syscall characterisation (strace)
##   make qemu-stage3-s    # Cart syscall strace + allowlist derivation
##   make qemu-stage4-s    # End-to-end: launcher + phase-1 + libblyt32 phase-2

SPIKE_H_DIR := $(CURDIR)/../spike-h
DOCKER_IMAGE := fc32-spike-h       ## reuse spike-h's image (carries riscv32/64 toolchains)
DOCKER_PLATFORM := linux/arm64

QEMU_DIR    := $(SPIKE_H_DIR)/qemu
FEDORA_IMG  := $(QEMU_DIR)/Fedora-Cloud-Base-Generic-42.riscv64.qcow2
QEMU_FW     := /opt/homebrew/share/qemu/opensbi-riscv64-generic-fw_dynamic.bin
FEDORA_KEY  := $(QEMU_DIR)/fedora-s-test-key

## ILP32-capable kernel image.  Built by `make kernel-build`; or supply
## your own.  The ruyisdk K230 kernel tree provides the u32ilp32 ELF loader.
ILP32_KERNEL ?= $(QEMU_DIR)/riscv64-ilp32-Image

## Kernel source cache directory (avoids re-cloning on repeat builds).
KERNEL_CACHE ?= $(QEMU_DIR)/kernel-cache

## c-sky kernel tree — Linux 6.5-rc1 + ILP32 user-space compat ELF loader.
## Provides:
##   fs/compat_binfmt_elf.c + arch/riscv/include/asm/elf.h: compat_elf_check_arch()
##   SET_PERSONALITY: sets TIF_32BIT for ELFCLASS32 → seccomp_data.arch=RISCV32
##   CONFIG_COMPAT default=y on 64BIT → auto-enabled in 64-bit kernel builds
K230_REPO    := https://github.com/c-sky/csky-linux.git
K230_BRANCH  := sg2042-master-qspinlock-64ilp32_v4

## Root device on the Fedora Cloud image (partition 3 for typical layouts).
## Adjust if your image differs; inspect with: sudo fdisk -l fedora.qcow2
FEDORA_ROOT ?= /dev/vda3

BUILD := $(CURDIR)/build

# ── Kernel build (ILP32-capable, QEMU-targeted) ───────────────────────────────
# Builds the ruyisdk K230 kernel (Linux 6.6 + u32ilp32 ELF loader) configured
# for the QEMU virt machine.  Runs entirely inside Docker (riscv64-linux-gnu-gcc
# already present in the spike-h image).
#
# Strategy: start from defconfig (baseline that boots systemd/SSH correctly),
# apply the K230 tree's own 64ilp32.config fragment (enables ILP32 user-space
# compat), then apply our kernel-qemu-ilp32.config fragment (strips K230 SoC
# drivers, disables modules, enables VirtIO/9p).
#
# Build time vs defconfig:
#   defconfig alone:               ~25 min  (modules + all drivers)
#   + CONFIG_MODULES=n:            ~12 min  (no module build/link)
#   + strip K230 platform drivers: ~ 7 min  (estimated on M-series)
#
# The kernel cache at $(KERNEL_CACHE) is reused across builds; delete it to
# force a fresh clone.

.PHONY: kernel-build
kernel-build:
	mkdir -p $(KERNEL_CACHE) $(QEMU_DIR)
	@echo "=== Spike S: building ILP32-capable RISC-V kernel ==="
	@echo "    Source:  $(K230_REPO)"
	@echo "    Branch:  $(K230_BRANCH)"
	@echo "    Output:  $(ILP32_KERNEL)"
	@echo ""
	@docker rm -f spike-s-kernel-build 2>/dev/null || true
	docker run --platform linux/arm64 --name spike-s-kernel-build \
	    -v $(KERNEL_CACHE):/kernel-cache \
	    -v $(CURDIR):/spike-s:ro \
	    -v $(QEMU_DIR):/out \
	    $(DOCKER_IMAGE) /bin/bash -c '\
	        set -eu; \
	        REPO="$(K230_REPO)"; \
	        BRANCH="$(K230_BRANCH)"; \
	        SRC=/kernel-cache/k230-linux; \
	        \
	        if [ ! -d "$$SRC/.git" ]; then \
	            echo "--- Cloning $$BRANCH (shallow) ---"; \
	            git clone --depth=1 -b "$$BRANCH" "$$REPO" "$$SRC"; \
	        else \
	            echo "--- Reusing cached clone at $$SRC ---"; \
	            git -C "$$SRC" fetch --depth=1 origin "$$BRANCH"; \
	            git -C "$$SRC" reset --hard FETCH_HEAD; \
	        fi; \
	        \
	        echo "--- Kernel version ---"; \
	        head -5 "$$SRC/Makefile"; \
	        \
	        echo "--- Installing kernel build tools ---"; \
	        apt-get update -qq && \
	        DEBIAN_FRONTEND=noninteractive apt-get install -y -q --no-install-recommends \
	            flex bison libelf-dev libssl-dev bc pahole 2>&1 | tail -5; \
	        \
	        cd "$$SRC"; \
	        \
	        echo "--- Applying Spike S patch: syscall_get_arch TIF_32BIT fix ---"; \
	        git checkout -- arch/riscv/include/asm/syscall.h \
	                        arch/riscv/include/asm/elf.h 2>/dev/null || true; \
	        python3 /spike-s/apply-syscall-arch-patch.py \
	            arch/riscv/include/asm/syscall.h; \
	        touch arch/riscv/include/asm/syscall.h arch/riscv/include/asm/elf.h; \
	        find . -name "seccomp.o" -o -name "process.o" | xargs rm -f 2>/dev/null || true; \
	        echo "--- Clean previous build artifacts ---"; \
	        find "$$SRC" -name ".*.cmd" -delete 2>/dev/null; \
	        find "$$SRC" -name "*.o" -not -path "*/.git/*" -delete 2>/dev/null; \
	        find "$$SRC" -name "*.a" -not -path "*/.git/*" -delete 2>/dev/null; \
	        rm -f "$$SRC"/vmlinux "$$SRC"/System.map "$$SRC"/.tmp_vmlinux* 2>/dev/null; \
	        echo "--- Configuring: defconfig ---"; \
	        make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- defconfig; \
	        \
	        echo "--- K230 defconfig fragments available ---"; \
	        ls arch/riscv/configs/*.config 2>/dev/null || true; \
	        echo "--- COMPAT / ILP32 options in defconfig ---"; \
	        grep -E "COMPAT|ILP32|RISCV32|U32ILP32" .config | sort || true; \
	        echo "--- (NOT applying 64ilp32.config: that sets kernel ILP32 ABI, wrong) ---"; \
	        echo "--- (LP64 kernel + u32ilp32 user-space compat is in defconfig already) ---"; \
	        \
	        echo "--- Applying QEMU optimisation fragment (no modules, VirtIO, 9p) ---"; \
	        ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- \
	            ./scripts/kconfig/merge_config.sh -m \
	            .config /spike-s/kernel-qemu-ilp32.config; \
	        make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- olddefconfig; \
	        \
	        echo "--- Config summary (ILP32 / COMPAT / VIRTIO / MODULES) ---"; \
	        grep -E "^CONFIG_(COMPAT|VIRTIO|NET_9P|9P_FS|MODULES|RISCV_COMPAT|64ILP32|SECCOMP)" \
	            .config | sort; \
	        \
	        echo "--- Building Image (ARCH=riscv, -j$$(nproc)) ---"; \
	        make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- -j$$(nproc) Image; \
	        \
	        echo "--- Copying Image to /out/ ---"; \
	        cp arch/riscv/boot/Image /out/riscv64-ilp32-Image; \
	        ls -lh /out/riscv64-ilp32-Image; \
	        echo "Kernel commit: $$(git rev-parse --short HEAD)"'
	@docker rm spike-s-kernel-build
	@echo ""
	@echo "=== Kernel built: $(ILP32_KERNEL) ==="

# ── Build RV64 LP64 binaries ──────────────────────────────────────────────────
# Cross-compiles seccomp_raw_test_s (Stage 1 test) and launcher_s (Stage 4)
# as static/dynamic RV64 binaries using the spike-h Docker image.

.PHONY: docker-build-s
docker-build-s:
	mkdir -p $(BUILD)
	@docker rm -f spike-s-build-rv64 2>/dev/null || true
	docker run --platform $(DOCKER_PLATFORM) --name spike-s-build-rv64 \
	    -v $(CURDIR)/..:/spikes:ro -w /spikes/spike-s \
	    $(DOCKER_IMAGE) /bin/bash -c '\
	        set -eu; \
	        echo "--- seccomp_raw_test_s (riscv64, static) ---"; \
	        riscv64-linux-gnu-gcc -O2 -static \
	            -o /tmp/seccomp_raw_test_s seccomp_raw_test_s.c; \
	        echo "--- launcher_s (riscv64, dynamic) ---"; \
	        riscv64-linux-gnu-gcc -O2 \
	            -o /tmp/launcher_s launcher_s.c; \
	        echo "--- ELF headers ---"; \
	        for f in /tmp/seccomp_raw_test_s /tmp/launcher_s; do \
	            echo "== $$f =="; \
	            riscv64-linux-gnu-readelf -h "$$f" \
	                | grep -E "Class:|Machine:|Type:"; \
	        done'
	docker cp spike-s-build-rv64:/tmp/seccomp_raw_test_s $(BUILD)/seccomp_raw_test_s
	docker cp spike-s-build-rv64:/tmp/launcher_s         $(BUILD)/launcher_s
	@docker rm spike-s-build-rv64
	chmod +x $(BUILD)/seccomp_raw_test_s $(BUILD)/launcher_s

# ── Build ILP32 binaries ──────────────────────────────────────────────────────
# Cross-compiles the ILP32 adversary (static) and libblyt32_stub.so + dynamic
# test cart using the riscv32-linux-musl toolchain in the spike-h Docker image.

.PHONY: docker-build-ilp32-s
docker-build-ilp32-s:
	mkdir -p $(BUILD)
	@docker rm -f spike-s-build-ilp32 2>/dev/null || true
	docker run --platform $(DOCKER_PLATFORM) --name spike-s-build-ilp32 \
	    -v $(CURDIR)/..:/spikes:ro -w /spikes/spike-s \
	    $(DOCKER_IMAGE) /bin/bash -c '\
	        set -eu; \
	        RV32=riscv32-linux-musl-gcc; \
	        echo "--- adversary_s_static (riscv32, static) ---"; \
	        $$RV32 -O2 -static \
	            -march=rv32gc -mabi=ilp32d \
	            -o /tmp/adversary_s_static adversary_s.c; \
	        echo "--- libblyt32_stub.so (riscv32, shared) ---"; \
	        $$RV32 -O2 -fPIC -shared \
	            -march=rv32gc -mabi=ilp32d \
	            -Wl,-soname,libblyt32.so \
	            -o /tmp/libblyt32_stub.so libblyt32_stub_s.c; \
	        echo "--- adversary_s_dynamic (riscv32, dynamic, DT_NEEDED libblyt32.so) ---"; \
	        ln -sf libblyt32_stub.so /tmp/libblyt32.so; \
	        $$RV32 -O2 \
	            -march=rv32gc -mabi=ilp32d \
	            -Wl,-rpath-link,/tmp \
	            -o /tmp/adversary_s_dynamic adversary_s.c \
	            -L/tmp -lblyt32; \
	        echo "--- ELF headers ---"; \
	        for f in /tmp/adversary_s_static /tmp/libblyt32_stub.so /tmp/adversary_s_dynamic; do \
	            echo "== $$f =="; \
	            riscv32-linux-musl-readelf -h "$$f" \
	                | grep -E "Class:|Machine:|Type:|Flags:"; \
	        done; \
	        echo "--- PT_INTERP of adversary_s_dynamic ---"; \
	        riscv32-linux-musl-readelf -l /tmp/adversary_s_dynamic \
	            | grep -A1 "INTERP" || echo "(no PT_INTERP — may be static)"; \
	        echo "--- DT_NEEDED of adversary_s_dynamic ---"; \
	        riscv32-linux-musl-readelf -d /tmp/adversary_s_dynamic \
	            | grep NEEDED'
	docker cp spike-s-build-ilp32:/tmp/adversary_s_static  $(BUILD)/adversary_s_static
	docker cp spike-s-build-ilp32:/tmp/libblyt32_stub.so    $(BUILD)/libblyt32_stub.so
	docker cp spike-s-build-ilp32:/tmp/adversary_s_dynamic $(BUILD)/adversary_s_dynamic
	@docker rm spike-s-build-ilp32
	chmod +x $(BUILD)/adversary_s_static $(BUILD)/adversary_s_dynamic

# ── Build all ─────────────────────────────────────────────────────────────────

.PHONY: docker-build-all-s
docker-build-all-s: docker-build-s docker-build-ilp32-s
	@echo "=== Spike S binaries built ==="
	@ls -lh $(BUILD)/*.{s,so,_static,_dynamic} 2>/dev/null || ls -lh $(BUILD)/

# ── Extract ILP32 dynamic linker from toolchain sysroot ──────────────────────
# The Fedora 42 rootfs does not include an ILP32 ld.so.  We extract it from
# the musl cross-toolchain sysroot in the Docker image and record the path.
# The in-guest run-guest-stage2.sh places it at the correct /lib path.

.PHONY: docker-extract-ld-musl
docker-extract-ld-musl:
	mkdir -p $(BUILD)
	@docker rm -f spike-s-extract-ld 2>/dev/null || true
	docker run --platform $(DOCKER_PLATFORM) --name spike-s-extract-ld \
	    -v $(BUILD):/out \
	    $(DOCKER_IMAGE) /bin/bash -c '\
	        set -eu; \
	        SYSROOT=/opt/riscv32/riscv32-linux-musl; \
	        FOUND=""; \
	        for candidate in \
	            $${SYSROOT}/lib/libc.so \
	            $${SYSROOT}/lib/ld-musl-riscv32.so.1 \
	            $${SYSROOT}/lib/ld-musl-riscv32sf.so.1; do \
	            if [ -f "$${candidate}" ]; then \
	                FOUND="$${candidate}"; break; \
	            fi; \
	        done; \
	        if [ -z "$${FOUND}" ]; then \
	            echo "ERROR: musl libc not found. Sysroot lib contents:"; \
	            ls /opt/riscv32/riscv32-linux-musl/lib/*.so* 2>/dev/null | head -10; \
	            exit 1; \
	        fi; \
	        echo "Found musl libc: $${FOUND}"; \
	        cp "$${FOUND}" /out/ld-musl-riscv32.so.1; \
	        riscv32-linux-musl-readelf -h /out/ld-musl-riscv32.so.1 \
	            | grep -E "Class:|Machine:|Flags:"; \
	        ls -lh /out/ld-musl-riscv32.so.1'
	@docker rm spike-s-extract-ld

# ── QEMU boot helper ──────────────────────────────────────────────────────────
# Common QEMU boot used by all stage targets.  Boots the Fedora 42 Cloud image
# under the ILP32-capable custom kernel.  SSH on host port 2222.

define QEMU_BOOT
	@if [ ! -f $(FEDORA_IMG) ]; then \
	    echo "Fedora image not found — run: make -C ../spike-h fedora-image"; exit 1; \
	fi
	@if [ ! -f $(QEMU_FW) ]; then \
	    echo "QEMU firmware not found at $(QEMU_FW)"; exit 1; \
	fi
	@if [ ! -f $(ILP32_KERNEL) ]; then \
	    echo "ILP32-capable kernel not found at $(ILP32_KERNEL)"; \
	    echo "See PREREQUISITE in this Makefile for build instructions."; exit 1; \
	fi
	@mkdir -p $(BUILD)/results
	@echo "Generating ephemeral SSH key..."
	@rm -f $(FEDORA_KEY) $(FEDORA_KEY).pub
	ssh-keygen -t ed25519 -f $(FEDORA_KEY) -N "" -q -C spike-s-test
	@{ echo '#cloud-config'; \
	   echo 'users:'; \
	   echo '  - name: fedora'; \
	   echo '    sudo: ALL=(ALL) NOPASSWD:ALL'; \
	   echo '    ssh_authorized_keys:'; \
	   printf '      - %s\n' "$$(cat $(FEDORA_KEY).pub)"; \
	} > $(QEMU_DIR)/fedora-s-userdata
	dd if=/dev/zero of=$(QEMU_DIR)/fedora-s-seed.img bs=1k count=1024 2>/dev/null
	mformat -i $(QEMU_DIR)/fedora-s-seed.img -v CIDATA -F ::
	mcopy -i $(QEMU_DIR)/fedora-s-seed.img $(QEMU_DIR)/fedora-s-userdata ::user-data
	mcopy -i $(QEMU_DIR)/fedora-s-seed.img $(SPIKE_H_DIR)/qemu/cloud-init/meta-data ::meta-data
	@echo "Creating fresh qcow2 overlay..."
	qemu-img create -f qcow2 -b $(abspath $(FEDORA_IMG)) -F qcow2 \
	    $(QEMU_DIR)/fedora-s-test.qcow2 2>/dev/null
	@echo "Booting Fedora RISC-V with ILP32 kernel (SSH on host:2222)..."
	qemu-system-riscv64 \
	    -machine virt -display none -m 4G -smp 4 \
	    -bios $(QEMU_FW) \
	    -kernel $(ILP32_KERNEL) \
	    -append "root=$(FEDORA_ROOT) rw rootflags=subvol=root console=ttyS0 earlycon rootwait" \
	    -serial file:$(QEMU_DIR)/fedora-s-console.log \
	    -drive id=root,file=$(QEMU_DIR)/fedora-s-test.qcow2,format=qcow2,if=none \
	    -device virtio-blk-device,drive=root \
	    -drive id=seed,file=$(QEMU_DIR)/fedora-s-seed.img,format=raw,if=none,readonly=on \
	    -device virtio-blk-device,drive=seed \
	    -netdev user,id=net0,hostfwd=tcp::2222-:22 \
	    -device virtio-net-device,netdev=net0 \
	    -virtfs local,path=$(CURDIR)/..,mount_tag=spikes,security_model=mapped-xattr,id=spikes \
	    -daemonize -pidfile $(QEMU_DIR)/fedora-s-test.pid 2>/dev/null
	@echo "Waiting for SSH daemon (up to 5 min)..."
	@for i in $$(seq 1 150); do \
	    ssh -i $(FEDORA_KEY) \
	        -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
	        -o ConnectTimeout=3 -o BatchMode=yes -o IdentitiesOnly=yes \
	        -p 2222 fedora@localhost true 2>/dev/null && break; \
	    sleep 2; \
	done
endef

define QEMU_STOP
	@echo "Stopping QEMU..."
	@kill $$(cat $(QEMU_DIR)/fedora-s-test.pid) 2>/dev/null || true
	@rm -f $(QEMU_DIR)/fedora-s-test.qcow2 $(QEMU_DIR)/fedora-s-test.pid \
	       $(QEMU_DIR)/fedora-s-seed.img   $(QEMU_DIR)/fedora-s-userdata \
	       $(FEDORA_KEY) $(FEDORA_KEY).pub
endef

define SSH_CMD
	ssh -i $(FEDORA_KEY) \
	    -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
	    -o IdentitiesOnly=yes \
	    -o ServerAliveInterval=60 -o ServerAliveCountMax=20 \
	    -p 2222 fedora@localhost
endef

define SCP_BINARIES
	scp -i $(FEDORA_KEY) \
	    -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
	    -o IdentitiesOnly=yes -P 2222
endef

# ── QEMU Stage 0: ILP32 exec + AUDIT_ARCH_RISCV32 confirmation ───────────────

# Note: 9p virtfs is unavailable with the c-sky 6.5-rc1 kernel (9pnet_virtio
# reports "no channels available").  Scripts are SCP'd to /tmp/ in the guest;
# logs are written to /tmp/spike-s-results/ and SCP'd back to $(BUILD)/results/.

.PHONY: qemu-stage0-s
qemu-stage0-s: docker-build-s docker-build-ilp32-s
	$(QEMU_BOOT)
	@mkdir -p $(BUILD)/results
	@echo "Copying binaries + script to guest..."
	$(SCP_BINARIES) \
	    $(BUILD)/seccomp_raw_test_s \
	    $(BUILD)/adversary_s_static \
	    $(CURDIR)/run-guest-stage0.sh \
	    fedora@localhost:/tmp/
	$(SSH_CMD) \
	    'chmod +x /tmp/run-guest-stage0.sh && \
	     sudo bash /tmp/run-guest-stage0.sh \
	         /tmp/seccomp_raw_test_s /tmp/adversary_s_static' || true
	$(SCP_BINARIES) \
	    fedora@localhost:/tmp/spike-s-results/stage0.log \
	    $(BUILD)/results/stage0.log 2>/dev/null || true
	$(QEMU_STOP)
	@echo "=== Stage 0 results ==="
	@grep -E "(PASS|FAIL|===|Stage)" $(BUILD)/results/stage0.log 2>/dev/null || \
	    echo "(log not found — check $(QEMU_DIR)/fedora-s-console.log)"

# ── QEMU Stage 1: arch-dispatch filter across exec boundary ──────────────────

.PHONY: qemu-stage1-s
qemu-stage1-s: docker-build-s docker-build-ilp32-s
	$(QEMU_BOOT)
	@mkdir -p $(BUILD)/results
	$(SCP_BINARIES) \
	    $(BUILD)/seccomp_raw_test_s \
	    $(BUILD)/adversary_s_static \
	    $(CURDIR)/run-guest-stage1.sh \
	    fedora@localhost:/tmp/
	$(SSH_CMD) \
	    'chmod +x /tmp/run-guest-stage1.sh && \
	     sudo bash /tmp/run-guest-stage1.sh \
	         /tmp/seccomp_raw_test_s /tmp/adversary_s_static' || true
	$(SCP_BINARIES) \
	    fedora@localhost:/tmp/spike-s-results/stage1.log \
	    $(BUILD)/results/stage1.log 2>/dev/null || true
	$(QEMU_STOP)
	@echo "=== Stage 1 results ==="
	@grep -E "(PASS|FAIL|===|Stage)" $(BUILD)/results/stage1.log 2>/dev/null || \
	    echo "(log not found)"

# ── QEMU Stage 2: ld.so startup syscall characterisation ─────────────────────

.PHONY: qemu-stage2-s
qemu-stage2-s: docker-build-s docker-build-ilp32-s docker-extract-ld-musl
	$(QEMU_BOOT)
	@mkdir -p $(BUILD)/results
	$(SCP_BINARIES) \
	    $(BUILD)/launcher_s \
	    $(BUILD)/adversary_s_dynamic \
	    $(BUILD)/libblyt32_stub.so \
	    $(BUILD)/ld-musl-riscv32.so.1 \
	    $(CURDIR)/run-guest-stage2.sh \
	    fedora@localhost:/tmp/
	$(SSH_CMD) \
	    'chmod +x /tmp/run-guest-stage2.sh && \
	     sudo bash /tmp/run-guest-stage2.sh' || true
	$(SCP_BINARIES) -r \
	    fedora@localhost:/tmp/spike-s-results/ \
	    $(BUILD)/results/ 2>/dev/null || true
	$(QEMU_STOP)
	@echo "=== Stage 2 results ==="
	@grep -E "(PASS|FAIL|===|Stage)" $(BUILD)/results/stage2.log 2>/dev/null || \
	    echo "(log not found)"
	@[ -f $(BUILD)/results/phase1_ld_syscalls.txt ] && \
	    echo "ld.so syscalls:" && cat $(BUILD)/results/phase1_ld_syscalls.txt || true

# ── QEMU Stage 3: cart syscall strace + allowlist derivation ─────────────────

.PHONY: qemu-stage3-s
qemu-stage3-s: docker-build-ilp32-s docker-extract-ld-musl
	$(QEMU_BOOT)
	@mkdir -p $(BUILD)/results
	$(SCP_BINARIES) \
	    $(BUILD)/adversary_s_dynamic \
	    $(BUILD)/libblyt32_stub.so \
	    $(BUILD)/ld-musl-riscv32.so.1 \
	    $(CURDIR)/run-guest-stage3.sh \
	    fedora@localhost:/tmp/
	$(SSH_CMD) \
	    -o ServerAliveCountMax=30 \
	    'chmod +x /tmp/run-guest-stage3.sh && \
	     sudo bash /tmp/run-guest-stage3.sh' || true
	$(SCP_BINARIES) -r \
	    fedora@localhost:/tmp/spike-s-results/ \
	    $(BUILD)/results/ 2>/dev/null || true
	$(QEMU_STOP)
	@echo "=== Stage 3 results ==="
	@grep -E "(PASS|FAIL|===|Stage)" $(BUILD)/results/stage3.log 2>/dev/null || \
	    echo "(log not found)"
	@[ -f $(BUILD)/results/stage3-strace/syscall_names.txt ] && \
	    echo "Observed cart syscalls:" && \
	    cat $(BUILD)/results/stage3-strace/syscall_names.txt || true

# ── QEMU Stage 4: end-to-end Option B ────────────────────────────────────────

.PHONY: qemu-stage4-s
qemu-stage4-s: docker-build-s docker-build-ilp32-s docker-extract-ld-musl
	$(QEMU_BOOT)
	@mkdir -p $(BUILD)/results
	$(SCP_BINARIES) \
	    $(BUILD)/launcher_s \
	    $(BUILD)/adversary_s_dynamic \
	    $(BUILD)/adversary_s_static \
	    $(BUILD)/libblyt32_stub.so \
	    $(BUILD)/ld-musl-riscv32.so.1 \
	    fedora@localhost:/tmp/
	$(SCP_BINARIES) $(CURDIR)/run-guest-stage4.sh fedora@localhost:/tmp/
	$(SSH_CMD) \
	    'chmod +x /tmp/run-guest-stage4.sh && \
	     sudo bash /tmp/run-guest-stage4.sh \
	         /tmp/launcher_s /tmp/adversary_s_dynamic /tmp/adversary_s_static' || true
	$(SCP_BINARIES) \
	    fedora@localhost:/tmp/spike-s-results/stage4.log \
	    $(BUILD)/results/stage4.log 2>/dev/null || true
	$(QEMU_STOP)
	@echo "=== Stage 4 results ==="
	@grep -E "(PASS|FAIL|===|Stage)" $(BUILD)/results/stage4.log 2>/dev/null || \
	    echo "(log not found)"

# ── clean ─────────────────────────────────────────────────────────────────────

.PHONY: clean
clean:
	$(RM) -r build
	$(RM) -f $(QEMU_DIR)/fedora-s-*.{img,qcow2,log,pid} \
	         $(QEMU_DIR)/fedora-s-userdata \
	         $(QEMU_DIR)/fedora-s-test-key* 2>/dev/null || true
