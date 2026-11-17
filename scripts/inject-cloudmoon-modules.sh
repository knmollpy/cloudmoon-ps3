#!/bin/sh
set -eu
ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
PS3_BASE=${1:-"$ROOT_DIR/vendor/PS3-Moonlight"}
SRC="$ROOT_DIR/src"
DST="$PS3_BASE/src"
if [ ! -f "$PS3_BASE/Makefile" ]; then
  echo "PS3-Moonlight submodule missing. Run: git submodule update --init --recursive" >&2
  exit 1
fi
for file in \
  cloudmoon_input.c cloudmoon_input.h \
  cloudmoon_client.c cloudmoon_client.h \
  cloudmoon_discovery.c cloudmoon_discovery.h \
  cloudmoon_protocol.c cloudmoon_protocol.h \
  cloudmoon_session.c cloudmoon_session.h \
  cloudmoon_ws.c cloudmoon_ws.h \
  cloudmoon_upgrade.c cloudmoon_upgrade.h \
  cloudmoon_random.c cloudmoon_random.h \
  cloudmoon_ps3_random.c cloudmoon_ps3_random.h \
  cloudmoon_tls_polarssl.c cloudmoon_tls_polarssl.h \
  cloudmoon_ws_tls.c cloudmoon_ws_tls.h \
  cloudmoon_mbedtls_entropy.c
do
  cp "$SRC/$file" "$DST/$file"
done
if ! grep -q 'src/cloudmoon_client.c' "$PS3_BASE/Makefile"; then
  sed -i 's#src/spu_opus_bridge.c#src/spu_opus_bridge.c src/cloudmoon_input.c src/cloudmoon_client.c src/cloudmoon_discovery.c src/cloudmoon_protocol.c src/cloudmoon_session.c src/cloudmoon_ws.c src/cloudmoon_upgrade.c src/cloudmoon_random.c src/cloudmoon_ps3_random.c src/cloudmoon_tls_polarssl.c src/cloudmoon_ws_tls.c#' "$PS3_BASE/Makefile"
fi

if [ "${CLOUDMOON_WEBRTC:-0}" = "1" ]; then
  for file in cloudmoon_webrtc.c cloudmoon_webrtc.h cloudmoon_launch.c cloudmoon_launch.h cloudmoon_clock_compat.c cloudmoon_clock_compat.h cloudmoon_time_compat.c cloudmoon_time_compat.h; do cp "$SRC/$file" "$DST/$file"; done
  # Expose L2/R2 from the existing PS3 pad poller to CloudMoon.
  if ! grep -q 'l2, r2;' "$DST/input.h"; then
    awk '1; /short ly, lx, ry, rx;/ { print "    unsigned char l2, r2;" }' "$DST/input.h" > "$DST/input.h.tmp"
    mv "$DST/input.h.tmp" "$DST/input.h"
  fi
  if ! grep -q 'g_pad_state.l2' "$DST/input.c"; then
    awk '1; /g_pad_state.ry = rightStickY;/ { print "                    g_pad_state.l2 = paddata.BTN_L2;"; print "                    g_pad_state.r2 = paddata.BTN_R2;" }' "$DST/input.c" > "$DST/input.c.tmp"
    mv "$DST/input.c.tmp" "$DST/input.c"
  fi
  if ! grep -q 'ps3video_cloudmoon_start' "$DST/video.c"; then cat "$SRC/cloudmoon_video_bridge.inc" >> "$DST/video.c"; fi
  if ! grep -q 'ps3audio_cloudmoon_start' "$DST/audio.c"; then cat "$SRC/cloudmoon_audio_bridge.inc" >> "$DST/audio.c"; fi
  if ! grep -q 'ps3video_cloudmoon_start' "$DST/video.h"; then
    sed -i '/#include <stdint.h>/a #include <stddef.h>' "$DST/video.h"
    sed -i '/void ps3video_start();/a int ps3video_cloudmoon_start(int width, int height);\nvoid ps3video_cloudmoon_stop(void);\nint ps3video_submit_h264(const uint8_t *data, size_t length, uint32_t timestamp);' "$DST/video.h"
  fi
  if ! grep -q 'ps3audio_cloudmoon_start' "$DST/audio.h"; then
    sed -i '/#include <Limelight.h>/a #include <stddef.h>\n#include <stdint.h>' "$DST/audio.h"
    sed -i '/void ps3audio_start();/a int ps3audio_cloudmoon_start(void);\nvoid ps3audio_submit_opus(const uint8_t *data, size_t length);\nvoid ps3audio_cloudmoon_stop(void);' "$DST/audio.h"
  fi
  # Remove the obsolete empty-interface shim from checkouts patched by older revisions.
  sed -i 's# src/cloudmoon_ifaddrs_stub.c##g' "$PS3_BASE/Makefile"
  # The legacy Moonlight compatibility header aliases Mbed TLS names to PolarSSL.
