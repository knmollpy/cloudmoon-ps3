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
  cloudmoon_link.c cloudmoon_link.h \
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
  sed -i 's#src/spu_opus_bridge.c#src/spu_opus_bridge.c src/cloudmoon_input.c src/cloudmoon_link.c src/cloudmoon_client.c src/cloudmoon_discovery.c src/cloudmoon_protocol.c src/cloudmoon_session.c src/cloudmoon_ws.c src/cloudmoon_upgrade.c src/cloudmoon_random.c src/cloudmoon_ps3_random.c src/cloudmoon_tls_polarssl.c src/cloudmoon_ws_tls.c#' "$PS3_BASE/Makefile"
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
  fi
  python3 "$ROOT_DIR/scripts/generate-cloudmoon-art.py" "$ROOT_DIR/assets"
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
if "int net_rc = ps3_net_init();" not in s:
    if s.count("  ps3_net_init();") != 1:
        raise SystemExit("Could not uniquely locate the original network initialization call")
    s = s.replace("  ps3_net_init();", "  int net_rc = ps3_net_init();", 1)
if 'NLOG("CloudMoon: network initialization result %d", net_rc);' not in s:
    anchor = "  net_logger_init();\n"
    if anchor not in s:
        raise SystemExit("Could not locate network logger startup")
    s = s.replace(anchor, anchor + '  NLOG("CloudMoon: network initialization result %d", net_rc);\n', 1)

replacement_main = """  ui_init(width, height, is_4_3);
  ps3input_start();
  NLOG("CloudMoon PS3 UI Initialized");
  NLOG("CloudMoon: waiting for Connect from the PS3 menu");
"""
s = s.replace(needle, replacement_main, 1)
# CloudMoon replaces Moonlight's blocking pairing/stream state machine.
# Do not leave the legacy Sunshine handshake ahead of the CloudMoon Connect action.
main_loop_patch = """  int cloudmoon_connect_started = 0;
  while (ui_is_running()) {
    sysUtilCheckCallback();
    if (ui_get_state() == UI_STATE_PAIRING) {
      /* Connect is a CloudMoon session action, never a Sunshine pairing flow. */
      if (!cloudmoon_connect_started) {
        cloudmoon_connect_started = 1;
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
      /* Circle cancels the pending coordinator/WebRTC session cleanly. */
      if (cm_cloudmoon_is_running()) {
        NLOG("CloudMoon: menu cancellation requested; stopping active session.");
        cm_cloudmoon_stop();
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
    s = s.replace(anchor_end, anchor_end + "\n  cm_cloudmoon_stop();", 1)

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
python3 - "$DST/ui.c" <<'PY'
import pathlib
import sys

p = pathlib.Path(sys.argv[1])
s = p.read_text()
if "CLOUDMOON_PS3_MENU_PATCH" in s:
    raise SystemExit(0)

def replace_once(old, new, label):
    global s
    if old not in s:
        if new in s:
            return
        raise SystemExit("Could not patch UI " + label)
    s = s.replace(old, new, 1)

replace_once("#define MAIN_MENU_ITEM_COUNT 3",
             "#define MAIN_MENU_ITEM_COUNT 2 /* CLOUDMOON_PS3_MENU_PATCH */",
             "main menu item count")
replace_once("static int active_main_item = 0; // 0: Sunshine Host IP, 1: Configure Settings, 2: Connect/Pair",
             "static int active_main_item = 0; // 0: Configure Settings, 1: Connect to CloudMoon",
             "main menu item mapping")

# Replace the inherited Sunshine host-IP interaction with two real CloudMoon actions.
start = s.find("                // Action handling per main menu item")
end = s.find("                // START button initiates connection", start)
if start < 0 or end < 0:
    raise SystemExit("Could not locate the main-menu action handler")
new_actions = """                // CloudMoon main actions: stream settings and session connect.
                if (active_main_item == 0) {
                    if ((pad.buttons_pressed & A_FLAG) || (pad.buttons_pressed & RIGHT_FLAG)) {
                        ui_state = UI_STATE_SETTINGS;
                        active_settings_item = 0;
                    }
                } else if (active_main_item == 1) {
                    if (pad.buttons_pressed & A_FLAG) {
                        ui_flush_settings_if_dirty();
                        ui_state = UI_STATE_PAIRING;
                    }
                }

"""
s = s[:start] + new_actions + s[end:]

# Replace the old Sunshine menu rows with a CloudMoon session landing screen.
start = s.find("                // Row 0: Sunshine Host")
end = s.find("            } else if (ui_state == UI_STATE_SETTINGS)", start)
if start < 0 or end < 0:
    raise SystemExit("Could not locate the main-menu drawing block")
new_rows = """                // Row 0: Stream settings
                SetFontSize(SF(24), SF(24));
                SetFontColor((active_main_item == 0) ? 0xff82b1ff : 0xffffffff, 0);
                DrawString(SX(60), SY(125), "[ CONFIGURE STREAM SETTINGS ]");

                // Active settings summary preview
                SetFontSize(SF(18), SF(18));
                SetFontColor(0xff9e9e9e, 0);
                int kbps = ui_bitrate_options[ui_bitrate_idx];
                if (kbps % 1000 == 0) {
                    DrawFormatString(SX(60), SY(165), "Current: %d FPS | %d Mbps | %s | %s | %s",
                                     ui_fps, kbps / 1000,
                                     ui_display_is_4_3 ? "4:3 Display" : "16:9 Display",
                                     ui_get_stream_res_name(), ui_get_scaling_mode_name());
                } else {
                    DrawFormatString(SX(60), SY(165), "Current: %d FPS | %.1f Mbps | %s | %s | %s",
                                     ui_fps, (float)kbps / 1000.0f,
                                     ui_display_is_4_3 ? "4:3 Display" : "16:9 Display",
                                     ui_get_stream_res_name(), ui_get_scaling_mode_name());
                }

                // Row 1: CloudMoon session connect
                SetFontSize(SF(24), SF(24));
                SetFontColor((active_main_item == 1) ? 0xff82b1ff : 0xffffffff, 0);
                DrawString(SX(60), SY(235), "[ CONNECT TO CLOUDMOON ]");

                SetFontSize(SF(18), SF(18));
                SetFontColor(0xffb0bec5, 0);
                DrawString(SX(60), SY(280), "Session file: USRDIR/cloudmoon.ini");
                DrawString(SX(60), SY(312), "Set game, user_id and android_instance_id before connecting.");

                // Controls legend
                SetFontSize(SF(18), SF(18));
                SetFontColor(0xff9e9e9e, 0);
                DrawString(SX(60), SY(445), "\\x05 Navigate   |   \\x01 Select   |   \\x02 Exit to XMB");
"""
s = s[:start] + new_rows + s[end:]

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
replace_once('"Press \\x01 to return."',
             '"Press \\x01 to return to the menu."',
             "error hint")

# Replace Moonlight's PIN/pairing page with a CloudMoon coordinator connection page.
# The UI_STATE_PAIRING value is used only as the internal "connecting" state.
pair_start = s.find("            } else if (ui_state == UI_STATE_PAIRING) {",
                    s.find("// 1. Draw UI / Video"))
pair_end = s.find("            } else if (ui_state == UI_STATE_APPLIST) {", pair_start)
if pair_start < 0 or pair_end < 0:
    raise SystemExit("Could not locate the legacy Sunshine PIN screen")
cloudmoon_connect_view = """            } else if (ui_state == UI_STATE_PAIRING) {
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
