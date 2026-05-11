## Spike R — raw BPF seccomp arch-dispatch filter build harness.
##
## Cross-compiles launcher_r and seccomp_raw_test as RV64 binaries inside the
## Spike H Docker image (which already has riscv64-linux-gnu-gcc).  The adversary
## RV32 binary is reused from spike-h/build/adversary.
##
## Targets:
##   make docker-build-r        Cross-compile spike-r RV64 binaries
##   make docker-build-adversary  Rebuild RV32 adversary via spike-h (if missing)
##   make qemu-test-fedora-r    Full Stage 0-2 run in Fedora 42 QEMU guest
##   make qemu-stage3-strace    Stage 3: run derive_allowlist.sh in guest
##   make clean                 Remove build artefacts
##
## Prerequisites (one-time):
##   cd ../spike-h && make fedora-image   (download Fedora 42 qcow2)
##   cd ../spike-h && make qemu-uboot     (extract U-Boot ELF)
##   brew install sshpass mtools          (for SSH + FAT seed image)

SPIKE_H_DIR := $(CURDIR)/../spike-h
DOCKER_IMAGE := fc32-spike-h       ## reuse spike-h's image; no new Dockerfile needed
DOCKER_PLATFORM := linux/arm64

QEMU_DIR    := $(SPIKE_H_DIR)/qemu
FEDORA_IMG  := $(QEMU_DIR)/Fedora-Cloud-Base-Generic-42.riscv64.qcow2
QEMU_UBOOT  := $(QEMU_DIR)/uboot-riscv64.elf
QEMU_FW     := /opt/homebrew/share/qemu/opensbi-riscv64-generic-fw_dynamic.bin
FEDORA_KEY  := $(QEMU_DIR)/fedora-r-test-key

BUILD := $(CURDIR)/build

# ── Build RV64 spike-r binaries ───────────────────────────────────────────────
# Mounts $(CURDIR)/.. (the spikes/ parent) as /spikes inside the container so
# the build can reference shared headers if needed.

# docker-build-r: cross-compile spike-r RV64 binaries.
#
# Uses a named container + docker cp to avoid Docker Desktop on macOS bind-mount
# write-back latency (if -v writes are async, the QEMU guest may read stale
# binaries via virtfs even though the host ls shows the new file).
# docker cp copies from the container's overlay FS to the host atomically.
.PHONY: docker-build-r
docker-build-r:
	mkdir -p $(BUILD)
	@docker rm -f spike-r-build 2>/dev/null || true
	docker run --platform $(DOCKER_PLATFORM) --name spike-r-build \
	    -v $(CURDIR)/..:/spikes:ro -w /spikes/spike-r \
	    $(DOCKER_IMAGE) /bin/bash -c '\
	        set -eu; \
	        echo "--- seccomp_raw_test (riscv64-linux-gnu, static) ---"; \
	        riscv64-linux-gnu-gcc -O2 -static \
	            -o /tmp/seccomp_raw_test seccomp_raw_test.c; \
	        echo "--- launcher_r (riscv64-linux-gnu, dynamic) ---"; \
	        riscv64-linux-gnu-gcc -O2 \
	            -o /tmp/launcher_r launcher_r.c'
	docker cp spike-r-build:/tmp/seccomp_raw_test $(BUILD)/seccomp_raw_test
	docker cp spike-r-build:/tmp/launcher_r $(BUILD)/launcher_r
	@docker rm spike-r-build
	chmod +x $(BUILD)/seccomp_raw_test $(BUILD)/launcher_r
	@echo "--- ELF headers ---"
	docker run --rm --platform $(DOCKER_PLATFORM) \
	    -v $(BUILD):/out \
	    $(DOCKER_IMAGE) /bin/bash -c '\
	        for f in /out/seccomp_raw_test /out/launcher_r; do \
	            echo "== $$f =="; \
	            riscv64-linux-gnu-readelf -h "$$f" \
	                | grep -E "Class:|Machine:|Type:|Build ID:"; \
	        done; \
	        riscv64-linux-gnu-readelf -n /out/seccomp_raw_test | grep "Build ID"'

