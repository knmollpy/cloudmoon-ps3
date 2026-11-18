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
  cloudmoon_link.c cloudmoon_link.h cloudmoon_catalog.c cloudmoon_catalog.h \
  cloudmoon_pairing.c cloudmoon_pairing.h qrcodegen.c qrcodegen.h \
  cloudmoon_video_limits.c cloudmoon_video_limits.h \
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
  sed -i 's#src/spu_opus_bridge.c#src/spu_opus_bridge.c src/cloudmoon_input.c src/cloudmoon_link.c src/cloudmoon_catalog.c src/qrcodegen.c src/cloudmoon_pairing.c src/cloudmoon_video_limits.c src/cloudmoon_client.c src/cloudmoon_discovery.c src/cloudmoon_protocol.c src/cloudmoon_session.c src/cloudmoon_ws.c src/cloudmoon_upgrade.c src/cloudmoon_random.c src/cloudmoon_ps3_random.c src/cloudmoon_tls_polarssl.c src/cloudmoon_ws_tls.c#' "$PS3_BASE/Makefile"
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
    s = s.replace(first, first + "\n" + guard, 1)
    pos = s.rfind("#endif")
    if pos < 0:
        raise SystemExit("Cannot close legacy OpenSSL compatibility guard")
    s = s[:pos] + "#endif /* CLOUDMOON_MBEDTLS_ENTROPY */\n" + s[pos:]

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
    s = s.replace(guard, guard + "\n" + renames, 1)
p.write_text(s)
PY

# Keep the Moonlight-common-C compatibility crypto on PolarSSL, while
# libdatachannel uses the real Mbed TLS 3.x symbols from build/ps3deps.
python3 - "$PS3_BASE/third_party/moonlight-common-c/src/PlatformCrypto.h" "$PS3_BASE/third_party/moonlight-common-c/src/PlatformCrypto.c" <<'PY'
import pathlib, sys
header = pathlib.Path(sys.argv[1])
source = pathlib.Path(sys.argv[2])

h = header.read_text()
h = h.replace("#include <mbedtls/cipher.h>", "#include <polarssl/cipher.h>")
h = h.replace("mbedtls_cipher_context_t ctx;", "cipher_context_t ctx;")
if "#include <polarssl/cipher.h>" not in h or "mbedtls_cipher_context_t ctx;" in h:
    raise SystemExit("Could not isolate Moonlight crypto context from Mbed TLS")
header.write_text(h)

c = source.read_text()
if '#include "../../../src/openssl_compat.h"' not in c:
    c = c.replace('#include "Limelight-internal.h"',
                  '#include "Limelight-internal.h"\n#include "../../../src/openssl_compat.h"', 1)
for inc in ('#include <mbedtls/entropy.h>\n',
            '#include <mbedtls/ctr_drbg.h>\n',
            '#include <mbedtls/version.h>\n'):
    c = c.replace(inc, '')
if "#define MBEDTLS_VERSION_MAJOR 3" not in c:
    c = c.replace("#ifdef USE_MBEDTLS",
                  "#ifdef USE_MBEDTLS\n#define MBEDTLS_VERSION_MAJOR 3\n#define MBEDTLS_VERSION_MINOR 0", 1)
if '#include "../../../src/openssl_compat.h"' not in c or "MBEDTLS_VERSION_MAJOR 3" not in c:
    raise SystemExit("Could not route Moonlight crypto through PolarSSL wrappers")
source.write_text(c)
PY

if ! grep -q 'src/cloudmoon_webrtc.c' "$PS3_BASE/Makefile"; then
    sed -i 's#src/cloudmoon_ws_tls.c#src/cloudmoon_ws_tls.c src/cloudmoon_mbedtls_entropy.c src/cloudmoon_clock_compat.c src/cloudmoon_time_compat.c src/cloudmoon_webrtc.c src/cloudmoon_launch.c#' "$PS3_BASE/Makefile"
    # Prepend the freshly built Mbed TLS headers but preserve all upstream SDK/Freetype includes.
    python3 - "$PS3_BASE/Makefile" "$ROOT_DIR/build/ps3deps/include" <<'PY'
import pathlib, sys
p = pathlib.Path(sys.argv[1])
include = sys.argv[2]
lines = p.read_text().splitlines()
assignment = "COMMON_INCLUDES := -D__PS3__ -I" + include + " $(COMMON_INCLUDES)"
if assignment not in lines:
    for i, line in enumerate(lines):
        if line.startswith("COMMON_INCLUDES") and ":=" in line:
            lines.insert(i + 1, assignment)
            break
    else:
        raise SystemExit("Could not locate upstream COMMON_INCLUDES assignment")
