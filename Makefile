# CloudMoon PS3 build entry points.
# The upstream submodule supplies the known-good PSL1GHT application and media pipeline.
# IMPORTANT: baseline-pkg builds upstream Moonlight for Sunshine/GameStream, NOT CloudMoon.
PS3_BASE := vendor/PS3-Moonlight

.PHONY: all prepare baseline-pkg pkg host-test clean
all: host-test

prepare:
	@git submodule update --init --recursive
	$(MAKE) -C $(PS3_BASE) prepare

baseline-pkg: prepare
	$(MAKE) -C $(PS3_BASE) pkg

# Kept as an explicit alias for convenience; see warning in output.
pkg: baseline-pkg
	@echo "NOTE: This is the upstream Moonlight/Sunshine baseline PKG, not a CloudMoon client."

host-test:
	$(MAKE) -f Makefile.host-test clean test

clean:
	$(MAKE) -f Makefile.host-test clean
	@if [ -d "$(PS3_BASE)" ]; then $(MAKE) -C $(PS3_BASE) clean; fi
