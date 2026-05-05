## Spike J — debugger composition (master_hook + DAP + GDB stub).
##
## Targets:
##   make docker-build               — build fc32-spike-j (extends fc32-spike-i)
##
## Stage 1 — master hook composition / overhead
##   make master-overhead            — run case_c under all five variants
##   make master-throttle-accuracy   — throttle regression on the 5-bench set
##
## Stage 2 — DAP server / breakpoints / step / locals
##   make dap-test                   — programmatic DAP regression
##   make run-dap                    — start dap_lua_host with case_c_dbg/cart.luac
##
## Stage 3 — GDB stub / libraries-svr4
##   make gdb-test                   — programmatic gdb-multiarch regression
##   make run-gdb                    — start gdb_stub_host on port 1234
##
## Stage 4 / 5 — reload-while-debugging
##   make dap-reload-test            — DAP synthetic reload (shifted + deleted)
##   make gdb-reload-test            — GDB qFc32:reload smoke test
##
## Stage 6 — VS Code launch configurations
##   See cases/case_c_dbg/.vscode/launch.json (DAP)
##   See cases/case_b/.vscode/launch.json     (GDB attach)

DOCKER_IMAGE    ?= fc32-spike-j
SPIKE_I_IMAGE   ?= fc32-spike-i
DOCKER_PLATFORM := linux/arm64

DAP_PORT ?= 5678
GDB_PORT ?= 1234

.PHONY: docker-build
docker-build:
	$(MAKE) -C ../spike-i docker-build
	docker build --platform $(DOCKER_PLATFORM) \
	    --build-arg SPIKE_I_IMAGE=$(SPIKE_I_IMAGE) \
	    -t $(DOCKER_IMAGE) .

## ── Stage 1 — master hook composition ─────────────────────────────────────

VARIANTS := master-off master-budget master-budget-dap-idle master-throttle

.PHONY: master-overhead
master-overhead: docker-build
	@mkdir -p baselines
	@for v in $(VARIANTS); do \
	    echo "=== variant $$v ==="; \
	    docker run --rm --platform $(DOCKER_PLATFORM) \
	        -v "$$PWD/variants/$$v:/spike-j/lib-override" \
	        $(DOCKER_IMAGE) sh -c "\
	            cp /spike-j/variants/$$v.so /spike-j/lib/libconsolelua.so && \
	            /spike-a/rv32emu/build/rv32emu -L /spike-j/lib /spike-j/cases/case_c/cart_c" \
	        | tee baselines/master-overhead-$$v.txt | tail -3 ; \
	done

.PHONY: master-throttle-accuracy
master-throttle-accuracy: docker-build
	@mkdir -p baselines
	@echo "Throttle accuracy regression — runs Spike G.3's five benches under MASTER_THROTTLE."
	@echo "(Reuses spike-g.3's Pi calibration; this target documents the procedure;"
	@echo " full bench run requires the spike-g.3 build artifacts mounted into the image.)"
	@docker run --rm --platform $(DOCKER_PLATFORM) $(DOCKER_IMAGE) \
	    sh -c "echo 'master-throttle-accuracy: deferred — see baselines/throttle-accuracy-procedure.md'" \
	    | tee baselines/master-throttle-accuracy.txt

## ── Stage 2 — DAP server ──────────────────────────────────────────────────

.PHONY: run-dap
run-dap: docker-build
	docker run --rm -it --platform $(DOCKER_PLATFORM) \
	    -p $(DAP_PORT):$(DAP_PORT) \
	    $(DOCKER_IMAGE) \
	    /usr/local/bin/dap_lua_host \
	        /spike-j/cases/case_c_dbg/build/cart.luac \
	        --port $(DAP_PORT) --wait \
	        --source cases/case_c_dbg/main.lua

