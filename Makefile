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
	./scripts/patch-ps3-sdk.sh
	sh ./scripts/patch-webrtc-ps3.sh
	./scripts/patch-mbedtls-ps3.sh vendor/mbedtls/include/mbedtls/mbedtls_config.h
	@mkdir -p build
	cmake -S vendor/mbedtls -B build/mbedtls -DCMAKE_TOOLCHAIN_FILE=$(CURDIR)/cmake/ps3-toolchain.cmake -DCMAKE_INSTALL_PREFIX=$(PS3DEPS) -DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF -DMBEDTLS_FATAL_WARNINGS=OFF
	cmake --build build/mbedtls --parallel 2
	cmake --install build/mbedtls
	cmake -S vendor/libdatachannel -B build/libdatachannel -DCMAKE_TOOLCHAIN_FILE=$(CURDIR)/cmake/ps3-toolchain.cmake -DCMAKE_PREFIX_PATH=$(PS3DEPS) -DCMAKE_INSTALL_PREFIX=$(PS3DEPS) -DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig=TRUE -DMbedTLS_INCLUDE_DIR=$(PS3DEPS)/include -DMbedTLS_LIBRARY=$(PS3DEPS)/lib/libmbedtls.a -DMbedCrypto_LIBRARY=$(PS3DEPS)/lib/libmbedcrypto.a -DMbedX509_LIBRARY=$(PS3DEPS)/lib/libmbedx509.a -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DUSE_MBEDTLS=ON -DENABLE_MBEDTLS=ON -DENABLE_OPENSSL=OFF -DUSE_NETTLE=OFF -DNO_WEBSOCKET=ON -DNO_EXAMPLES=ON -DNO_TESTS=ON -DWARNINGS_AS_ERRORS=OFF -DENABLE_WARNINGS=OFF -DENABLE_WARNINGS_AS_ERRORS=OFF -DLIBSRTP_TEST_APPS=OFF -DTHREADS_PREFER_PTHREAD_FLAG=FALSE -DCMAKE_THREAD_LIBS_INIT=-lpthread -Dsctp_inet=ON -Dsctp_inet6=OFF -Dsctp_build_programs=OFF -Dsctp_debug=OFF -Dsctp_invariants=OFF -Dhave_sys_queue_h=0 -Dhave_usrsctp_h=1 -Dsctp_werror=0
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