p.write_text("\n".join(lines) + "\n")
PY
    sed -i 's#-I./src -I./src/spu/include#-I./src -I./src/spu/include -DCM_ENABLE_WEBRTC -DRTC_STATIC#' "$PS3_BASE/Makefile"
    # Prefer the just-built Mbed TLS 3.x archives over older SDK portlibs.
    sed -i 's#-L./third_party/moonlight-common-c#-L'"$ROOT_DIR"'/build/ps3deps/lib -L./third_party/moonlight-common-c#' "$PS3_BASE/Makefile"
    # Only the Mbed TLS entropy object opts out of the legacy PolarSSL aliases.
    sed -i '/$(BUILD)\/obj\/%.o: src\/%.c/i $(BUILD)/obj/cloudmoon_mbedtls_entropy.o: CFLAGS += -DCLOUDMOON_MBEDTLS_ENTROPY' "$PS3_BASE/Makefile"
    sed -i 's#-lpolarssl#-lpolarssl -L'"$ROOT_DIR"'/build/ps3deps/lib -ldatachannel -lsrtp2 -lusrsctp -ljuice -lmbedtls -lmbedx509 -lmbedcrypto -lpthread -lstdc++#g' "$PS3_BASE/Makefile"
  fi
  if ! grep -q 'cloudmoon_autostart' "$DST/main.c"; then
    sed -i '/#include "input.h"/a #include "cloudmoon_launch.h"' "$DST/main.c"
    sed -i '/#include "cloudmoon_launch.h"/a #include "cloudmoon_pairing.h"' "$DST/main.c"
  fi
  if ! grep -q '#include "cloudmoon_catalog.h"' "$DST/main.c"; then
    sed -i '/#include "cloudmoon_pairing.h"/a #include "cloudmoon_catalog.h"' "$DST/main.c"
  fi
  sed -i 's/#define MAX_APP_ENTRIES 32/#define MAX_APP_ENTRIES 48/' "$DST/handshake.h"
  python3 "$ROOT_DIR/scripts/generate-cloudmoon-art.py" "$ROOT_DIR/assets"
  # Existing user artwork may have been saved at a preview size (for example 128x70).
  # Normalize it to the PS3 XMB ICON0 contract before staging the PKG.
  python3 "$ROOT_DIR/scripts/resize-ps3-icon.py" "$ROOT_DIR/assets/ICON0.PNG"
  if [ -s "$ROOT_DIR/assets/ICON0.PNG" ]; then
    cp "$ROOT_DIR/assets/ICON0.PNG" "$PS3_BASE/ICON0.PNG"
  else
    echo "CloudMoon ICON0.PNG missing from assets/" >&2
    exit 1
  fi
  if [ -s "$ROOT_DIR/assets/PIC1.PNG" ]; then
    cp "$ROOT_DIR/assets/PIC1.PNG" "$PS3_BASE/PIC1.PNG"
    if ! grep -q 'PIC1.PNG' "$PS3_BASE/Makefile"; then
      sed -i '/cp $(ICON0) $(BUILD)\/pkg\/ICON0.PNG/a\	cp PIC1.PNG $(BUILD)/pkg/PIC1.PNG' "$PS3_BASE/Makefile"
    fi
  else
    echo "CloudMoon PIC1.PNG missing from assets/; package preview will use no custom background." >&2
  fi
  python3 - "$DST/main.c" "$DST/ui.c" "$DST/ui.h" <<'PY'
import pathlib
import sys
p = pathlib.Path(sys.argv[1])
s = p.read_text()
# Preserve upstream ps3_net_init() and ps3_net_shutdown() exactly.
# The video API only reports selected standard resolution IDs; never use unchecked
# videoGetResolution output as a render surface size. Keep a fixed 720p logical
# canvas and let Tiny3D scale it into the GameOS-selected physical output mode.
old_display = """  // Çözünürlük Tespiti
  videoState vstate;
  videoResolution vres;
  videoGetState(VIDEO_PRIMARY, 0, &vstate);
  videoGetResolution(vstate.displayMode.resolution, &vres);

  int width = vres.width;
  int height = vres.height;

  // Eğer çözünürlük tanımsızsa varsayılan 720p kullan
  if (width == 0 || height == 0) {
    width = 1280;
    height = 720;
  }

  int is_4_3 = 0;
  if (vstate.displayMode.aspect == VIDEO_ASPECT_4_3) {
    is_4_3 = 1;
  } else if (vstate.displayMode.aspect == VIDEO_ASPECT_16_9) {
    is_4_3 = 0;
  } else if (height > 0 && ((float)width / (float)height) < 1.5f) {
    is_4_3 = 1;
  }

  NLOG("Detected Display: %dx%d (%s)", width, height, is_4_3 ? "4:3 Standard/CRT" : "16:9 Widescreen");

  ui_init(width, height, is_4_3);
"""
new_display = """  /* CLOUDMOON_STOCK_DISPLAY_QUERY */
  /* Query the GameOS-selected mode without changing the HDMI output. */
  videoState vstate = {0};
  videoResolution vres = {0};
  int video_state_rc = videoGetState(VIDEO_PRIMARY, 0, &vstate);
  int video_resolution_rc = -1;
  int width = 1280;
  int height = 720;
  int is_4_3 = 0;

  if (video_state_rc == 0 && vstate.state == VIDEO_STATE_ENABLED) {
    video_resolution_rc = videoGetResolution(vstate.displayMode.resolution, &vres);
    if (video_resolution_rc == 0 && vres.width >= 320 && vres.height >= 200) {
      width = (int)vres.width;
      height = (int)vres.height;
    }
    if (vstate.displayMode.aspect == VIDEO_ASPECT_4_3) is_4_3 = 1;
    else if (vstate.displayMode.aspect == VIDEO_ASPECT_16_9) is_4_3 = 0;
  }

  NLOG("CloudMoon display query=%d resolution=%d -> %dx%d aspect=%s",
       video_state_rc, video_resolution_rc, width, height,
       is_4_3 ? "4:3" : "16:9");
  ui_init(width, height, is_4_3);
"""
if old_display not in s:
    raise SystemExit("Could not match original unsafe display detection block")
s = s.replace(old_display, new_display, 1)
needle = "  ui_init(width, height, is_4_3);\n  ps3input_start();\n"
replacement_main = """  ui_init(width, height, is_4_3);
  ps3input_start();
  NLOG("CloudMoon PS3 UI Initialized");
  NLOG("CloudMoon: waiting for Connect from the PS3 menu");
"""
if needle not in s:
    raise SystemExit("Could not locate startup sequence after safe display patch")
s = s.replace(needle, replacement_main, 1)

if needle not in s:
    raise SystemExit("Could not locate UI startup sequence")
# Preserve the upstream network-init call, but capture its result so the
# runtime log reports whether the PS3 network pool actually initialized.
# Defer network initialization until the user opens Link or Connect.
# The menu can now start without loading sys_net or allocating its network pool.
if "int net_rc = ps3_net_init();" not in s:
    if s.count("  ps3_net_init();") != 1:
        raise SystemExit("Could not uniquely locate the original network initialization call")
    s = s.replace("  ps3_net_init();", "  /* CloudMoon initializes sys_net lazily from the menu. */", 1)
s = s.replace('  NLOG("CloudMoon: network initialization result %d", net_rc);\n', "", 1)

