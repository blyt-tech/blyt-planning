## Spike I — four cart cases × emulator / native / WASM targets

DOCKER_IMAGE    ?= fc32-spike-i
SPIKE_C_IMAGE   ?= fc32-spike-c
DOCKER_PLATFORM := linux/arm64

RV32EMU   := /spike-a/rv32emu/build/rv32emu
LIBDIR    := /spike-i/lib

.PHONY: docker-build
docker-build:
	$(MAKE) -C ../spike-c docker-build
	docker build --platform $(DOCKER_PLATFORM) \
	    --build-arg SPIKE_C_IMAGE=$(SPIKE_C_IMAGE) \
	    -t $(DOCKER_IMAGE) .

## Emulator targets
.PHONY: docker-run-case-a docker-run-case-b docker-run-case-c docker-run-case-d
docker-run-case-a: docker-build
	docker run --rm --platform $(DOCKER_PLATFORM) $(DOCKER_IMAGE) \
	    $(RV32EMU) -L $(LIBDIR) /spike-i/cases/case_a/cart_a

docker-run-case-b: docker-build
	docker run --rm --platform $(DOCKER_PLATFORM) $(DOCKER_IMAGE) \
	    $(RV32EMU) -L $(LIBDIR) /spike-i/cases/case_b/cart_b

docker-run-case-c: docker-build
	docker run --rm --platform $(DOCKER_PLATFORM) $(DOCKER_IMAGE) \
	    $(RV32EMU) -L $(LIBDIR) /spike-i/cases/case_c/cart_c

docker-run-case-d: docker-build
	docker run --rm --platform $(DOCKER_PLATFORM) $(DOCKER_IMAGE) \
	    $(RV32EMU) -L $(LIBDIR) /spike-i/cases/case_d/cart_d

.PHONY: docker-run-all
docker-run-all: docker-run-case-a docker-run-case-b docker-run-case-c docker-run-case-d

## Stage 3 — native-target runs via qemu-riscv32-static (user-mode).
## Each cart binary's PT_INTERP points at /lib/ld-musl-riscv32.so.1 (installed
## in the image from the musl.cc prebuilt cross-toolchain). LD_LIBRARY_PATH
## directs the dynamic linker at the spike-i lib directory so DT_NEEDED entries
## libconsole.so and libconsolelua.so resolve.
QEMU_USER := /usr/bin/qemu-riscv32-static
LD_PATH   := /spike-i/lib

.PHONY: qemu-user-run-case-a qemu-user-run-case-b qemu-user-run-case-c qemu-user-run-case-d
qemu-user-run-case-a: docker-build
	docker run --rm --platform $(DOCKER_PLATFORM) -e LD_LIBRARY_PATH=$(LD_PATH) $(DOCKER_IMAGE) \
	    $(QEMU_USER) /spike-i/cases/case_a/cart_a

qemu-user-run-case-b: docker-build
	docker run --rm --platform $(DOCKER_PLATFORM) -e LD_LIBRARY_PATH=$(LD_PATH) $(DOCKER_IMAGE) \
	    $(QEMU_USER) /spike-i/cases/case_b/cart_b

qemu-user-run-case-c: docker-build
	docker run --rm --platform $(DOCKER_PLATFORM) -e LD_LIBRARY_PATH=$(LD_PATH) $(DOCKER_IMAGE) \
	    $(QEMU_USER) /spike-i/cases/case_c/cart_c

qemu-user-run-case-d: docker-build
	docker run --rm --platform $(DOCKER_PLATFORM) -e LD_LIBRARY_PATH=$(LD_PATH) $(DOCKER_IMAGE) \
	    $(QEMU_USER) /spike-i/cases/case_d/cart_d

.PHONY: qemu-user-run-all
qemu-user-run-all: qemu-user-run-case-a qemu-user-run-case-b qemu-user-run-case-c qemu-user-run-case-d

## Stage 3 — full-system QEMU Fedora 42 RV64 boot.
## Reuses spike-h's already-downloaded Fedora qcow2 and U-Boot ELF. Stages
## spike-i artifacts (carts, libs, ld-musl, run-guest-tests.sh) into a host
## directory, virtfs-shares it at /mnt/spike-i in the guest, and SSHs in to
## run the tests.
SPIKE_H_DIR    := $(abspath ../spike-h)
FEDORA_IMG     := $(SPIKE_H_DIR)/qemu/Fedora-Cloud-Base-Generic-42.riscv64.qcow2
QEMU_UBOOT     := $(SPIKE_H_DIR)/qemu/uboot-riscv64.elf
QEMU_FW        := /opt/homebrew/share/qemu/opensbi-riscv64-generic-fw_dynamic.bin