# Rebuild the RV32 adversary via spike-h if it does not exist.
.PHONY: docker-build-adversary
docker-build-adversary:
	$(MAKE) -C $(SPIKE_H_DIR) docker-build-carts

# ── QEMU: Stage 0-2 test (Fedora 42, CONFIG_COMPAT) ──────────────────────────
# Boots the Fedora 42 RISC-V guest, mounts the entire spikes/ directory via
# 9p virtfs, then runs run-guest-tests-r.sh which exercises:
#   Stage 0: environment sanity (seccomp nr 277 present, CONFIG_COMPAT confirmed)
#   Stage 1: seccomp_raw_test — arch-dispatch + LIFO semantics
#   Stage 2: launcher_r adversary probes (all 5 checks)

.PHONY: qemu-test-fedora-r
qemu-test-fedora-r: docker-build-r
	@if [ ! -f $(FEDORA_IMG) ]; then \
	    echo "Fedora image not found — run: make -C ../spike-h fedora-image"; exit 1; \
	fi
	@if [ ! -f $(QEMU_FW) ]; then \
	    echo "QEMU firmware not found at $(QEMU_FW)"; exit 1; \
	fi
	@if [ ! -f $(SPIKE_H_DIR)/build/adversary ]; then \
	    echo "spike-h/build/adversary missing — run: make docker-build-adversary"; exit 1; \
	fi
	@mkdir -p $(BUILD)/results
	@echo "Generating ephemeral SSH key..."
	@rm -f $(FEDORA_KEY) $(FEDORA_KEY).pub
	ssh-keygen -t ed25519 -f $(FEDORA_KEY) -N "" -q -C spike-r-test
	@echo "Building FAT cidata seed..."
	@{ echo '#cloud-config'; \
	   echo 'users:'; \
	   echo '  - name: fedora'; \
	   echo '    sudo: ALL=(ALL) NOPASSWD:ALL'; \
	   echo '    ssh_authorized_keys:'; \
	   printf '      - %s\n' "$$(cat $(FEDORA_KEY).pub)"; \
	} > $(QEMU_DIR)/fedora-r-userdata
	dd if=/dev/zero of=$(QEMU_DIR)/fedora-r-seed.img bs=1k count=1024 2>/dev/null
	mformat -i $(QEMU_DIR)/fedora-r-seed.img -v CIDATA -F ::
	mcopy -i $(QEMU_DIR)/fedora-r-seed.img $(QEMU_DIR)/fedora-r-userdata ::user-data
	mcopy -i $(QEMU_DIR)/fedora-r-seed.img $(SPIKE_H_DIR)/qemu/cloud-init/meta-data ::meta-data
	@echo "Creating fresh qcow2 overlay..."
	qemu-img create -f qcow2 -b $(abspath $(FEDORA_IMG)) -F qcow2 \
	    $(QEMU_DIR)/fedora-r-test.qcow2 2>/dev/null
	@echo "Booting Fedora RISC-V (SSH on host:2222)..."
	qemu-system-riscv64 \
	    -machine virt -display none -m 4G -smp 4 \
	    -bios $(QEMU_FW) \
	    -kernel $(QEMU_UBOOT) \
	    -serial file:$(QEMU_DIR)/fedora-r-console.log \
	    -drive file=$(QEMU_DIR)/fedora-r-test.qcow2,format=qcow2,if=virtio \
	    -drive file=$(QEMU_DIR)/fedora-r-seed.img,format=raw,if=virtio,readonly=on \
	    -netdev user,id=net0,hostfwd=tcp::2222-:22 \
	    -device virtio-net-device,netdev=net0 \
	    -virtfs local,path=$(CURDIR)/..,mount_tag=spikes,security_model=mapped-xattr,id=spikes \
	    -daemonize -pidfile $(QEMU_DIR)/fedora-r-test.pid 2>/dev/null
	@echo "Waiting for SSH daemon to be ready (up to 4 min)..."
	@for i in $$(seq 1 120); do \
	    ssh -i $(FEDORA_KEY) \
	        -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
	        -o ConnectTimeout=3 -o BatchMode=yes -o IdentitiesOnly=yes \
	        -p 2222 fedora@localhost true 2>/dev/null && break; \
	    sleep 2; \
	done
	@echo "Copying fresh binaries directly to guest (bypass 9p virtfs caching)..."
	scp -i $(FEDORA_KEY) \
	    -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
	    -o IdentitiesOnly=yes \
	    -P 2222 \
	    $(BUILD)/seccomp_raw_test $(BUILD)/launcher_r \
	    fedora@localhost:/tmp/
	@echo "Running Stage 0-2 tests via SSH..."
	ssh -i $(FEDORA_KEY) \
	    -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
	    -o IdentitiesOnly=yes \
	    -p 2222 fedora@localhost \
	    'sudo mkdir -p /mnt/spikes && \
	     sudo mount -t 9p -o trans=virtio,version=9p2000.L spikes /mnt/spikes 2>/dev/null; \
	     sudo bash /mnt/spikes/spike-r/run-guest-tests-r.sh \
	         /tmp/seccomp_raw_test /tmp/launcher_r' || true
	@echo "Stopping QEMU..."
	@kill $$(cat $(QEMU_DIR)/fedora-r-test.pid) 2>/dev/null || true
	@rm -f $(QEMU_DIR)/fedora-r-test.qcow2 $(QEMU_DIR)/fedora-r-test.pid \
	       $(QEMU_DIR)/fedora-r-seed.img $(QEMU_DIR)/fedora-r-userdata \
	       $(FEDORA_KEY) $(FEDORA_KEY).pub
	@echo ""
	@echo "=== Stage 0-2 results ==="
	@if [ -f $(BUILD)/results/guest-run-r.log ]; then \
	    grep -E "(PASS|FAIL|SUMMARY|===|Stage)" $(BUILD)/results/guest-run-r.log || true; \
	else \
	    echo "($(BUILD)/results/guest-run-r.log not found — check $(QEMU_DIR)/fedora-r-console.log)"; \
	fi