replacement_main = """  ui_init(width, height, is_4_3);
  ps3input_start();
  NLOG("CloudMoon PS3 UI Initialized");
  NLOG("CloudMoon: waiting for Connect from the PS3 menu");
"""
s = s.replace(needle, replacement_main, 1)
# CloudMoon replaces Moonlight's blocking pairing/stream state machine.
# Do not leave the legacy Sunshine handshake ahead of the CloudMoon Connect action.
main_loop_patch = """  int cloudmoon_connect_started = 0;
  int cloudmoon_link_started = 0;
  int cloudmoon_net_initialized = 0;
  uint8_t cloudmoon_pair_qr[CM_PAIR_QR_MAX_MODULES];
  char cloudmoon_pair_url[CM_PAIR_URL_CAP];
  int cloudmoon_pair_qr_size = 0;
  while (ui_is_running()) {
    sysUtilCheckCallback();
    if (ui_get_state() == UI_STATE_LINKING) {
      if (!cloudmoon_net_initialized) {
        int net_rc = ps3_net_init();
        NLOG("CloudMoon: network initialization result %d", net_rc);
        if (net_rc != 0) {
          ui_push_log("CloudMoon: PS3 network initialization failed.");
          ui_set_state(UI_STATE_ERROR);
          ps3perf_tick(5);
          usleep(50000);
          continue;
        }
        cloudmoon_net_initialized = 1;
      }
      if (!cloudmoon_link_started) {
        if (cm_cloudmoon_pair_start((cm_cloudmoon_catalog_selected() ? cm_cloudmoon_catalog_selected()->package_id : NULL), cloudmoon_pair_url, sizeof(cloudmoon_pair_url),
                                    cloudmoon_pair_qr, sizeof(cloudmoon_pair_qr),
                                    &cloudmoon_pair_qr_size) == 0) {
          ui_set_cloudmoon_link_qr(cloudmoon_pair_qr, cloudmoon_pair_qr_size,
                                   cloudmoon_pair_url);
          cloudmoon_link_started = 1;
          NLOG("CloudMoon: phone pairing page ready at %s", cloudmoon_pair_url);
        } else {
          ui_set_state(UI_STATE_ERROR);
        }
      }
      ps3perf_tick(5);
      usleep(20000);
    } else if (ui_get_state() == UI_STATE_PAIRING) {
      if (cloudmoon_link_started) {
        cm_cloudmoon_pair_stop();
        ui_clear_cloudmoon_link_qr();
        cloudmoon_link_started = 0;
      }
      if (!cloudmoon_connect_started) {
        cloudmoon_connect_started = 1;
        if (!cloudmoon_net_initialized) {
          int net_rc = ps3_net_init();
          NLOG("CloudMoon: network initialization result %d", net_rc);
          if (net_rc != 0) {
            ui_push_log("CloudMoon: PS3 network initialization failed.");
            ui_set_state(UI_STATE_ERROR);
            cloudmoon_connect_started = 0;
            ps3perf_tick(5);
            usleep(50000);
            continue;
          }
          cloudmoon_net_initialized = 1;
        }
        NLOG("CloudMoon: Connect to CloudMoon selected; starting coordinator session.");
        cm_cloudmoon_autostart();
      }
      ps3perf_tick(5);
      usleep(10000);
    } else if (ui_get_state() == UI_STATE_STREAMING) {
      ps3_pad_state_t pad;
      ps3input_get_data(&pad);
      int combo = PLAY_FLAG | BACK_FLAG | LS_CLK_FLAG | RS_CLK_FLAG;
      if ((pad.buttons_down & combo) == combo) {
        cm_cloudmoon_stop();
        ui_set_state(UI_STATE_IP_ENTRY);
      }
      vdec_poll();
      ps3perf_tick(5);
      usleep(1000);
    } else {
      cloudmoon_connect_started = 0;
      if (cloudmoon_link_started) {
        cm_cloudmoon_pair_stop();
        ui_clear_cloudmoon_link_qr();
        cloudmoon_link_started = 0;
      }
      if (cm_cloudmoon_is_running()) {
        NLOG("CloudMoon: menu cancellation requested; stopping active session.");
        cm_cloudmoon_stop();
      }
      if (cloudmoon_net_initialized) {
        ps3_net_shutdown();
        cloudmoon_net_initialized = 0;
      }
      ps3perf_tick(5);
      usleep(50000);
    }
  }

"""
if "CloudMoon complete main-loop replacement" not in s:
    anchor_start = "  while (ui_is_running()) {"
    anchor_end = '  NLOG("Terminating Moonlight PS3 application...");'
    if s.count(anchor_start) != 1:
        raise SystemExit("Could not uniquely locate the original main loop")
    start_loop = s.find(anchor_start)
    end_loop = s.find(anchor_end, start_loop)
    if end_loop < 0:
        raise SystemExit("Could not locate application termination after main loop")
    s = s[:start_loop] + main_loop_patch + "  /* CloudMoon complete main-loop replacement */\n\n" + s[end_loop:]
    s = s.replace(anchor_end, anchor_end + "\n  cm_cloudmoon_stop();\n  cm_cloudmoon_pair_stop();", 1)

# Protect only the final main() shutdown. The loop's guarded shutdown is
# already part of main_loop_patch and must not be rewritten or nested.
old_exit = "  ps3_net_shutdown();\n  return 0;"
new_exit = "  if (cloudmoon_net_initialized) {\n    ps3_net_shutdown();\n    cloudmoon_net_initialized = 0;\n  }\n  return 0;"
if s.count(old_exit) != 1:
    raise SystemExit("Could not uniquely locate final main() network shutdown")
s = s.replace(old_exit, new_exit, 1)

p.write_text(s)

ui = pathlib.Path(sys.argv[2])
u = ui.read_text()
# Preserve upstream Tiny3D setup and direct-display viewport.
if "tiny3d_Init(1024 * 1024); // 1MB vertex buffer" not in u:
    raise SystemExit("Upstream Tiny3D initialization call is missing")
if "tiny3d_UserViewportSurface(1, (float)ui_width, (float)ui_height);" not in u:
    raise SystemExit("Upstream Tiny3D display viewport call is missing")
if "sysThreadCreate(&ui_thread, ui_loop, 0, 500, 0x10000," not in u:
    raise SystemExit("Unexpected UI thread stack configuration")
