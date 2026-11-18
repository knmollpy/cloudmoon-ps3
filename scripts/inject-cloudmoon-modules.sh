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
main_loop_patch = """  while (ui_is_running()) {
    sysUtilCheckCallback();
    if (ui_get_state() == UI_STATE_PAIRING) {
      cm_cloudmoon_autostart();
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

# CloudMoon panel helper: uses the same upstream Tiny3D 2D renderer and viewport.
helper_code = r"""
static void cloudmoon_draw_panel(float x1, float y1, float x2, float y2,
                                 float r, float g, float b, float a) {
    tiny3d_SetPolygon(TINY3D_TRIANGLE_STRIP);
    tiny3d_VertexPos(x1, y1, 65535);
    tiny3d_VertexFcolor(r, g, b, a);
    tiny3d_VertexPos(x2, y1, 65535);
    tiny3d_VertexFcolor(r, g, b, a);
    tiny3d_VertexPos(x1, y2, 65535);
    tiny3d_VertexFcolor(r, g, b, a);
    tiny3d_VertexPos(x2, y2, 65535);
    tiny3d_VertexFcolor(r, g, b, a);
    tiny3d_End();
}
"""
if "static void cloudmoon_draw_panel(" not in s:
    helper_anchor = "static void ui_loop(void *arg) {"
    if helper_anchor not in s:
        raise SystemExit("Could not locate UI loop for CloudMoon card helper")
    s = s.replace(helper_anchor, helper_code + "\n" + helper_anchor, 1)

# Replace the old Sunshine menu rows with a CloudMoon session landing screen.
start = s.find("                // Row 0: Sunshine Host")
end = s.find("            } else if (ui_state == UI_STATE_SETTINGS)", start)
if start < 0 or end < 0:
    raise SystemExit("Could not locate the main-menu drawing block")
new_rows = """                // CloudMoon home: two large selectable action cards.
                const int settings_selected = (active_main_item == 0);
                const int connect_selected = (active_main_item == 1);
                const float selected_r = 0.055f, selected_g = 0.66f, selected_b = 1.0f;
                const float idle_r = 0.025f, idle_g = 0.24f, idle_b = 0.48f;

                // Main action card backgrounds.
                cloudmoon_draw_panel(SX(48), SY(105), SX(620), SY(360),
                                     0.010f, 0.029f, 0.068f, 0.98f);
                cloudmoon_draw_panel(SX(650), SY(105), SX(1232), SY(360),
                                     0.008f, 0.024f, 0.060f, 0.98f);

                // Active card gets a bright cyan top and left edge; idle card stays blue.
                cloudmoon_draw_panel(SX(48), SY(105), SX(620), SY(109),
                                     settings_selected ? selected_r : idle_r,
                                     settings_selected ? selected_g : idle_g,
                                     settings_selected ? selected_b : idle_b, 1.0f);
                cloudmoon_draw_panel(SX(48), SY(105), SX(54), SY(360),
                                     settings_selected ? selected_r : idle_r,
                                     settings_selected ? selected_g : idle_g,
                                     settings_selected ? selected_b : idle_b, 1.0f);
                cloudmoon_draw_panel(SX(650), SY(105), SX(1232), SY(109),
                                     connect_selected ? selected_r : idle_r,
                                     connect_selected ? selected_g : idle_g,
                                     connect_selected ? selected_b : idle_b, 1.0f);
                cloudmoon_draw_panel(SX(650), SY(105), SX(656), SY(360),
                                     connect_selected ? selected_r : idle_r,
                                     connect_selected ? selected_g : idle_g,
                                     connect_selected ? selected_b : idle_b, 1.0f);

                // Left card: stream profile.
                SetFontSize(SF(16), SF(16));
                SetFontColor(0x61d2ffff, 0);
                DrawString(SX(78), SY(137), "01  /  STREAM PROFILE"); /* CLOUDMOON_HOME_CARDS */
                SetFontSize(SF(29), SF(29));
                SetFontColor(settings_selected ? 0x44b8ffff : 0xb4cbe4ff, 0);
                DrawString(SX(78), SY(178), "CONFIGURE STREAM");
                SetFontSize(SF(18), SF(18));
                SetFontColor(0xb4cbe4ff, 0);
                int cloudmoon_kbps = ui_bitrate_options[ui_bitrate_idx];
                DrawFormatString(SX(78), SY(231), "TARGET   %d FPS     BITRATE   %.1f Mbps",
                                 ui_fps, (float)cloudmoon_kbps / 1000.0f);
                DrawFormatString(SX(78), SY(265), "OUTPUT   %s     RESOLUTION   %s",
                                 ui_display_is_4_3 ? "4:3" : "16:9",
                                 ui_get_stream_res_name());
                SetFontSize(SF(17), SF(17));
                SetFontColor(settings_selected ? 0x61d2ffff : 0x8ca6c6ff, 0);
                DrawString(SX(78), SY(319), settings_selected
                           ? "[ X ]  OPEN STREAM SETTINGS"
                           : "PRESS X TO EDIT STREAM SETTINGS");

                // Right card: CloudMoon session entry.
                SetFontSize(SF(16), SF(16));
                SetFontColor(0x61d2ffff, 0);
                DrawString(SX(680), SY(137), "02  /  CLOUD SESSION");
                SetFontSize(SF(28), SF(28));
                SetFontColor(connect_selected ? 0x44b8ffff : 0xb4cbe4ff, 0);
                DrawString(SX(680), SY(178), "CONNECT TO CLOUDMOON");
                SetFontSize(SF(18), SF(18));
                SetFontColor(0xb4cbe4ff, 0);
                DrawString(SX(680), SY(231), "Load the active coordinator session");
                DrawString(SX(680), SY(263), "USRDIR/cloudmoon.ini");
                SetFontSize(SF(16), SF(16));
                SetFontColor(0x8ca6c6ff, 0);
                DrawString(SX(680), SY(291), "Requires game, user_id and android_instance_id");
                SetFontSize(SF(17), SF(17));
                SetFontColor(connect_selected ? 0x61d2ffff : 0x8ca6c6ff, 0);
                DrawString(SX(680), SY(319), connect_selected
                           ? "[ X ]  START CLOUD SESSION"
                           : "PRESS X TO CONNECT");

                // Compact stream summary strip.
                cloudmoon_draw_panel(SX(48), SY(382), SX(1232), SY(454),
                                     0.008f, 0.022f, 0.052f, 0.98f);
                cloudmoon_draw_panel(SX(48), SY(382), SX(1232), SY(384),
                                     0.025f, 0.30f, 0.72f, 1.0f);
                SetFontSize(SF(13), SF(13));
                SetFontColor(0x8ca6c6ff, 0);
                DrawString(SX(76), SY(397), "ACTIVE STREAM PROFILE");
                SetFontSize(SF(18), SF(18));
                SetFontColor(0xf0f6ffff, 0);
                DrawFormatString(SX(76), SY(425), "%d FPS", ui_fps);
                DrawFormatString(SX(288), SY(425), "%.1f Mbps", (float)cloudmoon_kbps / 1000.0f);
                DrawString(SX(500), SY(425), ui_display_is_4_3 ? "4:3 DISPLAY" : "16:9 DISPLAY");
                DrawString(SX(735), SY(425), ui_get_stream_res_name());
                DrawString(SX(1015), SY(425),
                           ui_get_scaling_mode() == SCALING_MODE_SHADER ? "SHADER" : "LEGACY");

                // Controller guide.
                SetFontSize(SF(16), SF(16));
                SetFontColor(0x8ca6c6ff, 0);
                DrawString(SX(76), SY(479), "D-PAD  NAVIGATE     |     X  SELECT     |     O  EXIT TO XMB");
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

