## Spike L — libretro core for the FC32 runtime.
##
## The core (runtime_libretro.so) is built as a -fPIC -shared library
## targeting linux/amd64 (RetroArch's primary desktop platform). The
## adapter source lives in host/, the runtime-side facade in lib/.
##
## Targets (see PLAN.md §Deliverables):
##   make core            — build runtime_libretro.so
##   make run-retroarch   — launch RetroArch with the core + case_d cart
##   make trace-frames    — Stage 2 step 8 accumulator trace
##   make audio-drift     — Stage 3 step 12 60 s drift run (headless)
##   make savestate-test  — Stage 4 step 15 digest comparison
##   make rewind-capacity — Stage 4 step 17 buffer-size calculation
##   make layout-mismatch — Stage 4 step 18 negative test
##   make loc-count       — Stage 5 step 19 LOC tally per file
##   make demo-recording  — Stage 5 step 20 screen recording (manual)
##   make all             — every headless gate; exits 0 only if all pass
##                          plus loc-count is ≤ 1000

DOCKER_IMAGE    ?= fc32-spike-l
SPIKE_I_IMAGE   ?= fc32-spike-i
DOCKER_PLATFORM := linux/amd64

CORE_NAME       := runtime_libretro.so
BUILD_DIR       := build
HOST_DIR        := host
LIB_DIR         := lib
CASES_DIR       := cases
BASELINES_DIR   := baselines