ui.write_text(u)
PY
  # Ship a real CA bundle inside the PKG so TLS certificate verification works.
  if ! grep -q 'cacert.pem' "$PS3_BASE/Makefile"; then
    sed -i '/mkdir -p $(BUILD)\/pkg\/USRDIR/a\	cp '"$ROOT_DIR"'/assets/cacert.pem $(BUILD)/pkg/USRDIR/cacert.pem' "$PS3_BASE/Makefile"
  fi
  if ! grep -q 'cloudmoon-mobile-link.html' "$PS3_BASE/Makefile"; then
    sed -i '/mkdir -p $(BUILD)\/pkg\/USRDIR/a\	cp '"$ROOT_DIR"'/docs/cloudmoon-mobile-link.html $(BUILD)/pkg/USRDIR/cloudmoon-mobile-link.html' "$PS3_BASE/Makefile"
  fi
fi

# Use the unmodified Tiny3D library from the upstream PS3 SDK.

# Apply the same GameOS display-mode query to the no-WebRTC diagnostic build.
# Both variants retain upstream Tiny3D initialization and viewport code.
python3 - "$DST/main.c" "$DST/ui.c" <<'PY'
import pathlib
import sys

main_path, ui_path = map(pathlib.Path, sys.argv[1:])
main = main_path.read_text()
ui = ui_path.read_text()

safe_main = """  /* Query the GameOS-selected mode without changing the HDMI output. */
  videoState vstate = {0};
  videoResolution vres = {0};
  int video_state_rc = videoGetState(VIDEO_PRIMARY, 0, &vstate);
  int video_resolution_rc = -1;
  int width = 1280;
  int height = 720;
  int is_4_3 = 0;

  if (video_state_rc == 0 && vstate.state == VIDEO_STATE_ENABLED) {
    video_resolution_rc = videoGetResolution(vstate.displayMode.resolution, &vres);
    if (video_resolution_rc == 0 && vres.width >= 320 && vres.height >= 200) {
      width = (int)vres.width;
      height = (int)vres.height;
    }
    if (vstate.displayMode.aspect == VIDEO_ASPECT_4_3) is_4_3 = 1;
    else if (vstate.displayMode.aspect == VIDEO_ASPECT_16_9) is_4_3 = 0;
  }

  NLOG("CloudMoon display query=%d resolution=%d -> %dx%d aspect=%s",
       video_state_rc, video_resolution_rc, width, height,
       is_4_3 ? "4:3" : "16:9");
  ui_init(width, height, is_4_3);
"""

if "CLOUDMOON_STOCK_DISPLAY_QUERY" not in main:
    start = main.find("  // Çözünürlük Tespiti")
    end = main.find("  ps3input_start();", start)
    if start < 0 or end < 0:
        raise SystemExit("Could not locate upstream PS3 display detection block")
    main = main[:start] + "  /* CLOUDMOON_STOCK_DISPLAY_QUERY */\n" + safe_main + main[end:]
// No global constructor: file I/O begins only after the PS3 C runtime enters main().
if '#include <stdio.h>' not in main:
    include_anchor = '#include "input.h"'
    if include_anchor not in main:
        raise SystemExit("Could not locate main include for boot logger")
    main = main.replace(include_anchor, include_anchor + '\n#include <stdio.h>', 1)
logger_code = r"""static void cm_boot_log_write(const char *message) {
    FILE *f = fopen("/dev_hdd0/game/MNLT00001/USRDIR/cloudmoon_boot.log", "a");
    if (f) {
        fprintf(f, "%s\n", message ? message : "(null)");
        fclose(f);
    }
}
void cm_boot_log(const char *message) { cm_boot_log_write(message); }

"""
if "void cm_boot_log(const char *message)" not in main:
    anchor = '#include <stdio.h>'
    main = main.replace(anchor, anchor + '\n' + logger_code, 1)
main_start = main.find("int main(")
if main_start < 0:
    raise SystemExit("Could not locate main() for explicit boot logging")
main_head, main_body = main[:main_start], main[main_start:]
if 'cm_boot_log("1 main")' not in main_body:
    brace = main_body.find("{")
    if brace < 0:
        raise SystemExit("Could not locate main() opening brace")
    main_body = main_body[:brace + 1] + '\n    cm_boot_log("1 main");' + main_body[brace + 1:]
main_body = main_body.replace('ui_init(width, height, is_4_3);',
                              'ui_init(width, height, is_4_3);\n    cm_boot_log("2 ui_init complete");', 1)
main_body = main_body.replace('ps3input_start();',
                              'ps3input_start();\n    cm_boot_log("3 ps3input_start complete");', 1)
main = main_head + main_body
main_path.write_text(main)

# Tiny3D is deliberately untouched to match the upstream, working UI path.
if "    tiny3d_Init(1024 * 1024); // 1MB vertex buffer" not in ui:
    raise SystemExit("Upstream Tiny3D initialization call is missing")
if "        tiny3d_UserViewportSurface(1, (float)ui_width, (float)ui_height);" not in ui:
    raise SystemExit("Upstream Tiny3D direct-display viewport is missing")
if "sysThreadCreate(&ui_thread, ui_loop, 0, 500, 0x10000," not in ui:
    raise SystemExit("Upstream UI thread stack is missing")

ui_path.write_text(ui)
PY

# Align the inherited Moonlight screen with the CloudMoon session workflow.
# Keep the stream settings page, but remove the obsolete Sunshine host-IP action.
python3 - "$DST/ui.c" "$DST/ui.h" <<'PY'
import os
import pathlib
import sys

webrtc_enabled = 1 if os.environ.get("CLOUDMOON_WEBRTC") == "1" else 0
p = pathlib.Path(sys.argv[1])
s = p.read_text()
header_path = pathlib.Path(sys.argv[2])
h = header_path.read_text()
if "CLOUDMOON_PS3_MENU_PATCH" in s:
    raise SystemExit(0)

s = s.replace("#define UI_CLOUDMOON_QR_MAX 3249",
              f"#define CLOUDMOON_WEBRTC_ENABLED {webrtc_enabled}\n#define UI_CLOUDMOON_QR_MAX 3249", 1)

