#!/bin/sh
set -eu
CONFIG=${1:-vendor/mbedtls/include/mbedtls/mbedtls_config.h}
if ! grep -q '^#define MBEDTLS_ENTROPY_HARDWARE_ALT$' "$CONFIG"; then
  sed -i '/^#define MBEDTLS_ENTROPY_C$/a\
#define MBEDTLS_ENTROPY_HARDWARE_ALT\
#define MBEDTLS_NO_PLATFORM_ENTROPY' "$CONFIG"
fi