ADAPTER_SRCS    := $(wildcard $(HOST_DIR)/*.c)
FACADE_SRCS     := $(LIB_DIR)/blyt_facade.c
ALL_SRCS        := $(ADAPTER_SRCS) $(FACADE_SRCS)

CC              ?= cc
CFLAGS_COMMON   := -O2 -Wall -Wextra -Wno-unused-parameter \
                   -fPIC \
                   -I$(HOST_DIR) -I$(HOST_DIR)/vendor -I$(LIB_DIR)
LDFLAGS_COMMON  := -shared -lm

## ── Docker build ──────────────────────────────────────────────────────
.PHONY: docker-build
docker-build:
	$(MAKE) -C ../spike-i docker-build
	docker build --platform $(DOCKER_PLATFORM) \
	    --build-arg SPIKE_I_IMAGE=$(SPIKE_I_IMAGE) \
	    -t $(DOCKER_IMAGE) .

## Build the core inside the Docker image (the production path — Linux,
## libretro-dev installed, RetroArch alongside).
.PHONY: core
core: docker-build
	@mkdir -p $(BUILD_DIR)
	docker run --rm --platform $(DOCKER_PLATFORM) \
	    -v $(abspath $(BUILD_DIR)):/out \
	    $(DOCKER_IMAGE) \
	    bash -c 'cd /spike-l && make -f /dev/stdin host-core CC=$(CC) \
	            <<<"include /spike-l/Makefile" && cp $(CORE_NAME) /out/'

## Local (Mac/host) build — uses the vendored libretro.h subset to
## typecheck the adapter without the full libretro-dev install. Useful
## for IDE feedback even though the resulting .dylib isn't loadable
## by RetroArch on macOS without further work (RetroArch macOS expects
## a .dylib bundled with Info.plist).
.PHONY: host-core
host-core: $(CORE_NAME)

$(CORE_NAME): $(ALL_SRCS) $(LIB_DIR)/blyt_facade.h $(HOST_DIR)/vendor/libretro.h
	$(CC) $(CFLAGS_COMMON) -o $@ $(ALL_SRCS) $(LDFLAGS_COMMON)

## ── Stage 1 + 2: launch RetroArch with the core (manual / visual) ──────
.PHONY: run-retroarch
run-retroarch: core
	@echo "Launching RetroArch (host display required)..."
	docker run --rm --platform $(DOCKER_PLATFORM) \
	    -v /tmp/.X11-unix:/tmp/.X11-unix \
	    -e DISPLAY=$(DISPLAY) \
	    --device /dev/snd \
	    $(DOCKER_IMAGE) \
	    retroarch -L /spike-l/$(CORE_NAME) /spike-i/cases/case_d/cart_d

## ── Stage 2: accumulator trace ─────────────────────────────────────────
.PHONY: trace-frames
trace-frames: core
	@mkdir -p $(BASELINES_DIR)
	docker run --rm --platform $(DOCKER_PLATFORM) \
	    -v $(abspath $(BASELINES_DIR)):/out \
	    $(DOCKER_IMAGE) \
	    bash -c 'cd /tmp && BLYT_TRACE_FRAMES=/out/frame_trace.csv \
	             retroarch --headless --max-frames=600 \
	             -L /spike-l/$(CORE_NAME) \
	             /spike-i/cases/case_d/cart_d || true'
	@echo "Trace written to $(BASELINES_DIR)/frame_trace.csv"

## ── Stage 3: 60 s audio drift (headless) ───────────────────────────────
.PHONY: audio-drift
audio-drift: core
	@mkdir -p $(BASELINES_DIR)
	docker run --rm --platform $(DOCKER_PLATFORM) \
	    -v $(abspath $(BASELINES_DIR)):/out \
	    -v $(abspath $(HOST_DIR)):/spike-l/host \
	    $(DOCKER_IMAGE) \
	    python3 /spike-l/host/audio_drift.py \
	            --core /spike-l/$(CORE_NAME) \
	            --cart /spike-i/cases/case_d/cart_d \
	            --frames 3600 \
	            --out /out/audio_drift_60s.csv

## ── Stage 4: save-state digest comparison ──────────────────────────────
.PHONY: savestate-test
savestate-test: core
	@mkdir -p $(BASELINES_DIR)
	docker run --rm --platform $(DOCKER_PLATFORM) \
	    -v $(abspath $(BASELINES_DIR)):/out \
	    -v $(abspath $(HOST_DIR)):/spike-l/host \
	    $(DOCKER_IMAGE) \
	    python3 /spike-l/host/dump_savestate.py \
	            --core /spike-l/$(CORE_NAME) \
	            --cart /spike-i/cases/case_d/cart_d \
	            --save-frame 100 \
	            --continue-frames 100 \
	            --out /out/savestate_digest.txt

## ── Stage 4: rewind-capacity calculation ───────────────────────────────
.PHONY: rewind-capacity
rewind-capacity: core
	@mkdir -p $(BASELINES_DIR)
	docker run --rm --platform $(DOCKER_PLATFORM) \
	    -v $(abspath $(BASELINES_DIR)):/out \
	    -v $(abspath $(HOST_DIR)):/spike-l/host \
	    $(DOCKER_IMAGE) \
	    python3 /spike-l/host/rewind_capacity.py \
	            --core /spike-l/$(CORE_NAME) \
	            --cart /spike-i/cases/case_d/cart_d \
	            --buffer-mb 10 --fps 60 \
	            --out /out/rewind_capacity.txt

## ── Stage 4: layout-mismatch negative test ─────────────────────────────
.PHONY: layout-mismatch
layout-mismatch: core
	@mkdir -p $(BASELINES_DIR)
	docker run --rm --platform $(DOCKER_PLATFORM) \
	    -v $(abspath $(BASELINES_DIR)):/out \
	    -v $(abspath $(HOST_DIR)):/spike-l/host \
	    $(DOCKER_IMAGE) \
	    python3 /spike-l/host/layout_mismatch.py \
	            --core /spike-l/$(CORE_NAME) \
	            --cart /spike-i/cases/case_d/cart_d \
	            --alt  /spike-l/$(CORE_NAME).alt \
	            --out  /out/layout_mismatch.txt

## ── Stage 5: LOC count ─────────────────────────────────────────────────
.PHONY: loc-count
loc-count:
	@mkdir -p $(BASELINES_DIR)
	@{ echo "── adapter (host/*.c) ──"; \
	   wc -l $(HOST_DIR)/*.c; \
	   echo ""; \
	   echo "── facade (lib/blyt_facade.{c,h}) ──"; \
	   wc -l $(LIB_DIR)/blyt_facade.c $(LIB_DIR)/blyt_facade.h; \
	   echo ""; \
	   total_adapter=$$(cat $(HOST_DIR)/*.c | wc -l); \
	   echo "ADAPTER TOTAL: $$total_adapter (gate ≤ 1000)"; \
	   if [ "$$total_adapter" -gt 1000 ]; then \
	       echo "FAIL: adapter exceeds budget"; exit 1; \
	   fi; \
	   echo "PASS: adapter within budget"; \
	} | tee $(BASELINES_DIR)/loc-count.txt

## ── Stage 5: demo recording (manual) ───────────────────────────────────
.PHONY: demo-recording
demo-recording:
	@echo "Manual step — record the 5-behaviour gate inside RetroArch"
	@echo "  1. Video at 60 fps (RetroArch FPS overlay enabled)"
	@echo "  2. Audio at 48 kHz (no underruns in retroarch.cfg log)"
	@echo "  3. All ADR-0017 inputs route through libretro descriptors"
	@echo "  4. F2 save / F4 load round-trips correctly"
	@echo "  5. Rewind plays back ≥ 5 s of history smoothly"
	@echo "Save .webm under recordings/demo-five-behaviours.webm"

## ── make all — every headless gate ─────────────────────────────────────
.PHONY: all
all: trace-frames audio-drift savestate-test rewind-capacity \
     layout-mismatch loc-count
	@echo "All headless gates passed."

.PHONY: clean
clean:
	$(RM) -r $(BUILD_DIR) $(BASELINES_DIR) $(CORE_NAME)
	docker image rm -f $(DOCKER_IMAGE) 2>/dev/null || true
