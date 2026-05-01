## Spike C — Lua 5.4 as a host-provided shared library (RV32IMFC .so + rv32emu dynamic loader)
##
## Targets:
##   make docker-build    Build spike-c Docker image
##   make docker-run      Build then run the cart (expect "OK" on stdout)
##   make docker-shell    Drop into the container for debugging

DOCKER_IMAGE    ?= fc32-spike-c
SPIKE_A_IMAGE   ?= fc32-spike-a
DOCKER_PLATFORM := linux/arm64

.PHONY: docker-build
docker-build:
	$(MAKE) -C ../spike-a docker-build
	docker build --platform $(DOCKER_PLATFORM) \
	    --build-arg SPIKE_A_IMAGE=$(SPIKE_A_IMAGE) \
	    -t $(DOCKER_IMAGE) .

.PHONY: docker-run
docker-run: docker-build
	docker run --rm --platform $(DOCKER_PLATFORM) $(DOCKER_IMAGE) \
	    /spike-a/rv32emu/build/rv32emu -L /spike-c/lib \
	    /spike-c/cart/spike_c_cart.elf

.PHONY: docker-shell
docker-shell: docker-build
	docker run --rm -it --platform $(DOCKER_PLATFORM) $(DOCKER_IMAGE) /bin/bash

.PHONY: clean
clean:
	docker image rm -f $(DOCKER_IMAGE) 2>/dev/null || true
