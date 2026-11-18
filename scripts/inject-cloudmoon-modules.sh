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
    # Put the freshly built Mbed TLS headers before PSL1GHT's older bundled headers.
    sed -i 's#COMMON_INCLUDES[[:space:]]*:=[[:space:]]*#COMMON_INCLUDES := -D__PS3__ -I'"$ROOT_DIR"'/build/ps3deps/include -I'"$ROOT_DIR"'/vendor/tiny3D/lib/include #' "$PS3_BASE/Makefile"
    sed -i 's#-I./src -I./src/spu/include#-I./src -I./src/spu/include -DCM_ENABLE_WEBRTC -DRTC_STATIC#' "$PS3_BASE/Makefile"
    # Prefer the just-built Mbed TLS 3.x archives over older SDK portlibs.
    sed -i 's#-L./third_party/moonlight-common-c#-L'"$ROOT_DIR"'/build/ps3deps/lib -L'"$ROOT_DIR"'/vendor/tiny3D/lib -L./third_party/moonlight-common-c#' "$PS3_BASE/Makefile"
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
new_display = """  videoState vstate = {0};
  int video_state_rc = videoGetState(VIDEO_PRIMARY, 0, &vstate);
  int is_4_3 = 0;

  if (video_state_rc == 0) {
    if (vstate.displayMode.aspect == VIDEO_ASPECT_4_3) {
      is_4_3 = 1;
    } else if (vstate.displayMode.aspect == VIDEO_ASPECT_16_9) {
      is_4_3 = 0;
    }
  }

  NLOG("Display query=%d mode=%u scan=%u aspect=%u; UI will adapt to active framebuffer (%s)",
       video_state_rc,
       (unsigned int)vstate.displayMode.resolution,
       (unsigned int)vstate.displayMode.scanMode,
       (unsigned int)vstate.displayMode.aspect,
       is_4_3 ? "4:3" : "16:9");

  /* Placeholder design canvas; ui_loop replaces it with actual RSX dimensions. */
  ui_init(1280, 720, is_4_3);
"""
if old_display not in s:
    raise SystemExit("Could not match original unsafe display detection block")
s = s.replace(old_display, new_display, 1)
needle = "  ui_init(1280, 720, is_4_3);\n  ps3input_start();\n"
replacement_main = """  ui_init(1280, 720, is_4_3);
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

replacement_main = """  ui_init(1280, 720, is_4_3);
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
h = pathlib.Path(sys.argv[3])
u = ui.read_text()
if 'extern videoResolution Video_Resolution;' not in u:
    u = u.replace('#include <tiny3d.h>', '#include <tiny3d.h>\nextern videoResolution Video_Resolution;')
header = h.read_text()

# Scale the logical UI canvas to the actual, validated physical framebuffer.
# UserViewportSurface is only for render-to-texture surfaces; the menu is
# rendered directly to the display and therefore needs UserViewport.
old_viewport = "        tiny3d_UserViewportSurface(1, (float)ui_width, (float)ui_height);"
new_viewport = """        tiny3d_UserViewport(1, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f);"""
if old_viewport in u:
    u = u.replace(old_viewport, new_viewport, 1)
elif "tiny3d_UserViewport(1, 0.0f, 0.0f," not in u:
    raise SystemExit("Could not locate the UI display viewport call")
ready_decl = "static volatile int ui_first_frame_ready = 0;"
if ready_decl not in u:
    anchor = "static volatile int ui_running = 1;"
    if anchor not in u:
        raise SystemExit("Could not locate UI running state")
    u = u.replace(anchor, anchor + "\n" + ready_decl, 1)
ready_fn = """int ui_wait_ready(uint32_t timeout_ms) {
    const u64 start = sysGetSystemTime();
    const u64 timeout_us = (u64)timeout_ms * 1000ULL;
    while (!ui_first_frame_ready && ui_running) {
        if (sysGetSystemTime() - start >= timeout_us) return 0;
        usleep(10000);
    }
    return ui_first_frame_ready ? 1 : 0;
}
"""
if "int ui_wait_ready(uint32_t timeout_ms)" not in u:
    anchor = "int ui_is_running() { return ui_running; }"
    if anchor not in u:
        raise SystemExit("Could not locate UI status accessors")
    u = u.replace(anchor, anchor + "\n" + ready_fn, 1)