h = h.replace("    UI_STATE_ERROR\n};",
              "    UI_STATE_ERROR,\n    UI_STATE_LINKING,\n    UI_STATE_LINKED\n};")
if "ui_set_cloudmoon_link_qr" not in h:
    h = h.replace("void ui_init(int width, int height, int is_4_3);",
                  "void ui_set_cloudmoon_link_qr(const uint8_t *modules, int size, const char *url);\nvoid ui_clear_cloudmoon_link_qr(void);\nvoid ui_init(int width, int height, int is_4_3);")
header_path.write_text(h)

if '#include "cloudmoon_catalog.h"' not in s:
    s = s.replace('#include "input.h"', '#include "input.h"\n#include "cloudmoon_catalog.h"', 1)
state_anchor = "static volatile int ui_state = UI_STATE_IP_ENTRY;"
qr_globals = """#define UI_CLOUDMOON_QR_MAX 3249
static int cloudmoon_catalog_mode = 0;
static uint8_t ui_cloudmoon_qr[UI_CLOUDMOON_QR_MAX];
static int ui_cloudmoon_qr_size = 0;
static volatile int ui_cloudmoon_qr_ready = 0;
static char ui_cloudmoon_pair_url[160];
"""
if "UI_CLOUDMOON_QR_MAX" not in s:
    if state_anchor not in s:
        raise SystemExit("Could not locate UI state for QR data")
    s = s.replace(state_anchor, state_anchor + "\n" + qr_globals, 1)

setter_anchor = "void ui_set_state(int state) { ui_state = state; }"
qr_setters = """void ui_set_cloudmoon_link_qr(const uint8_t *modules, int size, const char *url) {
    size_t count;
    if (!modules || size <= 0 || size > 57 || !url) return;
    count = (size_t)size * (size_t)size;
    if (count > sizeof(ui_cloudmoon_qr)) return;
    memcpy(ui_cloudmoon_qr, modules, count);
    strncpy(ui_cloudmoon_pair_url, url, sizeof(ui_cloudmoon_pair_url) - 1);
    ui_cloudmoon_pair_url[sizeof(ui_cloudmoon_pair_url) - 1] = '\\0';
    ui_cloudmoon_qr_size = size;
    __sync_synchronize();
    ui_cloudmoon_qr_ready = 1;
}
void ui_clear_cloudmoon_link_qr(void) {
    ui_cloudmoon_qr_ready = 0;
    ui_cloudmoon_qr_size = 0;
    ui_cloudmoon_pair_url[0] = '\\0';
    memset(ui_cloudmoon_qr, 0, sizeof(ui_cloudmoon_qr));
}
"""
if "ui_set_cloudmoon_link_qr(const uint8_t *modules" not in s:
    if setter_anchor not in s:
        raise SystemExit("Could not locate ui_set_state for QR setters")
    s = s.replace(setter_anchor, setter_anchor + "\n" + qr_setters, 1)

draw_anchor = "// FreeType font engine state"
draw_helper = """static void ui_draw_cloudmoon_pair_qr(void) {
    int x, y, size;
    float cell, total, origin_x, origin_y;
    if (!ui_cloudmoon_qr_ready) return;
    size = ui_cloudmoon_qr_size;
    if (size <= 0 || size > 57) return;

    total = SY(260);
    cell = total / (float)(size + 8);
    origin_x = (float)ui_width - SX(340);
    origin_y = SY(104);

    /* Batch the entire static QR into one QUADS submission instead of
       hundreds of Tiny3D calls on every UI frame. */
    tiny3d_SetPolygon(TINY3D_QUADS);
    tiny3d_VertexPos(origin_x, origin_y, 65535);
    tiny3d_VertexColor(0xffffffff);
    tiny3d_VertexPos(origin_x + total, origin_y, 65535);
    tiny3d_VertexPos(origin_x + total, origin_y + total, 65535);
    tiny3d_VertexPos(origin_x, origin_y + total, 65535);

    for (y = 0; y < size; ++y) {
        x = 0;
        while (x < size) {
            int run_start;
            float left, top, right, bottom;
            while (x < size && !ui_cloudmoon_qr[(size_t)y * (size_t)size + (size_t)x]) ++x;
            if (x >= size) break;
            run_start = x;
            while (x < size && ui_cloudmoon_qr[(size_t)y * (size_t)size + (size_t)x]) ++x;

            left = origin_x + (float)(run_start + 4) * cell;
            top = origin_y + (float)(y + 4) * cell;
            right = origin_x + (float)(x + 4) * cell;
            bottom = top + cell;

            tiny3d_VertexPos(left, top, 65535);
            tiny3d_VertexColor(0xff000000);
            tiny3d_VertexPos(right, top, 65535);
            tiny3d_VertexPos(right, bottom, 65535);
            tiny3d_VertexPos(left, bottom, 65535);
        }
    }
    tiny3d_End();
}

"""
if "ui_draw_cloudmoon_pair_qr" not in s:
    if draw_anchor not in s:
        raise SystemExit("Could not locate QR renderer insertion point")
    s = s.replace(draw_anchor, draw_helper + draw_anchor, 1)

cancel_anchor = """        } else if (ui_state == UI_STATE_PAIRING) {
            // Circle button to cancel pairing attempt
            if (pad.buttons_pressed & B_FLAG) {
                ui_state = UI_STATE_IP_ENTRY;
            }
"""
cancel_replacement = """        } else if (ui_state == UI_STATE_LINKING) {
            if (pad.buttons_pressed & B_FLAG) ui_state = UI_STATE_IP_ENTRY;
        } else if (ui_state == UI_STATE_LINKED) {
            if ((pad.buttons_pressed & A_FLAG) || (pad.buttons_pressed & B_FLAG))
                ui_state = UI_STATE_IP_ENTRY;
        } else if (ui_state == UI_STATE_PAIRING) {
            if (pad.buttons_pressed & B_FLAG) {
                ui_state = UI_STATE_IP_ENTRY;
            }
"""
if cancel_anchor not in s:
    raise SystemExit("Could not locate input pairing handler")