# Apply a clearly visible CloudMoon navy/blue theme to the actual Tiny3D draw colors.
# Include alpha-independent RGB triplets and the framebuffer clear color; fail closed
# if the upstream palette changes so a future PKG cannot silently ship gray again.
clear_old = "tiny3d_Clear(0x20202000, TINY3D_CLEAR_ALL);"
clear_new = "tiny3d_Clear(0x07142600, TINY3D_CLEAR_ALL);"
if clear_old not in s:
    if clear_new not in s:
        raise SystemExit("Could not find the stock gray framebuffer clear color")
else:
    s = s.replace(clear_old, clear_new, 1)

palette = {
    "0.188f, 0.188f, 0.188f": "0.012f, 0.030f, 0.070f",
    "0.141f, 0.141f, 0.141f": "0.006f, 0.016f, 0.040f",
    "0.200f, 0.260f, 0.620f": "0.025f, 0.150f, 0.340f",
    "0.102f, 0.137f, 0.494f": "0.020f, 0.420f, 0.920f",
    "0.247f, 0.318f, 0.710f": "0.025f, 0.300f, 0.720f",
    "0.07f, 0.07f, 0.07f": "0.010f, 0.026f, 0.060f",
    "0.12f, 0.12f, 0.12f": "0.012f, 0.035f, 0.080f",
    "0.118f, 0.118f, 0.118f": "0.010f, 0.028f, 0.068f",
    "0.090f, 0.090f, 0.090f": "0.006f, 0.018f, 0.045f",
    "0.16f, 0.16f, 0.16f": "0.025f, 0.090f, 0.190f",
    "1.0f, 0.51f, 0.69f": "0.06f, 0.58f, 1.0f",
    "0xff82b1ff": "0x44b8ffff",
    "0xffb0bec5": "0xb4cbe4ff",
    "0xff9e9e9e": "0x8ca6c6ff",
    "0xff80d8ff": "0x61d2ffff",
    "0x00ff00ff": "0x61d2ffff",
    "0xffff82b1": "0x61d2ffff",
}
for old, new in palette.items():
    if old not in s and new not in s:
        raise SystemExit("Expected UI palette token missing: " + old)
    s = s.replace(old, new)