.PHONY: dap-test
dap-test: docker-build
	@mkdir -p baselines
	docker run --rm --platform $(DOCKER_PLATFORM) $(DOCKER_IMAGE) sh -c "\
	    /usr/local/bin/dap_lua_host /spike-j/cases/case_c_dbg/build/cart.luac \
	        --port $(DAP_PORT) --source cases/case_c_dbg/main.lua --wait & \
	    DAP_PID=\$$!; \
	    sleep 1; \
	    cd /spike-j/host && CART_LINE=4 \
	        CART_SOURCE=cases/case_c_dbg/main.lua \
	        node dap_test.js $(DAP_PORT) breakpoint; \
	    RC=\$$?; \
	    kill \$$DAP_PID 2>/dev/null; \
	    exit \$$RC" \
	    | tee baselines/dap-test.txt

.PHONY: dap-reload-test
dap-reload-test: docker-build
	@mkdir -p baselines
	docker run --rm --platform $(DOCKER_PLATFORM) $(DOCKER_IMAGE) sh -c "\
	    set -e; \
	    cd /spike-j/host; \
	    for mode in reload-shifted reload-deleted; do \
	        /usr/local/bin/dap_lua_host /spike-j/cases/case_c_dbg/build/cart.luac \
	            --port $(DAP_PORT) --source cases/case_c_dbg/main.lua --wait & \
	        DAP_PID=\$$!; \
	        sleep 1; \
	        SHIFTED_LUAC=/spike-j/cases/case_c_dbg/build/cart_shifted.luac \
	        DELETED_LUAC=/spike-j/cases/case_c_dbg/build/cart_deleted.luac \
	        CART_LINE=4 CART_SOURCE=cases/case_c_dbg/main.lua \
	            node dap_test.js $(DAP_PORT) \$$mode; \
	        RC=\$$?; \
	        kill \$$DAP_PID 2>/dev/null; \
	        wait \$$DAP_PID 2>/dev/null || true; \
	        if [ \$$RC -ne 0 ]; then exit \$$RC; fi; \
	    done" \
	    | tee baselines/dap-reload-test.txt

## ── Stage 3 — GDB stub ────────────────────────────────────────────────────

.PHONY: run-gdb
run-gdb: docker-build
	docker run --rm -it --platform $(DOCKER_PLATFORM) \
	    -p $(GDB_PORT):$(GDB_PORT) \
	    $(DOCKER_IMAGE) \
	    /usr/local/bin/gdb_stub_host \
	        --port $(GDB_PORT) \
	        --exec /spike-j/cases/case_b/cart_b \
	        --libconsole /spike-j/lib/libconsole.so \
	        --libconsolelua /spike-j/lib/libconsolelua.so

.PHONY: gdb-test
gdb-test: docker-build
	@mkdir -p baselines
	docker run --rm --platform $(DOCKER_PLATFORM) $(DOCKER_IMAGE) sh -c "\
	    /usr/local/bin/gdb_stub_host --port $(GDB_PORT) \
	        --exec /spike-j/cases/case_b/cart_b & \
	    STUB_PID=\$$!; \
	    sleep 1; \
	    cd /spike-j/host && GDB_PORT=$(GDB_PORT) python3 gdb_test.py protocol; \
	    RC=\$$?; \
	    kill \$$STUB_PID 2>/dev/null; \
	    exit \$$RC" \
	    | tee baselines/gdb-test.txt

.PHONY: gdb-reload-test
gdb-reload-test: docker-build
	@mkdir -p baselines
	docker run --rm --platform $(DOCKER_PLATFORM) $(DOCKER_IMAGE) sh -c "\
	    /usr/local/bin/gdb_stub_host --port $(GDB_PORT) \
	        --exec /spike-j/cases/case_b/cart_b & \
	    STUB_PID=\$$!; \
	    sleep 1; \
	    cd /spike-j/host && GDB_PORT=$(GDB_PORT) python3 gdb_test.py reload; \
	    RC=\$$?; \
	    kill \$$STUB_PID 2>/dev/null; \
	    exit \$$RC" \
	    | tee baselines/gdb-reload-test.txt

## ── shell ─────────────────────────────────────────────────────────────────

.PHONY: docker-shell
docker-shell: docker-build
	docker run --rm -it --platform $(DOCKER_PLATFORM) $(DOCKER_IMAGE) /bin/bash

.PHONY: clean
clean:
	docker image rm -f $(DOCKER_IMAGE) 2>/dev/null || true
	$(RM) -r build baselines