s = s.replace(cancel_anchor, cancel_replacement, 1)

# In the native CloudMoon catalog, selecting an entry starts the QR link flow.
app_list_input_anchor = """        } else if (ui_state == UI_STATE_APPLIST) {
            if (current_app_list.count > 0) {
                if (pad.buttons_pressed & UP_FLAG) {
                    active_app_idx = (active_app_idx + current_app_list.count - 1) % current_app_list.count;
                }
                if (pad.buttons_pressed & DOWN_FLAG) {
                    active_app_idx = (active_app_idx + 1) % current_app_list.count;
                }
                if (pad.buttons_pressed & A_FLAG) {
                    app_selection_confirmed = 1;
                }
            }
            // Circle button returns to main menu
            if (pad.buttons_pressed & B_FLAG) {
                app_selection_confirmed = 0;
                ui_state = UI_STATE_IP_ENTRY;
            }
        }
"""
app_list_input_replacement = """        } else if (ui_state == UI_STATE_APPLIST) {
            if (current_app_list.count > 0) {
                if (pad.buttons_pressed & UP_FLAG)
                    active_app_idx = (active_app_idx + current_app_list.count - 1) % current_app_list.count;
                if (pad.buttons_pressed & DOWN_FLAG)
                    active_app_idx = (active_app_idx + 1) % current_app_list.count;
                if (pad.buttons_pressed & A_FLAG) {
                    if (cloudmoon_catalog_mode &&
                        active_app_idx >= 0 && active_app_idx < current_app_list.count) {
                        int catalog_id = current_app_list.apps[active_app_idx].id;
                        const cm_cloudmoon_game_t *selected = cm_cloudmoon_catalog_at((size_t)catalog_id);
                        if (selected && cm_cloudmoon_catalog_select(catalog_id) == 0) {
#if CLOUDMOON_WEBRTC_ENABLED
                            cloudmoon_catalog_mode = 0;
                            app_selection_confirmed = 0;
                            ui_push_log("CloudMoon: selected game; scan QR to link its active session.");
                            ui_state = UI_STATE_LINKING;
#else
                            ui_push_log("CloudMoon transport is disabled in this safe build; catalogue browsing only.");
#endif
                        }
                    } else {
                        app_selection_confirmed = 1;
                    }
                }
            }
            if (pad.buttons_pressed & B_FLAG) {
                cloudmoon_catalog_mode = 0;
                app_selection_confirmed = 0;
                ui_state = UI_STATE_IP_ENTRY;
            }
        }
"""
if app_list_input_anchor not in s:
    raise SystemExit("Could not locate upstream APPLIST input handler for CloudMoon catalog")
s = s.replace(app_list_input_anchor, app_list_input_replacement, 1)

def replace_once(old, new, label):
    global s
    if old not in s:
        if new in s:
            return
        raise SystemExit("Could not patch UI " + label)
    s = s.replace(old, new, 1)

replace_once("#define MAIN_MENU_ITEM_COUNT 3",
             "#define MAIN_MENU_ITEM_COUNT 4 /* CLOUDMOON_PS3_MENU_PATCH */",
             "main menu item count")
replace_once("static int active_main_item = 0; // 0: Sunshine Host IP, 1: Configure Settings, 2: Connect/Pair",
             "static int active_main_item = 0; // 0: Browse games, 1: Settings, 2: Link, 3: Connect",
             "main menu item mapping")

# Replace the inherited Sunshine host-IP interaction with two real CloudMoon actions.
start = s.find("                // Action handling per main menu item")
end = s.find("                // START button initiates connection", start)
if start < 0 or end < 0:
    raise SystemExit("Could not locate the main-menu action handler")
new_actions = """                // CloudMoon native catalog and session actions.
                if (active_main_item == 0) {
                    if (pad.buttons_pressed & A_FLAG) {
                        ps3_app_list_t catalog_list = {0};
                        size_t catalog_count = cm_cloudmoon_catalog_count();
                        size_t i;
                        if (catalog_count > MAX_APP_ENTRIES) catalog_count = MAX_APP_ENTRIES;
                        for (i = 0; i < catalog_count; ++i) {
                            const cm_cloudmoon_game_t *game = cm_cloudmoon_catalog_at(i);
                            if (!game) continue;
                            catalog_list.apps[i].id = (int)i;
                            strncpy(catalog_list.apps[i].name, game->name,
                                    sizeof(catalog_list.apps[i].name) - 1);
                            catalog_list.apps[i].name[sizeof(catalog_list.apps[i].name) - 1] = '\\0';
                            catalog_list.count++;
                        }
                        if (catalog_list.count > 0) {
                            ui_set_app_list(&catalog_list);
                            cloudmoon_catalog_mode = 1;
                            ui_state = UI_STATE_APPLIST;
                        } else {
                            ui_push_log("CloudMoon: catalog is empty.");
                        }
                    }
                } else if (active_main_item == 1) {
                    if ((pad.buttons_pressed & A_FLAG) || (pad.buttons_pressed & RIGHT_FLAG)) {
                        ui_state = UI_STATE_SETTINGS;
                        active_settings_item = 0;
                    }
                } else if (active_main_item == 2) {
                    if (pad.buttons_pressed & A_FLAG) {
#if CLOUDMOON_WEBRTC_ENABLED
                        ui_state = UI_STATE_LINKING;
#else
                        ui_push_log("Phone linking is unavailable in this safe build.");
#endif
                    }
                } else if (active_main_item == 3) {
                    if (pad.buttons_pressed & A_FLAG) {
#if CLOUDMOON_WEBRTC_ENABLED
                        ui_flush_settings_if_dirty();
                        ui_state = UI_STATE_PAIRING;
#else
                        ui_push_log("CloudMoon streaming transport is not loaded in this safe build.");
#endif
                    }
                }

"""

# Replace the old Sunshine menu rows with a CloudMoon session landing screen.
start = s.find("                // Row 0: Sunshine Host")
end = s.find("            } else if (ui_state == UI_STATE_SETTINGS)", start)
if start < 0 or end < 0:
    raise SystemExit("Could not locate the main-menu drawing block")
