#!/bin/sh
set -eu
CONFIG=${1:-vendor/mbedtls/include/mbedtls/mbedtls_config.h}

# libdatachannel requires DTLS-SRTP negotiation and DTLS key material export.
if ! grep -q '^#define MBEDTLS_SSL_DTLS_SRTP$' "$CONFIG"; then
  if grep -q '^#define MBEDTLS_SSL_PROTO_DTLS$' "$CONFIG"; then
    sed -i '/^#define MBEDTLS_SSL_PROTO_DTLS$/a #define MBEDTLS_SSL_DTLS_SRTP' "$CONFIG"
  else
    echo "Mbed TLS DTLS support is required for libdatachannel SRTP" >&2
    exit 1
  fi
fi

if ! grep -q '^#define MBEDTLS_SSL_KEYING_MATERIAL_EXPORT$' "$CONFIG"; then
  if grep -q '^#define MBEDTLS_SSL_TLS_C$' "$CONFIG"; then
    sed -i '/^#define MBEDTLS_SSL_TLS_C$/a #define MBEDTLS_SSL_KEYING_MATERIAL_EXPORT' "$CONFIG"
  else
    echo "Mbed TLS TLS implementation is required for key export" >&2
    exit 1
  fi
fi

# libdatachannel imports PSA-generated keys into mbedtls_pk_context.
if ! grep -q '^#define MBEDTLS_PSA_CRYPTO_CLIENT$' "$CONFIG"; then
  if grep -q '^#define MBEDTLS_PSA_CRYPTO_C$' "$CONFIG"; then
    sed -i '/^#define MBEDTLS_PSA_CRYPTO_C$/a #define MBEDTLS_PSA_CRYPTO_CLIENT' "$CONFIG"
  else
    echo "Mbed TLS PSA crypto support is required by libdatachannel" >&2
    exit 1
  fi
fi

# PS3 supplies hardware entropy through the platform-specific entropy alt.
if ! grep -q '^#define MBEDTLS_ENTROPY_HARDWARE_ALT$' "$CONFIG"; then
  if grep -q '^#define MBEDTLS_ENTROPY_C$' "$CONFIG"; then
    sed -i '/^#define MBEDTLS_ENTROPY_C$/a #define MBEDTLS_ENTROPY_HARDWARE_ALT' "$CONFIG"
    sed -i '/^#define MBEDTLS_ENTROPY_HARDWARE_ALT$/a #define MBEDTLS_NO_PLATFORM_ENTROPY' "$CONFIG"
    sed -i '/^#define MBEDTLS_NO_PLATFORM_ENTROPY$/a #define MBEDTLS_PLATFORM_MS_TIME_ALT' "$CONFIG"
  else
    echo "Mbed TLS entropy module is required" >&2
    exit 1
  fi
fi

# libdatachannel supplies retransmission timing; it uses its own socket layer.
sed -i '/^#define MBEDTLS_TIMING_C$/d' "$CONFIG"
sed -i '/^#define MBEDTLS_NET_C$/d' "$CONFIG"

grep -q '^#define MBEDTLS_SSL_DTLS_SRTP$' "$CONFIG"
grep -q '^#define MBEDTLS_SSL_KEYING_MATERIAL_EXPORT$' "$CONFIG"
grep -q '^#define MBEDTLS_PSA_CRYPTO_CLIENT$' "$CONFIG"