# The hardware entropy shim must include the real Mbed TLS 3.x headers instead.
python3 - "$PS3_BASE/src/openssl_compat.h" <<'PY'
import pathlib, sys
p = pathlib.Path(sys.argv[1])
s = p.read_text()
guard = "#ifndef CLOUDMOON_MBEDTLS_ENTROPY"
if guard not in s:
    first = "#define OPENSSL_COMPAT_H"
    if first not in s:
        raise SystemExit("Cannot guard the legacy OpenSSL compatibility header")
    s = s.replace(first, first + "\\n" + guard, 1)
    pos = s.rfind("#endif")
    if pos < 0:
        raise SystemExit("Cannot close legacy OpenSSL compatibility guard")
    s = s[:pos] + "#endif /* CLOUDMOON_MBEDTLS_ENTROPY */\\n" + s[pos:]

# The compatibility wrappers must not export real Mbed TLS 3.x symbol names:
# libdatachannel links the actual Mbed TLS implementation in the same PKG.
renames = """/* PolarSSL-backed compatibility functions are deliberately namespaced. */
#define mbedtls_entropy_init cm_polarssl_entropy_init
#define mbedtls_ctr_drbg_init cm_polarssl_ctr_drbg_init
#define mbedtls_ctr_drbg_seed cm_polarssl_ctr_drbg_seed
#define mbedtls_ctr_drbg_random cm_polarssl_ctr_drbg_random
#define mbedtls_entropy_func cm_polarssl_entropy_func
#define mbedtls_cipher_init cm_polarssl_cipher_init
#define mbedtls_cipher_free cm_polarssl_cipher_free
#define mbedtls_cipher_setup cm_polarssl_cipher_setup
#define mbedtls_cipher_setkey cm_polarssl_cipher_setkey
#define mbedtls_cipher_set_iv cm_polarssl_cipher_set_iv
#define mbedtls_cipher_reset cm_polarssl_cipher_reset
#define mbedtls_cipher_update cm_polarssl_cipher_update
#define mbedtls_cipher_finish cm_polarssl_cipher_finish
#define mbedtls_cipher_auth_encrypt cm_polarssl_cipher_auth_encrypt
#define mbedtls_cipher_auth_decrypt cm_polarssl_cipher_auth_decrypt
#define mbedtls_cipher_auth_encrypt_ext cm_polarssl_cipher_auth_encrypt_ext
#define mbedtls_cipher_auth_decrypt_ext cm_polarssl_cipher_auth_decrypt_ext
#define mbedtls_cipher_info_from_values cm_polarssl_cipher_info_from_values
"""
if "cm_polarssl_entropy_init" not in s:
    if guard not in s:
        raise SystemExit("Legacy Mbed TLS compatibility guard missing")
    s = s.replace(guard, guard + "\\n" + renames, 1)
p.write_text(s)
PY

if ! grep -q 'src/cloudmoon_webrtc.c' "$PS3_BASE/Makefile"; then
    sed -i 's#src/cloudmoon_ws_tls.c#src/cloudmoon_ws_tls.c src/cloudmoon_mbedtls_entropy.c src/cloudmoon_clock_compat.c src/cloudmoon_time_compat.c src/cloudmoon_webrtc.c src/cloudmoon_launch.c#' "$PS3_BASE/Makefile"
    # Put the freshly built Mbed TLS headers before PSL1GHT's older bundled headers.
    sed -i 's#COMMON_INCLUDES[[:space:]]*:=[[:space:]]*#COMMON_INCLUDES := -I'"$ROOT_DIR"'/build/ps3deps/include #' "$PS3_BASE/Makefile"
    sed -i 's#-I./src -I./src/spu/include#-I./src -I./src/spu/include -DCM_ENABLE_WEBRTC -DRTC_STATIC#' "$PS3_BASE/Makefile"
    # Only the Mbed TLS entropy object opts out of the legacy PolarSSL aliases.
    sed -i '/$(BUILD)\\/obj\\/%.o: src\\/%.c/i $(BUILD)/obj/cloudmoon_mbedtls_entropy.o: CFLAGS += -DCLOUDMOON_MBEDTLS_ENTROPY' "$PS3_BASE/Makefile"
    sed -i 's#-lpolarssl#-lpolarssl -L'"$ROOT_DIR"'/build/ps3deps/lib -ldatachannel -lsrtp2 -lusrsctp -ljuice -lmbedtls -lmbedx509 -lmbedcrypto -lpthread -lstdc++#g' "$PS3_BASE/Makefile"
  fi
  if ! grep -q 'cloudmoon_autostart' "$DST/main.c"; then
    sed -i '/#include "input.h"/a #include "cloudmoon_launch.h"' "$DST/main.c"
    sed -i '/ps3input_start();/a\  cm_cloudmoon_autostart();' "$DST/main.c"
  fi
  # Ship a real CA bundle inside the PKG so TLS certificate verification works.
  if ! grep -q 'cacert.pem' "$PS3_BASE/Makefile"; then
    sed -i '/mkdir -p $(BUILD)\/pkg\/USRDIR/a\	cp '"$ROOT_DIR"'/assets/cacert.pem $(BUILD)/pkg/USRDIR/cacert.pem' "$PS3_BASE/Makefile"
  fi
fi
echo "CloudMoon transport modules integrated."
if [ "${CLOUDMOON_WEBRTC:-0}" = "1" ]; then
  echo "WebRTC, DualShock input and H.264/Opus PS3 decoder bridges integrated."
fi