new_rows = """                // Row 0: Native CloudMoon catalogue
                SetFontSize(SF(24), SF(24));
                SetFontColor((active_main_item == 0) ? 0xff55c8ff : 0xffffffff, 0);
                DrawString(SX(60), SY(104), "[ BROWSE CLOUDMOON GAMES ]");
                SetFontSize(SF(17), SF(17));
                SetFontColor(0xffa9bfd8, 0);
                DrawString(SX(60), SY(135), "Explore verified titles from the public CloudMoon catalog.");

                // Row 1: Stream settings
                SetFontSize(SF(22), SF(22));
                SetFontColor((active_main_item == 1) ? 0xff55c8ff : 0xffffffff, 0);
                DrawString(SX(60), SY(188), "[ CONFIGURE STREAM SETTINGS ]");
                SetFontSize(SF(16), SF(16));
                SetFontColor(0xff7894b8, 0);
                DrawFormatString(SX(60), SY(216), "%d FPS | %d Kbps | %s",
                                 ui_fps, ui_get_bitrate(), ui_get_stream_res_name());

                // Row 2: Link selected game via phone
                SetFontSize(SF(22), SF(22));
                SetFontColor((active_main_item == 2) ? 0xff55c8ff : 0xffffffff, 0);
                DrawString(SX(60), SY(270), "[ LINK GAME FROM PHONE ]");
                SetFontSize(SF(16), SF(16));
                SetFontColor(0xff7894b8, 0);
                DrawString(SX(60), SY(298), "Scan the QR and import the active CloudMoon game session.");

                // Row 3: Connect
                SetFontSize(SF(22), SF(22));
                SetFontColor((active_main_item == 3) ? 0xff55c8ff : 0xffffffff, 0);
                DrawString(SX(60), SY(354), "[ CONNECT TO CLOUDMOON ]");
                SetFontSize(SF(16), SF(16));
                SetFontColor(0xff7894b8, 0);
                DrawString(SX(60), SY(382), "Connect using the game session linked to this PS3.");

                SetFontSize(SF(17), SF(17));
                SetFontColor(0xff7894b8, 0);
                DrawString(SX(60), SY(445), "\\x05 Navigate   |   \\x01 Select   |   \\x02 Exit to XMB");
"""

replace_once('DrawString(SX(40), SY(18), "Moonlight PS3");',
             'DrawString(SX(40), SY(18), "CloudMoon PS3");',
             "main title")
replace_once('DrawString(SX(40), SY(18), "Moonlight PS3  -  Stream Settings");',
             'DrawString(SX(40), SY(18), "CloudMoon PS3  -  Stream Settings");',
             "settings title")
replace_once('DrawString(SX(40), SY(18), "Moonlight PS3  -  Device Pairing");',
             'DrawString(SX(40), SY(18), "CloudMoon PS3  -  Session Connection");',
             "connection title")
replace_once('"Connecting to Sunshine Host..."',
             '"Connecting to CloudMoon coordinator..."',
             "connection status")
replace_once('"Initializing handshake session..."',
             '"Loading CloudMoon session and WebRTC transport..."',
             "connection detail")
replace_once('"Do you want to quit Moonlight and return to the PS3 XMB?"',
             '"Do you want to quit CloudMoon and return to the PS3 XMB?"',
             "exit confirmation")
replace_once('"ERROR: Target unreachable or Pairing failed."',
             '"ERROR: CloudMoon connection failed. Check cloudmoon.ini and network."',
             "error message")
replace_once('\"Press \\x01 to return.\"',
             '\"Press \\x01 to return to the menu.\"',
             "error hint")

replace_once('DrawString(SX(40), SY(18), \"Moonlight PS3  -  Host Applications\");',
             'DrawString(SX(40), SY(18), \"CloudMoon PS3  -  Game Catalog\");',
             "catalog title")
replace_once('DrawString(SX(60), SY(95), \"Select Game or Application to Stream:\");',
             'DrawString(SX(60), SY(95), \"Choose a CloudMoon game to link from your phone:\");',
             "catalog subtitle")
replace_once("Launch Game   |   ", "Select and link   |   ", "catalog legend")

# Replace Moonlight's PIN/pairing page with a CloudMoon coordinator connection page.
# The UI_STATE_PAIRING value is used only as the internal "connecting" state.
pair_start = s.find("            } else if (ui_state == UI_STATE_PAIRING) {",
                    s.find("// 1. Draw UI / Video"))
pair_end = s.find("            } else if (ui_state == UI_STATE_APPLIST) {", pair_start)
if pair_start < 0 or pair_end < 0:
    raise SystemExit("Could not locate the legacy Sunshine PIN screen")