# Fail the build if any of the known pink RGBA literals survived.
for pink_token in ("0xff82b1ff", "0xffb0bec5", "0xff9e9e9e",
                   "0xff80d8ff", "0x00ff00ff", "0xffff82b1"):
    if pink_token in s:
        raise SystemExit("Pink font color survived CloudMoon theme patch: " + pink_token)

# Ensure the rebuilt home screen is genuinely card-based, not the stock row layout.
if "cloudmoon_draw_panel" not in s or "01  /  STREAM PROFILE" not in s:
    raise SystemExit("CloudMoon redesigned home screen verification failed")

# Verify strong blue colors are present in the final patched UI source.
for token in ("tiny3d_Clear(0x07142600", "0.025f, 0.300f, 0.720f", "0xff55c8ff"):
    if token not in s:
        raise SystemExit("CloudMoon blue theme verification failed: " + token)

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

# Keep CloudMoon XMB artwork in both the no-WebRTC B and WebRTC builds.
# This is shared by both build variants: the menu-only B package must also
# carry the custom XMB icon and background, without enabling WebRTC.
python3 "$ROOT_DIR/scripts/generate-cloudmoon-art.py" "$ROOT_DIR/assets"
cp "$ROOT_DIR/assets/ICON0.PNG" "$PS3_BASE/ICON0.PNG"
cp "$ROOT_DIR/assets/PIC1.PNG" "$PS3_BASE/PIC1.PNG"
python3 - "$PS3_BASE/Makefile" <<'PY'
import pathlib, sys
p = pathlib.Path(sys.argv[1])
lines = p.read_text().splitlines()
if not any("cp PIC1.PNG $(BUILD)/pkg/PIC1.PNG" in line for line in lines):
    for i, line in enumerate(lines):
        if "cp $(ICON0) $(BUILD)/pkg/ICON0.PNG" in line:
            lines.insert(i + 1, "\tcp PIC1.PNG $(BUILD)/pkg/PIC1.PNG")
            break
    else:
        raise SystemExit("Could not locate the upstream ICON0 package-copy recipe")
p.write_text("\n".join(lines) + "\n")
PY