QEMU_DIR       := qemu
GUEST_STAGE    := build/guest-stage
FEDORA_KEY     := $(QEMU_DIR)/fedora-test-key

.PHONY: qemu-fedora-stage
qemu-fedora-stage: docker-build
	@mkdir -p $(GUEST_STAGE)/lib $(GUEST_STAGE)/qemu-guest \
	          $(GUEST_STAGE)/cases/case_a $(GUEST_STAGE)/cases/case_b \
	          $(GUEST_STAGE)/cases/case_c $(GUEST_STAGE)/cases/case_d \
	          build/results
	@echo "Extracting spike-i artifacts from $(DOCKER_IMAGE)..."
	docker run --rm --platform $(DOCKER_PLATFORM) $(DOCKER_IMAGE) tar -C / -cf - \
	    spike-i/lib/libconsole.so spike-i/lib/libconsolelua.so \
	    spike-i/cases/case_a/cart_a spike-i/cases/case_b/cart_b \
	    spike-i/cases/case_c/cart_c spike-i/cases/case_d/cart_d \
	    lib/ld-musl-riscv32.so.1 \
	    | tar -C $(GUEST_STAGE) --strip-components=0 -xf -
	mv $(GUEST_STAGE)/spike-i/lib/* $(GUEST_STAGE)/lib/
	for c in a b c d; do mv $(GUEST_STAGE)/spike-i/cases/case_$$c/cart_$$c \
	                        $(GUEST_STAGE)/cases/case_$$c/cart_$$c; done
	mv $(GUEST_STAGE)/lib/ld-musl-riscv32.so.1 $(GUEST_STAGE)/qemu-guest/ld-musl-riscv32.so.1 \
	    || mv $(GUEST_STAGE)/spike-i/../lib/ld-musl-riscv32.so.1 $(GUEST_STAGE)/qemu-guest/ld-musl-riscv32.so.1 \
	    2>/dev/null || true
	# `lib/ld-musl-...` extracted at top level of stage dir — relocate it
	test -f $(GUEST_STAGE)/qemu-guest/ld-musl-riscv32.so.1 || \
	    mv $(GUEST_STAGE)/lib/ld-musl-riscv32.so.1 $(GUEST_STAGE)/qemu-guest/ld-musl-riscv32.so.1 || true
	rm -rf $(GUEST_STAGE)/spike-i
	cp run-guest-tests.sh $(GUEST_STAGE)/run-guest-tests.sh
	chmod +x $(GUEST_STAGE)/run-guest-tests.sh
	@echo "Stage layout:"
	@find $(GUEST_STAGE) -maxdepth 3 -type f | sort

.PHONY: qemu-fedora-test
qemu-fedora-test: qemu-fedora-stage
	@if [ ! -f $(FEDORA_IMG) ]; then \
	    echo "Fedora image not found at $(FEDORA_IMG) — run: make -C $(SPIKE_H_DIR) fedora-image"; exit 1; \
	fi
	@if [ ! -f $(QEMU_UBOOT) ]; then \
	    echo "U-Boot ELF not found at $(QEMU_UBOOT) — run: make -C $(SPIKE_H_DIR) qemu-uboot"; exit 1; \
	fi
	@mkdir -p $(QEMU_DIR)
	@echo "Generating ephemeral SSH key..."
	@rm -f $(FEDORA_KEY) $(FEDORA_KEY).pub
	ssh-keygen -t ed25519 -f $(FEDORA_KEY) -N "" -q -C spike-i-test
	@{ echo '#cloud-config'; \
	   echo 'users:'; \
	   echo '  - name: fedora'; \
	   echo '    sudo: ALL=(ALL) NOPASSWD:ALL'; \
	   echo '    ssh_authorized_keys:'; \
	   printf '      - %s\n' "$$(cat $(FEDORA_KEY).pub)"; \
	} > $(QEMU_DIR)/fedora-userdata
	dd if=/dev/zero of=$(QEMU_DIR)/fedora-seed.img bs=1k count=1024 2>/dev/null
	mformat -i $(QEMU_DIR)/fedora-seed.img -v CIDATA -F ::
	mcopy -i $(QEMU_DIR)/fedora-seed.img $(QEMU_DIR)/fedora-userdata ::user-data
	mcopy -i $(QEMU_DIR)/fedora-seed.img qemu-cloud-init/meta-data ::meta-data
	@echo "Creating fresh qcow2 overlay..."
	qemu-img create -f qcow2 -b $(abspath $(FEDORA_IMG)) -F qcow2 \
	    $(QEMU_DIR)/fedora-test.qcow2 2>/dev/null
	@echo "Booting Fedora RISC-V (SSH on host:2222)..."
	qemu-system-riscv64 \
	    -machine virt -display none -m 4G -smp 4 \
	    -bios $(QEMU_FW) \
	    -kernel $(QEMU_UBOOT) \
	    -serial file:$(QEMU_DIR)/fedora-console.log \
	    -drive file=$(QEMU_DIR)/fedora-test.qcow2,format=qcow2,if=virtio \
	    -drive file=$(QEMU_DIR)/fedora-seed.img,format=raw,if=virtio,readonly=on \
	    -netdev user,id=net0,hostfwd=tcp::2222-:22 \
	    -device virtio-net-device,netdev=net0 \
	    -virtfs local,path=$(abspath $(GUEST_STAGE)),mount_tag=spike_i,security_model=mapped-xattr,id=spike_i \
	    -daemonize -pidfile $(QEMU_DIR)/fedora-test.pid 2>/dev/null
	@echo "Waiting for SSH (up to 6 min)..."
	@for i in $$(seq 1 180); do nc -z localhost 2222 2>/dev/null && break; sleep 2; done
	@echo "(SSH port reachable; retrying ssh for up to 3 min while cloud-init finishes...)"
	@for i in $$(seq 1 36); do \
	    ssh -i $(FEDORA_KEY) -o ConnectTimeout=5 \
	        -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
	        -p 2222 fedora@localhost true 2>/dev/null && break; \
	    sleep 5; \
	done
	@echo "Running spike-i tests via SSH..."
	-ssh -i $(FEDORA_KEY) -o ConnectTimeout=10 \
	    -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
	    -p 2222 fedora@localhost \
	    'sudo mkdir -p /mnt/spike-i && \
	     sudo mount -t 9p -o trans=virtio,version=9p2000.L spike_i /mnt/spike-i && \
	     sudo bash /mnt/spike-i/run-guest-tests.sh'
	@echo "Stopping QEMU..."
	@kill $$(cat $(QEMU_DIR)/fedora-test.pid) 2>/dev/null || true
	@rm -f $(QEMU_DIR)/fedora-test.qcow2 $(QEMU_DIR)/fedora-test.pid \
	       $(QEMU_DIR)/fedora-seed.img $(QEMU_DIR)/fedora-userdata \
	       $(FEDORA_KEY) $(FEDORA_KEY).pub
	@echo ""
	@echo "=== Spike I Stage 3 Fedora results ==="
	@if [ -f $(GUEST_STAGE)/build/results/guest-run.log ]; then \
	    cp $(GUEST_STAGE)/build/results/guest-run.log build/results/fedora-guest-run.log; \
	    grep -E "(PASS|FAIL|=== |Spike I)" build/results/fedora-guest-run.log || true; \
	else \
	    echo "(no guest-run.log — check $(QEMU_DIR)/fedora-console.log)"; \
	fi

.PHONY: docker-shell
docker-shell: docker-build
	docker run --rm -it --platform $(DOCKER_PLATFORM) $(DOCKER_IMAGE) /bin/bash

## Stage 4 — rv32emu compiled to WASM with the multi-dynload patch.
## Reuses spike-e's emsdk install. Stages an upstream rv32emu source tree,
## applies the spike-i multi-dynload patch, embeds the C-cart artefacts
## (cart_a, cart_b, libconsole.so, libconsolelua.so) into the in-WASM
## filesystem, and runs each cart under Node + WASM through the same
## fc32_dynload as the native target.
SPIKE_E_DIR      := $(abspath ../spike-e)
SPIKE_A_DIR      := $(abspath ../spike-a)
EMSDK_DIR        := $(SPIKE_E_DIR)/emsdk
EM_PATH          := $(EMSDK_DIR)/upstream/emscripten:$(EMSDK_DIR):$(EMSDK_DIR)/node/22.16.0_64bit/bin
NODE             := $(EMSDK_DIR)/node/22.16.0_64bit/bin/node

WASM_DIR         := wasm
WASM_OUT         := build-wasm
WASM_RV32EMU     := $(WASM_DIR)/rv32emu
WASM_BIN         := $(WASM_RV32EMU)/$(WASM_OUT)/rv32emu.js

.PHONY: wasm-stage
wasm-stage: $(WASM_RV32EMU)/.spike-i-staged

$(WASM_RV32EMU)/.spike-i-staged: \
        $(WASM_DIR)/wasm.mk \
        $(WASM_DIR)/wasm_spike_i.config \
        $(WASM_DIR)/user-pre.js \
        patches/apply-multi-dynload.py \
        $(SPIKE_A_DIR)/rv32emu/Makefile
	@mkdir -p $(WASM_RV32EMU)
	rsync -a --delete \
	    --exclude=build/ \
	    --exclude=build-native/ \
	    --exclude=build-aarch64/ \
	    --exclude=build-wasm/ \
	    --exclude=demo/ \
	    --exclude=.config \
	    --exclude=.spike-i-staged \
	    $(SPIKE_A_DIR)/rv32emu/ $(WASM_RV32EMU)/
	cp $(WASM_DIR)/wasm.mk $(WASM_RV32EMU)/mk/wasm.mk
	cp $(WASM_DIR)/wasm_spike_i.config $(WASM_RV32EMU)/.config
	@mkdir -p $(WASM_RV32EMU)/assets/wasm/js
	cp $(WASM_DIR)/user-pre.js $(WASM_RV32EMU)/assets/wasm/js/user-pre.js
	cd $(WASM_RV32EMU) && python3 $(abspath patches/apply-multi-dynload.py)
	touch $@

## Extract artefacts from the spike-i image and stage them under
## $(WASM_RV32EMU)/$(WASM_OUT)/embed/ where wasm.mk's --embed-file flags
## look for them.
WASM_EMBED_DIR := $(WASM_RV32EMU)/$(WASM_OUT)/embed
WASM_EMBED_FILES := $(WASM_EMBED_DIR)/libconsole.so \
                    $(WASM_EMBED_DIR)/libconsolelua.so \
                    $(WASM_EMBED_DIR)/cart_a \
                    $(WASM_EMBED_DIR)/cart_b \
                    $(WASM_EMBED_DIR)/cart_c \
                    $(WASM_EMBED_DIR)/cart_d

.PHONY: wasm-embed
wasm-embed: $(WASM_EMBED_FILES)

$(WASM_EMBED_DIR)/libconsole.so: docker-build wasm-stage
	@mkdir -p $(WASM_EMBED_DIR)
	docker run --rm --platform $(DOCKER_PLATFORM) $(DOCKER_IMAGE) \
	    cat /spike-i/lib/libconsole.so > $@

$(WASM_EMBED_DIR)/libconsolelua.so: docker-build wasm-stage
	@mkdir -p $(WASM_EMBED_DIR)
	docker run --rm --platform $(DOCKER_PLATFORM) $(DOCKER_IMAGE) \
	    cat /spike-i/lib/libconsolelua.so > $@

$(WASM_EMBED_DIR)/cart_a: docker-build wasm-stage
	@mkdir -p $(WASM_EMBED_DIR)
	docker run --rm --platform $(DOCKER_PLATFORM) $(DOCKER_IMAGE) \
	    cat /spike-i/cases/case_a/cart_a > $@

$(WASM_EMBED_DIR)/cart_b: docker-build wasm-stage
	@mkdir -p $(WASM_EMBED_DIR)
	docker run --rm --platform $(DOCKER_PLATFORM) $(DOCKER_IMAGE) \
	    cat /spike-i/cases/case_b/cart_b > $@

$(WASM_EMBED_DIR)/cart_c: docker-build wasm-stage
	@mkdir -p $(WASM_EMBED_DIR)
	docker run --rm --platform $(DOCKER_PLATFORM) $(DOCKER_IMAGE) \
	    cat /spike-i/cases/case_c/cart_c > $@

$(WASM_EMBED_DIR)/cart_d: docker-build wasm-stage
	@mkdir -p $(WASM_EMBED_DIR)
	docker run --rm --platform $(DOCKER_PLATFORM) $(DOCKER_IMAGE) \
	    cat /spike-i/cases/case_d/cart_d > $@

.PHONY: wasm-build
wasm-build: $(WASM_BIN)

$(WASM_BIN): wasm-embed
	@if [ ! -f "$(EMSDK_DIR)/emsdk_env.sh" ]; then \
	    echo "spike-e emsdk missing — run: make -C $(SPIKE_E_DIR) emsdk"; exit 1; \
	fi
	cd $(WASM_RV32EMU) && \
	    PATH="$(EM_PATH):$$PATH" \
	    EMSDK="$(EMSDK_DIR)" \
	    EMSDK_NODE="$(NODE)" \
	    $(MAKE) OUT=$(WASM_OUT)

.PHONY: node-test-case-a node-test-case-b node-test-case-c node-test-case-d
node-test-case-a: wasm-build
	@cp $(WASM_DIR)/node_run.cjs $(WASM_RV32EMU)/$(WASM_OUT)/
	@echo "=== Node + WASM rv32emu running case_a ==="
	cd $(WASM_RV32EMU)/$(WASM_OUT) && \
	    "$(NODE)" node_run.cjs /spike-i/cases/case_a/cart_a

node-test-case-b: wasm-build
	@cp $(WASM_DIR)/node_run.cjs $(WASM_RV32EMU)/$(WASM_OUT)/
	@echo "=== Node + WASM rv32emu running case_b ==="
	cd $(WASM_RV32EMU)/$(WASM_OUT) && \
	    "$(NODE)" node_run.cjs /spike-i/cases/case_b/cart_b

node-test-case-c: wasm-build
	@cp $(WASM_DIR)/node_run.cjs $(WASM_RV32EMU)/$(WASM_OUT)/
	@echo "=== Node + WASM rv32emu running case_c ==="
	cd $(WASM_RV32EMU)/$(WASM_OUT) && \
	    "$(NODE)" node_run.cjs /spike-i/cases/case_c/cart_c

node-test-case-d: wasm-build
	@cp $(WASM_DIR)/node_run.cjs $(WASM_RV32EMU)/$(WASM_OUT)/
	@echo "=== Node + WASM rv32emu running case_d ==="
	cd $(WASM_RV32EMU)/$(WASM_OUT) && \
	    "$(NODE)" node_run.cjs /spike-i/cases/case_d/cart_d

.PHONY: node-test-c-all
node-test-c-all: node-test-case-a node-test-case-b

## All four cases under rv32emu-WASM. PLAN scopes Stage 4 to C carts only;
## this runs Lua carts through the same loader for completeness.
.PHONY: node-test-all
node-test-all: node-test-case-a node-test-case-b node-test-case-c node-test-case-d

## ── Stage 5 — Lua-direct WASM (no rv32emu, no dynamic linking) ────────────
## Compile libconsole_wasm.c + libconsolelua_wasm.c + Lua VM sources +
## cart-side data into a single static WASM module via Emscripten.
LD_DIR        := $(WASM_DIR)/lua-direct
LD_STAGE      := $(LD_DIR)/build
LD_OUT        := $(LD_DIR)/build/out

EMCC          := $(EMSDK_DIR)/upstream/emscripten/emcc

## Lua source files compiled into the consolelua-* modules (mirrors lib/Makefile's
## LUA_OMIT — drops lua.c/luac.c REPL/compiler mains and the libs the spike
## sandbox does not load).
LD_LUA_SRCS := lapi.c lcode.c lctype.c ldebug.c ldo.c ldump.c lfunc.c lgc.c \
               llex.c lmem.c lobject.c lopcodes.c lparser.c lstate.c lstring.c \
               ltable.c ltm.c lundump.c lvm.c lzio.c lauxlib.c lbaselib.c \
               lmathlib.c lstrlib.c ltablib.c

## Stage Lua sources + cart inputs onto the host, regenerating bytecode .inc
## files via the host-native 32-bit luac in the spike-i image.
.PHONY: lua-direct-stage
lua-direct-stage: $(LD_STAGE)/.staged

$(LD_STAGE)/.staged: docker-build $(LD_DIR)/libconsole_wasm.c \
                     $(LD_DIR)/libconsolelua_wasm.c $(LD_DIR)/wasm_main.c
	@mkdir -p $(LD_STAGE)/lua $(LD_STAGE)/case_c $(LD_STAGE)/case_d
	@echo "Extracting Lua source tree..."
	docker run --rm --platform $(DOCKER_PLATFORM) $(DOCKER_IMAGE) tar -C / -cf - \
	    spike-i/lua/src \
	    spike-i/cases/case_c/main.lua \
	    spike-i/cases/case_d/main.lua spike-i/cases/case_d/cart_lua_modules.c \
	    spike-i/cases/case_d/mylib.c \
	    spike-i/lib/include \
	    | tar -C $(LD_STAGE) --strip-components=2 -xf -
	@echo "Compiling Lua bytecode for cases c, d..."
	docker run --rm --platform $(DOCKER_PLATFORM) -v $(abspath $(LD_STAGE)):/work $(DOCKER_IMAGE) \
	    bash -c 'luac -o /work/case_c/cart.luac /work/case_c/main.lua && \
	             luac -o /work/case_d/cart.luac /work/case_d/main.lua && \
	             cd /work && \
	             xxd -i case_c/cart.luac | sed "s/^unsigned.*//" | sed "/^$$/d" \
	                 | sed "s/.*{//" | sed "s/}.*$$//" > case_c/cart_lua_bytes.inc && \
	             xxd -i case_d/cart.luac | sed "s/^unsigned.*//" | sed "/^$$/d" \
	                 | sed "s/.*{//" | sed "s/}.*$$//" > case_d/cart_lua_bytes.inc'
	## Local copies of cart C glue (portable across rv32 and wasm)
	cp ../spike-i/cases/case_c/cart_c.c $(LD_STAGE)/case_c/cart_c.c 2>/dev/null \
	    || cp /Users/tom/code/fc32/spikes/spike-i/cases/case_c/cart_c.c $(LD_STAGE)/case_c/cart_c.c
	cp /Users/tom/code/fc32/spikes/spike-i/cases/case_d/cart_d.c $(LD_STAGE)/case_d/cart_d.c
	@find $(LD_STAGE) -maxdepth 3 -type f | sort | head -30
	touch $@

## Build consolelua-c.{js,wasm} — case_c (Lua-only)
$(LD_OUT)/consolelua-c.js: lua-direct-stage
	@if [ ! -x "$(EMCC)" ]; then \
	    echo "Emscripten missing — run: make -C $(SPIKE_E_DIR) emsdk"; exit 1; \
	fi
	@mkdir -p $(LD_OUT)
	cd $(LD_STAGE) && PATH="$(EM_PATH):$$PATH" EMSDK="$(EMSDK_DIR)" \
	    "$(EMCC)" -O2 -DLUA_32BITS \
	    -Isrc -Icase_c \
	    $(abspath $(LD_DIR)/libconsole_wasm.c) \
	    $(abspath $(LD_DIR)/libconsolelua_wasm.c) \
	    $(abspath $(LD_DIR)/wasm_main.c) \
	    $(addprefix src/,$(LD_LUA_SRCS)) \
	    case_c/cart_c.c \
	    -sEXIT_RUNTIME=1 -sALLOW_MEMORY_GROWTH \
	    -o $(abspath $@)

## Build consolelua-d.{js,wasm} — case_d (Lua + C user library)
$(LD_OUT)/consolelua-d.js: lua-direct-stage
	@mkdir -p $(LD_OUT)
	cd $(LD_STAGE) && PATH="$(EM_PATH):$$PATH" EMSDK="$(EMSDK_DIR)" \
	    "$(EMCC)" -O2 -DLUA_32BITS \
	    -Isrc -Icase_d \
	    $(abspath $(LD_DIR)/libconsole_wasm.c) \
	    $(abspath $(LD_DIR)/libconsolelua_wasm.c) \
	    $(abspath $(LD_DIR)/wasm_main.c) \
	    $(addprefix src/,$(LD_LUA_SRCS)) \
	    case_d/cart_d.c case_d/cart_lua_modules.c case_d/mylib.c \
	    -sEXIT_RUNTIME=1 -sALLOW_MEMORY_GROWTH \
	    -o $(abspath $@)

.PHONY: lua-direct-build
lua-direct-build: $(LD_OUT)/consolelua-c.js $(LD_OUT)/consolelua-d.js

.PHONY: node-test-lua-direct-c node-test-lua-direct-d
node-test-lua-direct-c: $(LD_OUT)/consolelua-c.js
	@echo "=== Node + WASM (Lua-direct) running case_c ==="
	cd $(LD_OUT) && "$(NODE)" consolelua-c.js

node-test-lua-direct-d: $(LD_OUT)/consolelua-d.js
	@echo "=== Node + WASM (Lua-direct) running case_d ==="
	cd $(LD_OUT) && "$(NODE)" consolelua-d.js

.PHONY: node-test-lua-direct-all
node-test-lua-direct-all: node-test-lua-direct-c node-test-lua-direct-d

.PHONY: clean
clean:
	docker image rm -f $(DOCKER_IMAGE) 2>/dev/null || true
	$(RM) -r $(WASM_RV32EMU)