if "    ui_first_frame_ready = 0;" not in u:
    anchor = "void ui_init(int width, int height, int is_4_3) {\n"
    if anchor not in u:
        raise SystemExit("Could not locate ui_init")
    u = u.replace(anchor, anchor + "    ui_first_frame_ready = 0;\n", 1)
if "ui_first_frame_ready = 1;" not in u:
    anchor = "        tiny3d_Flip();\n"
    if anchor not in u:
        raise SystemExit("Could not locate first frame flip")
    u = u.replace(anchor, anchor + "        ui_first_frame_ready = 1;\n        NLOG(\"CloudMoon PS3 first UI frame rendered\");\n", 1)
elif 'NLOG("CloudMoon PS3 first UI frame rendered")' not in u:
    u = u.replace("        ui_first_frame_ready = 1;\n",
                  "        ui_first_frame_ready = 1;\n        NLOG(\"CloudMoon PS3 first UI frame rendered\");\n", 1)
if "int ui_wait_ready(uint32_t timeout_ms);" not in header:
    anchor = "void ui_init(int width, int height, int is_4_3);"
    if anchor not in header:
        raise SystemExit("Could not locate ui_init declaration")
    header = header.replace(anchor, anchor + "\nint ui_wait_ready(uint32_t timeout_ms);", 1)
ui.write_text(u)

# Never continue drawing if the patched Tiny3D/RSX initializer rejects the
# active GameOS display mode. Exit the title cleanly so XMB remains usable.
ui_init_call = "    tiny3d_Init(1024 * 1024); // 1MB vertex buffer"
ui_init_safe = """    int tiny3d_init_rc = tiny3d_Init(1024 * 1024);
    if (tiny3d_init_rc != TINY3D_OK) {
        NLOG("CloudMoon: Tiny3D/RSX initialization failed (%d); returning safely.", tiny3d_init_rc);
        ui_running = 0;
        sysThreadExit(0);
        return;
    }
    /* Use the framebuffer dimensions resolved from the active GameOS mode.
       1280x720 is only the UI design coordinate system, never an output mode. */
    if (Video_Resolution.width < 320 || Video_Resolution.width > 2048 ||
        Video_Resolution.height < 200 || Video_Resolution.height > 2048) {
        NLOG("CloudMoon: invalid active framebuffer dimensions %ux%u",
             (unsigned int)Video_Resolution.width,
             (unsigned int)Video_Resolution.height);
        ui_running = 0;
        sysThreadExit(0);
        return;
    }
    ui_width = (int)Video_Resolution.width;
    ui_height = (int)Video_Resolution.height;
    scale_x = (float)ui_width / 1280.0f;
    scale_y = (float)ui_height / 720.0f;
    scale_font = (scale_x < scale_y) ? scale_x : scale_y;
    NLOG("CloudMoon: UI canvas adapted to active output %dx%d", ui_width, ui_height);
    tiny3d_UserViewport(1, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f);
"""
if "int tiny3d_init_rc = tiny3d_Init(1024 * 1024);" not in u:
    if u.count(ui_init_call) != 1:
        raise SystemExit("Could not uniquely locate Tiny3D initialization in ui_loop")
    u = u.replace(ui_init_call, ui_init_safe, 1)
    ui.write_text(u)

h.write_text(header)
PY
  # Ship a real CA bundle inside the PKG so TLS certificate verification works.
  if ! grep -q 'cacert.pem' "$PS3_BASE/Makefile"; then
    sed -i '/mkdir -p $(BUILD)\/pkg\/USRDIR/a\	cp '"$ROOT_DIR"'/assets/cacert.pem $(BUILD)/pkg/USRDIR/cacert.pem' "$PS3_BASE/Makefile"
  fi
fi

# All CloudMoon variants (including B without WebRTC) must link the patched
# Tiny3D archive. Otherwise the SDK's original libtiny3d silently wins and
# the RSX display validation above is never used.
if ! grep -q 'vendor/tiny3D/lib/include' "$PS3_BASE/Makefile"; then
  sed -i 's#COMMON_INCLUDES[[:space:]]*:=[[:space:]]*#COMMON_INCLUDES := -I'"$ROOT_DIR"'/vendor/tiny3D/lib/include #' "$PS3_BASE/Makefile"
fi
if ! grep -q 'vendor/tiny3D/lib -L./third_party/moonlight-common-c' "$PS3_BASE/Makefile"; then
  sed -i 's#-L./third_party/moonlight-common-c#-L'"$ROOT_DIR"'/vendor/tiny3D/lib -L./third_party/moonlight-common-c#' "$PS3_BASE/Makefile"