# ── QEMU: Stage 3 strace allowlist derivation ────────────────────────────────
# Boots the Fedora 42 RISC-V guest, mounts the entire spikes/ directory via
# 9p virtfs, and runs run-guest-stage3.sh which:
#   1. Installs strace + gcc via dnf
#   2. Builds rv32emu from spike-a source (CONFIG_EXT_A + multi-dynload patch)
#   3. Runs strace over spike-i/q carts
#   4. Outputs syscall union + allowlist suggestion to build/results/stage3-strace/
#
# Prerequisites:
#   Stages 0-2 must have passed (make qemu-test-fedora-r).
#   spike-i must have been staged: make -C ../spike-i qemu-fedora-stage

.PHONY: qemu-stage3-strace
qemu-stage3-strace:
	@if [ ! -f $(FEDORA_IMG) ]; then \
	    echo "Fedora image not found — run: make -C ../spike-h fedora-image"; exit 1; \
	fi
	@if [ ! -f $(QEMU_FW) ]; then \
	    echo "QEMU firmware not found at $(QEMU_FW)"; exit 1; \
	fi
	@if [ ! -d $(SPIKE_H_DIR)/../spike-i/build/guest-stage/cases ]; then \
	    echo "spike-i guest-stage missing — run: make -C ../spike-i qemu-fedora-stage"; exit 1; \
	fi
	@mkdir -p $(BUILD)/results
	@echo "Generating ephemeral SSH key..."
	@rm -f $(FEDORA_KEY) $(FEDORA_KEY).pub
	ssh-keygen -t ed25519 -f $(FEDORA_KEY) -N "" -q -C spike-r-stage3
	@{ echo '#cloud-config'; \
	   echo 'users:'; \
	   echo '  - name: fedora'; \
	   echo '    sudo: ALL=(ALL) NOPASSWD:ALL'; \
	   echo '    ssh_authorized_keys:'; \
	   printf '      - %s\n' "$$(cat $(FEDORA_KEY).pub)"; \
	} > $(QEMU_DIR)/fedora-r-userdata
	dd if=/dev/zero of=$(QEMU_DIR)/fedora-r-seed.img bs=1k count=1024 2>/dev/null
	mformat -i $(QEMU_DIR)/fedora-r-seed.img -v CIDATA -F ::
	mcopy -i $(QEMU_DIR)/fedora-r-seed.img $(QEMU_DIR)/fedora-r-userdata ::user-data
	mcopy -i $(QEMU_DIR)/fedora-r-seed.img $(SPIKE_H_DIR)/qemu/cloud-init/meta-data ::meta-data
	@echo "Creating fresh qcow2 overlay..."
	qemu-img create -f qcow2 -b $(abspath $(FEDORA_IMG)) -F qcow2 \
	    $(QEMU_DIR)/fedora-r-test.qcow2 2>/dev/null
	@echo "Booting Fedora RISC-V (SSH on host:2222)..."
	qemu-system-riscv64 \
	    -machine virt -display none -m 4G -smp 4 \
	    -bios $(QEMU_FW) \
	    -kernel $(QEMU_UBOOT) \
	    -serial file:$(QEMU_DIR)/fedora-r-console.log \
	    -drive file=$(QEMU_DIR)/fedora-r-test.qcow2,format=qcow2,if=virtio \
	    -drive file=$(QEMU_DIR)/fedora-r-seed.img,format=raw,if=virtio,readonly=on \
	    -netdev user,id=net0,hostfwd=tcp::2222-:22 \
	    -device virtio-net-device,netdev=net0 \
	    -virtfs local,path=$(CURDIR)/..,mount_tag=spikes,security_model=mapped-xattr,id=spikes \
	    -daemonize -pidfile $(QEMU_DIR)/fedora-r-test.pid 2>/dev/null
	@echo "Waiting for SSH daemon to be ready (up to 6 min)..."
	@for i in $$(seq 1 180); do \
	    ssh -i $(FEDORA_KEY) \
	        -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
	        -o ConnectTimeout=3 -o BatchMode=yes -o IdentitiesOnly=yes \
	        -p 2222 fedora@localhost true 2>/dev/null && break; \
	    sleep 2; \
	done
	@echo "Running Stage 3 in guest (builds rv32emu + strace; ~10 min)..."
	ssh -i $(FEDORA_KEY) \
	    -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
	    -o IdentitiesOnly=yes \
	    -o ServerAliveInterval=60 -o ServerAliveCountMax=20 \
	    -p 2222 fedora@localhost \
	    'sudo mkdir -p /mnt/spikes && \
	     sudo mount -t 9p -o trans=virtio,version=9p2000.L spikes /mnt/spikes 2>/dev/null; \
	     sudo bash /mnt/spikes/spike-r/run-guest-stage3.sh' || true
	@echo "Stopping QEMU..."
	@kill $$(cat $(QEMU_DIR)/fedora-r-test.pid) 2>/dev/null || true
	@rm -f $(QEMU_DIR)/fedora-r-test.qcow2 $(QEMU_DIR)/fedora-r-test.pid \
	       $(QEMU_DIR)/fedora-r-seed.img $(QEMU_DIR)/fedora-r-userdata \
	       $(FEDORA_KEY) $(FEDORA_KEY).pub
	@echo ""
	@echo "=== Stage 3 results ==="
	@if [ -f $(BUILD)/results/stage3-build.log ]; then \
	    grep -E "(PASS|FAIL|SKIP|SUMMARY|===|rv32emu:)" \
	        $(BUILD)/results/stage3-build.log || true; \
	else \
	    echo "($(BUILD)/results/stage3-build.log not found — check $(QEMU_DIR)/fedora-r-console.log)"; \
	fi
	@if [ -f $(BUILD)/results/stage3-strace/syscall_names.txt ]; then \
	    echo ""; echo "Observed syscall names:"; \
	    cat $(BUILD)/results/stage3-strace/syscall_names.txt; \
	fi

# ── clean ─────────────────────────────────────────────────────────────────────

.PHONY: clean
clean:
	$(RM) -r build
	$(RM) -f $(QEMU_DIR)/fedora-r-*.{img,qcow2,log,pid} \
	         $(QEMU_DIR)/fedora-r-userdata \
	         $(QEMU_DIR)/fedora-r-test-key* 2>/dev/null || true
