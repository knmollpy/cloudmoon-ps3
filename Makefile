# CloudMoon PS3 build entry points.
# Upstream submodule supplies the PSL1GHT application and hardware media pipeline.
# No target below claims CloudMoon streaming works until signaling + WebRTC/media are integrated.
PS3_BASE := vendor/PS3-Moonlight
.PHONY: all prepare baseline-pkg compile-cloudmoon-modules prototype-pkg pkg host-test clean
all: host-test
prepare:
	@git submodule update --init --recursive
	$(MAKE) -C $(PS3_BASE) prepare
baseline-pkg: prepare
	$(MAKE) -C $(PS3_BASE) pkg
compile-cloudmoon-modules: prepare
	./scripts/inject-cloudmoon-modules.sh "$(PS3_BASE)"
	$(MAKE) -C $(PS3_BASE) moonlight-ps3.elf
prototype-pkg: compile-cloudmoon-modules
	$(MAKE) -C $(PS3_BASE) pkg
	@echo "Prototype PKG generated with CloudMoon transport modules linked."
	@echo "WARNING: The running app still uses the upstream Sunshine/Moonlight flow; CloudMoon streaming is not yet functional."
pkg: prototype-pkg
host-test:
	$(MAKE) -f Makefile.host-test clean test
clean:
	$(MAKE) -f Makefile.host-test clean
	@if [ -d "$(PS3_BASE)" ]; then $(MAKE) -C $(PS3_BASE) clean; fi
