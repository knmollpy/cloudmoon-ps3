# CloudMoon PS3 direct WebRTC build.
PS3_BASE := vendor/PS3-Moonlight
PS3DEV ?= $(abspath $(PS3_BASE)/ps3dev)
PS3DEPS := $(CURDIR)/build/ps3deps
CLOUDMOON_WEBRTC ?= 0
export PS3DEV CLOUDMOON_WEBRTC
.PHONY: all prepare baseline-pkg compile-cloudmoon-modules prototype-pkg cloudmoon-pkg webrtc-stack pkg host-test clean
all: cloudmoon-pkg
prepare:
	@git submodule update --init --recursive
	$(MAKE) -C $(PS3_BASE) prepare
	@mkdir -p assets
	@if [ ! -s assets/cacert.pem ]; then curl -fsSL https://curl.se/ca/cacert.pem -o assets/cacert.pem; fi
baseline-pkg: prepare
	$(MAKE) -C $(PS3_BASE) pkg
webrtc-stack: prepare
	./scripts/patch-mbedtls-ps3.sh vendor/mbedtls/include/mbedtls/mbedtls_config.h
	@mkdir -p build
	cmake -S vendor/mbedtls -B build/mbedtls -DCMAKE_TOOLCHAIN_FILE=$(CURDIR)/cmake/ps3-toolchain.cmake -DCMAKE_INSTALL_PREFIX=$(PS3DEPS) -DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF
	cmake --build build/mbedtls --parallel 2
	cmake --install build/mbedtls
	cmake -S vendor/libdatachannel -B build/libdatachannel -DCMAKE_TOOLCHAIN_FILE=$(CURDIR)/cmake/ps3-toolchain.cmake -DCMAKE_PREFIX_PATH=$(PS3DEPS) -DCMAKE_INSTALL_PREFIX=$(PS3DEPS) -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DUSE_MBEDTLS=ON -DNO_WEBSOCKET=ON -DNO_EXAMPLES=ON -DNO_TESTS=ON
	cmake --build build/libdatachannel --parallel 2
	cmake --install build/libdatachannel
compile-cloudmoon-modules: prepare
	./scripts/inject-cloudmoon-modules.sh "$(PS3_BASE)"
	$(MAKE) -C $(PS3_BASE) moonlight-ps3.elf
prototype-pkg: compile-cloudmoon-modules
	$(MAKE) -C $(PS3_BASE) pkg
	@echo "PS3 PKG generated."
cloudmoon-pkg:
	$(MAKE) CLOUDMOON_WEBRTC=1 webrtc-stack
	$(MAKE) CLOUDMOON_WEBRTC=1 prototype-pkg
pkg: cloudmoon-pkg
host-test:
	$(MAKE) -f Makefile.host-test clean test
clean:
	$(MAKE) -f Makefile.host-test clean
	@if [ -d "$(PS3_BASE)" ]; then $(MAKE) -C $(PS3_BASE) clean; fi
