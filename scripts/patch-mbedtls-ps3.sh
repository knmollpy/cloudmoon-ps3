#!/bin/sh
set -eu
CONFIG=${1:-vendor/mbedtls/include/mbedtls/mbedtls_config.h}

# libdatachannel's DTLS key-export callback requires this Mbed TLS feature.
if ! grep -q '^#define MBEDTLS_SSL_KEYING_MATERIAL_EXPORT$' "$CONFIG"; then
  if grep -q '^#define MBEDTLS_SSL_TLS_C$' "$CONFIG"; then
    sed -i '/^#define MBEDTLS_SSL_TLS_C$/a #define MBEDTLS_SSL_KEYING_MATERIAL_EXPORT' "$CONFIG"
  else
    printf '\n#define MBEDTLS_SSL_KEYING_MATERIAL_EXPORT\n' >> "$CONFIG"
  fi
fi

if ! grep -q '^#define MBEDTLS_ENTROPY_HARDWARE_ALT$' "$CONFIG"; then
  sed -i '/^#define MBEDTLS_ENTROPY_C$/a #define MBEDTLS_ENTROPY_HARDWARE_ALT' "$CONFIG"
  sed -i '/^#define MBEDTLS_ENTROPY_HARDWARE_ALT$/a #define MBEDTLS_NO_PLATFORM_ENTROPY' "$CONFIG"
  sed -i '/^#define MBEDTLS_NO_PLATFORM_ENTROPY$/a #define MBEDTLS_PLATFORM_MS_TIME_ALT' "$CONFIG"
fi
# libdatachannel supplies DTLS retransmission timers via std::chrono callbacks.
sed -i '/^#define MBEDTLS_TIMING_C$/d' "$CONFIG"
# libdatachannel uses its own socket layer; Mbed TLS must not compile POSIX net_sockets.
sed -i '/^#define MBEDTLS_NET_C$/d' "$CONFIG"