fi

# Apply screen/RSX safety to the no-WebRTC diagnostic build as well. The full
# WebRTC branch above already patched these call sites; B must use the same
# display detection, UI scaling and Tiny3D failure handling to be a valid test.
python3 - "$DST/main.c" "$DST/ui.c" <<'PY'
import pathlib
import sys

main_path, ui_path = map(pathlib.Path, sys.argv[1:])
main = main_path.read_text()
ui = ui_path.read_text()

if "  // Çözünürlük Tespiti" in main:
    start = main.find("  // Çözünürlük Tespiti")
    end = main.find("  ps3input_start();", start)
    if start < 0 or end < 0:
        raise SystemExit("Could not locate original PS3 display detection block")
    safe_main = """  videoState vstate = {0};
  int video_state_rc = videoGetState(VIDEO_PRIMARY, 0, &vstate);
  int is_4_3 = 0;

  if (video_state_rc == 0) {
    if (vstate.displayMode.aspect == VIDEO_ASPECT_4_3) {
      is_4_3 = 1;
    } else if (vstate.displayMode.aspect == VIDEO_ASPECT_16_9) {
      is_4_3 = 0;
    }
  }

  NLOG("Display query=%d mode=%u scan=%u aspect=%u; using 1280x720 logical canvas (%s)",
       video_state_rc,
       (unsigned int)vstate.displayMode.resolution,
       (unsigned int)vstate.displayMode.scanMode,
       (unsigned int)vstate.displayMode.aspect,
       is_4_3 ? "4:3" : "16:9");

  /* Logical coordinates only. Keep the GameOS-selected HDMI mode unchanged. */
  ui_init(1280, 720, is_4_3);
"""
    main = main[:start] + safe_main + main[end:]
elif "int video_state_rc = videoGetState(VIDEO_PRIMARY, 0, &vstate);" not in main:
    raise SystemExit("Display detection is neither original nor safely patched")
# The original Moonlight loop used vres.height to detect PAL timing.
# vres is intentionally removed with the unsafe display query above; use the
# already-validated active GameOS resolution ID instead.
if "if (vres.height == 576)" in main:
    main = main.replace(
        "if (vres.height == 576)",
        "if (video_state_rc == 0 and vstate.displayMode.resolution == VIDEO_RESOLUTION_576)"
    )
    # Correct C syntax in the generated main.c condition.
    main = main.replace(
        "if (video_state_rc == 0 and vstate.displayMode.resolution == VIDEO_RESOLUTION_576)",
        "if (video_state_rc == 0 && vstate.displayMode.resolution == VIDEO_RESOLUTION_576)"
    )
elif "vres.height" in main:
    raise SystemExit("Unexpected leftover vres use in main.c after safe display patch")
main_path.write_text(main)

old_viewport = "        tiny3d_UserViewportSurface(1, (float)ui_width, (float)ui_height);"
new_viewport = """        if (ui_width <= 0 || ui_height <= 0 ||
            Video_Resolution.width == 0 || Video_Resolution.height == 0) {
            NLOG("CloudMoon: invalid viewport dimensions; stopping UI safely.");
            ui_running = 0;
            sysThreadExit(0);
            return;
        }
        tiny3d_UserViewport(1, 0.0f, 0.0f,
            (float)Video_Resolution.width / (float)ui_width,
            (float)Video_Resolution.height / (float)ui_height,
            1.0f, 1.0f);
"""
if old_viewport in ui:
    ui = ui.replace(old_viewport, new_viewport, 1)
elif "tiny3d_UserViewport(1, 0.0f, 0.0f," not in ui:
    raise SystemExit("Could not locate the UI display viewport call")

old_init = "    tiny3d_Init(1024 * 1024); // 1MB vertex buffer"
new_init = """    int tiny3d_init_rc = tiny3d_Init(1024 * 1024);
    if (tiny3d_init_rc != TINY3D_OK) {
        NLOG("CloudMoon: Tiny3D/RSX initialization failed (%d); returning safely.", tiny3d_init_rc);
        ui_running = 0;
        sysThreadExit(0);
        return;
    }
"""
if old_init in ui:
    ui = ui.replace(old_init, new_init, 1)
elif "int tiny3d_init_rc = tiny3d_Init(1024 * 1024);" not in ui:
    raise SystemExit("Could not locate Tiny3D initialization in ui_loop")

ui_path.write_text(ui)
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