cloudmoon_connect_view = """            } else if (ui_state == UI_STATE_LINKING) {
                SetFontSize(SF(26), SF(26));
                SetFontColor(0xffffffff, 0);
                DrawString(SX(40), SY(18), "CloudMoon PS3  -  Link Session");

                SetFontSize(SF(23), SF(23));
                SetFontColor(0xff55c8ff, 0);
                DrawString(SX(55), SY(100), "SCAN THIS QR CODE WITH YOUR PHONE");

                SetFontSize(SF(17), SF(17));
                SetFontColor(0xffb4cbe4, 0);
                DrawString(SX(55), SY(150), "1. Scan it while your phone is on the same Wi-Fi.");
                DrawString(SX(55), SY(184), "2. Sign in to CloudMoon in the new browser tab.");
                DrawString(SX(55), SY(218), "3. Start a game, then copy its full session URL.");
                DrawString(SX(55), SY(252), "4. Return here, paste the URL and tap Link to PS3.");

                SetFontSize(SF(15), SF(15));
                SetFontColor(0xff8ca6c6, 0);
                DrawString(SX(55), SY(325), "Only the active game session is sent to this PS3.");
                DrawString(SX(55), SY(350), "The one-time QR expires after 10 minutes.");
                if (ui_cloudmoon_pair_url[0]) {
                    DrawString(SX(55), SY(388), "Local address:");
                    DrawString(SX(55), SY(410), ui_cloudmoon_pair_url);
                } else {
                    DrawString(SX(55), SY(388), "Starting local pairing service...");
                }
                ui_draw_cloudmoon_pair_qr();

                SetFontSize(SF(18), SF(18));
                SetFontColor(0xff7894b8, 0);
                DrawString(SX(55), SY(445), "CIRCLE  CANCEL PHONE LINK");
            } else if (ui_state == UI_STATE_LINKED) {
                SetFontSize(SF(26), SF(26));
                SetFontColor(0xffffffff, 0);
                DrawString(SX(40), SY(18), "CloudMoon PS3  -  Session Linked");

                SetFontSize(SF(30), SF(30));
                SetFontColor(0xff72e0a0, 0);
                DrawString(SX(60), SY(145), "CLOUDMOON SESSION LINKED");

                SetFontSize(SF(20), SF(20));
                SetFontColor(0xffb4cbe4, 0);
                DrawString(SX(60), SY(210), "The active game session was saved to this PS3.");
                DrawString(SX(60), SY(250), "Return to the main menu and choose CONNECT TO CLOUDMOON.");

                SetFontSize(SF(18), SF(18));
                SetFontColor(0xff7894b8, 0);
                DrawString(SX(60), SY(445), "CROSS  RETURN TO MAIN MENU");
            } else if (ui_state == UI_STATE_PAIRING) {
                SetFontSize(SF(26), SF(26));
                SetFontColor(0xffffffff, 0);
                DrawString(SX(40), SY(18), "CloudMoon PS3  -  Cloud Session");

                SetFontSize(SF(25), SF(25));
                SetFontColor(0xff55c8ff, 0);
                DrawString(SX(60), SY(118), "CONNECTING TO CLOUDMOON");

                SetFontSize(SF(19), SF(19));
                SetFontColor(0xffb4cbe4, 0);
                DrawString(SX(60), SY(168), "Opening a secure connection to the CloudMoon coordinator.");
                DrawString(SX(60), SY(205), "Loading game, user_id and android_instance_id from:");
                DrawString(SX(60), SY(240), "USRDIR/cloudmoon.ini");

                SetFontSize(SF(18), SF(18));
                SetFontColor(0xff8ca6c6, 0);
                DrawString(SX(60), SY(310), "Negotiating WebRTC media and controller data channels.");
                DrawString(SX(60), SY(445), "CIRCLE  CANCEL CLOUDMOON CONNECTION");
            } else if (ui_state == UI_STATE_APPLIST) {
"""
s = s[:pair_start] + cloudmoon_connect_view + s[pair_end + len("            } else if (ui_state == UI_STATE_APPLIST) {\n"):]
# Fail the build if any legacy Sunshine PIN instructions remain in the compiled UI.
for forbidden in ("Sunshine Pairing Required", "Open Sunshine Web UI", "PIN:  %s"):
    if forbidden in s:
        raise SystemExit("Legacy Sunshine PIN UI survived: " + forbidden)

# Apply CloudMoon's restrained navy/metallic-blue visual palette.
# This changes UI colors only; Tiny3D initialization, viewport, thread and loop stay untouched.
palette = {
    "0xff82b1ff": "0xff55c8ff",  # selected text: electric cyan
    "0xffb0bec5": "0xffa9bfd8",  # secondary text: cool silver
    "0xff9e9e9e": "0xff7894b8",  # hints: muted blue-grey
    "0xff80d8ff": "0xff65caff",  # log text: icy blue
    "0.188f, 0.188f, 0.188f": "0.008f, 0.020f, 0.045f", # main background: deep navy
    "0.141f, 0.141f, 0.141f": "0.016f, 0.035f, 0.070f", # lower background: navy
    "0.200f, 0.260f, 0.620f": "0.025f, 0.160f, 0.360f", # header gradient
    "0.102f, 0.137f, 0.494f": "0.020f, 0.340f, 0.780f", # metallic-blue separator
    "0.118f, 0.118f, 0.118f": "0.018f, 0.040f, 0.075f", # log panel
    "0.12f, 0.12f, 0.12f": "0.025f, 0.055f, 0.110f",  # panel navy
    "0.090f, 0.090f, 0.090f": "0.012f, 0.026f, 0.060f", # deep footer
    "0.16f, 0.16f, 0.16f": "0.035f, 0.075f, 0.135f",  # selected row
    "1.0f, 0.51f, 0.69f": "0.10f, 0.55f, 0.95f", # legacy pink marker -> blue
    "0.247f, 0.318f, 0.710f": "0.035f, 0.420f, 0.920f", # blue accent
}
for old, new in palette.items():
    s = s.replace(old, new)

# Keep the upstream 64 KiB UI thread stack unchanged.
if "sysThreadCreate(&ui_thread, ui_loop, 0, 500, 0x10000," not in s:
    raise SystemExit("Upstream UI thread stack is missing")

p.write_text(s)
PY

# Give the generated package CloudMoon identity instead of the upstream Moonlight label.
python3 - "$PS3_BASE/Makefile" "$PS3_BASE/sfo.xml" "$PS3_BASE/sfo_debug.xml" <<'PY'
import pathlib, sys
makefile, *sfo_files = map(pathlib.Path, sys.argv[1:])
m = makefile.read_text()
m = m.replace("Moonlight PS3", "CloudMoon PS3")
m = m.replace("moonlight-ps3", "cloudmoon-ps3")
if "TITLE" not in m or "TARGET" not in m or "cloudmoon-ps3" not in m:
    raise SystemExit("Could not set CloudMoon package title and executable name")
makefile.write_text(m)
for p in sfo_files:
    x = p.read_text()
    x = x.replace("Moonlight PS3 Port.", "CloudMoon PS3 native WebRTC client")
    x = x.replace("Moonlight PS3", "CloudMoon PS3")
    if "CloudMoon PS3" not in x:
        raise SystemExit("Could not update package title in " + str(p))
    p.write_text(x)
PY

echo "CloudMoon transport modules integrated."
if [ "${CLOUDMOON_WEBRTC:-0}" = "1" ]; then
  echo "WebRTC, DualShock input and H.264/Opus PS3 decoder bridges integrated."
fi
