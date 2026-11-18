#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
PS3_BASE=${1:-"$ROOT_DIR/vendor/PS3-Moonlight"}
PS3DEV=${PS3DEV:-"$PS3_BASE/ps3dev"}
TINY3D="$ROOT_DIR/vendor/tiny3D"

if [ ! -f "$PS3DEV/ppu_rules" ]; then
  echo "PS3 SDK rules not found at $PS3DEV/ppu_rules" >&2
  exit 1
fi
if [ ! -f "$TINY3D/lib/source/rsxutil.c" ] || [ ! -f "$TINY3D/lib/source/tiny3d.c" ]; then
  echo "Tiny3D source submodule is missing or incomplete." >&2
  exit 1
fi

python3 - "$TINY3D/lib/source/rsxutil.c" "$TINY3D/lib/source/rsxutil.h" "$TINY3D/lib/source/tiny3d.c" <<'PY'
import pathlib
import sys

rsx_path, rsx_header_path, tiny_path = map(pathlib.Path, sys.argv[1:])
rsx = rsx_path.read_text()
rsx_header = rsx_header_path.read_text()
tiny = tiny_path.read_text()

if "CLOUDMOON_SAFE_DISPLAY_INIT" not in rsx:
    start = rsx.find("// Initilize and rsx")
    end = rsx.find("\nvoid waitFlip()", start)
    if start < 0 or end < 0:
        raise SystemExit("Could not locate Tiny3D init_screen() implementation")

    safe_init = r"""
/* CLOUDMOON_SAFE_DISPLAY_INIT
 * Resolve the active GameOS output mode before touching RSX buffers.
 * Never pass an unsupported resolution ID to videoGetResolution(), and never
 * derive pitches or buffer sizes from an uninitialized/zero-sized structure.
 */
static int cloudmoon_resolve_display_resolution(const videoState *state,
                                                 videoResolution *resolution) {
    if (!state || !resolution) return -1;
    memset(resolution, 0, sizeof(*resolution));

    if (videoGetResolution(state->displayMode.resolution, resolution) == 0 &&
        resolution->width >= 320 && resolution->width <= 2048 &&
        resolution->height >= 200 && resolution->height <= 2048) {
        return 0;
    }

    /* PSL1GHT exposes these valid custom PS3 display IDs, but
       videoGetResolution() documents only 1080/720/480/576. */
    switch (state->displayMode.resolution) {
        case VIDEO_RESOLUTION_1600x1080:
            resolution->width = 1600; resolution->height = 1080; return 0;
        case VIDEO_RESOLUTION_1440x1080:
            resolution->width = 1440; resolution->height = 1080; return 0;
        case VIDEO_RESOLUTION_1280x1080:
            resolution->width = 1280; resolution->height = 1080; return 0;
        case VIDEO_RESOLUTION_960x1080:
            resolution->width = 960; resolution->height = 1080; return 0;
        default:
            resolution->width = 0; resolution->height = 0; return -1;
    }
}

/* Initialize RSX using the mode already selected by GameOS/XMB.
   This function never chooses 720p/1080p or changes the resolution ID. */
int init_screen(int command_buffer, int z_method) {
    videoState state;
    videoConfiguration vconfig;
    u32 buffer_size, depth_buffer_size;
    int rc;

    memset(&state, 0, sizeof(state));
    rc = videoGetState(VIDEO_PRIMARY, 0, &state);
    if (rc != 0 || state.state != VIDEO_STATE_ENABLED) {
        printf("Tiny3D: videoGetState failed/disabled (rc=%d, state=%u)\n",
               rc, (unsigned int)state.state);
        return -1;
    }

    rc = cloudmoon_resolve_display_resolution(&state, &Video_Resolution);
    if (rc != 0 || !Video_Resolution.width || !Video_Resolution.height) {
        printf("Tiny3D: unsupported or invalid current display mode (id=%u)\n",
               (unsigned int)state.displayMode.resolution);
        return -1;
    }

    if (!tiny_host_addr) {
        tiny_host_addr = memalign(1024 * 1024, command_buffer);
        if (!tiny_host_addr) {
            printf("Tiny3D: RSX shared-memory allocation failed\n");
            return -1;
        }
        context = rsxtiny_Init(0x10000, command_buffer, tiny_host_addr);
        if (!context) {
            printf("Tiny3D: gcmInitBody failed\n");
            free(tiny_host_addr);
            tiny_host_addr = NULL;
            return -1;
        }
    }
    if (!context) {
        printf("Tiny3D: RSX context is null\n");
        return -1;
    }

    if (z_method) zformat = REALITY_TARGET_FORMAT_ZETA_Z24S8;
    else zformat = REALITY_TARGET_FORMAT_ZETA_Z16;

    Video_pitch = 4 * (((u32)Video_Resolution.width + 15U) / 16U) * 16U;
    if (!z_method) {
        depth_pitch = 2 * ((Video_Resolution.width > 1920)
            ? (((Video_Resolution.width + 31) / 32) * 32) : 1920);
    } else {
        depth_pitch = 4 * ((Video_Resolution.width > 1920)
            ? (((Video_Resolution.width + 15) / 16) * 16) : 1920);
    }

    memset(&vconfig, 0, sizeof(vconfig));
    vconfig.resolution = state.displayMode.resolution;
    vconfig.format = VIDEO_BUFFER_FORMAT_XRGB;
    vconfig.pitch = Video_pitch;
    Video_aspect = vconfig.aspect = state.displayMode.aspect;

    /* Preserve the current XMB-selected mode; only set the RSX framebuffer format/pitch. */
    rc = videoConfigure(VIDEO_PRIMARY, &vconfig, NULL, 1);
    if (rc != 0) {
        printf("Tiny3D: videoConfigure for existing mode failed (rc=%d)\n", rc);
        return -1;
    }

    memset(&state, 0, sizeof(state));
    rc = videoGetState(VIDEO_PRIMARY, 0, &state);
    if (rc != 0 || state.state != VIDEO_STATE_ENABLED ||
        state.displayMode.resolution != vconfig.resolution) {
        printf("Tiny3D: display mode changed/invalid after framebuffer setup (rc=%d)\n", rc);
        return -1;
    }

    /* Re-resolve after configure in case the system reports a different mode. */
    if (cloudmoon_resolve_display_resolution(&state, &Video_Resolution) != 0) {
        printf("Tiny3D: could not verify the configured display resolution\n");
        return -1;
    }

    rsxtiny_MakeCmdSpace(context, 0x4000);
    rsxtiny_MemAlign(-1, 0);

    buffer_size = (u32)Video_pitch * (u32)Video_Resolution.height;
    if (!z_method) {
        depth_buffer_size = (u32)depth_pitch *
            ((Video_Resolution.height > 1088)
                ? (((Video_Resolution.height + 31) / 32) * 32) : 1088);
    } else {
        depth_buffer_size = (u32)depth_pitch *
            ((Video_Resolution.height > 1088)
                ? (((Video_Resolution.height + 15) / 16) * 16) : 1088);
    }
    if (!buffer_size || !depth_buffer_size) {
        printf("Tiny3D: computed zero RSX buffer size\n");
        return -1;
    }

    gcmSetFlipMode(GCM_FLIP_VSYNC);

    Video_buffer[0] = rsxtiny_MemAlign(64, (s32)buffer_size);
    Video_buffer[1] = rsxtiny_MemAlign(64, (s32)buffer_size);
    depth_buffer = rsxtiny_MemAlign(64, (s32)depth_buffer_size);
    if (!Video_buffer[0] || !Video_buffer[1] || !depth_buffer) {
        printf("Tiny3D: RSX framebuffer allocation failed (%u bytes, depth %u)\n",
               buffer_size, depth_buffer_size);
        return -1;
    }

    if (rsxtiny_AddressToOffset(Video_buffer[0], &offset[0]) != 0 ||
        rsxtiny_AddressToOffset(Video_buffer[1], &offset[1]) != 0 ||
        rsxtiny_AddressToOffset(depth_buffer, &depth_offset) != 0) {
        printf("Tiny3D: could not map RSX framebuffer to offsets\n");
        return -1;
    }

    gcmSetDisplayBuffer(0, offset[0], Video_pitch,
                        Video_Resolution.width, Video_Resolution.height);
    gcmSetDisplayBuffer(1, offset[1], Video_pitch,
                        Video_Resolution.width, Video_Resolution.height);

    gcmResetFlipStatus();
    flip(Video_currentBuffer ^ 1);
    printf("Tiny3D: RSX ready for existing display mode %ux%u (id=%u, aspect=%u)\n",
           (unsigned int)Video_Resolution.width,
           (unsigned int)Video_Resolution.height,
           (unsigned int)state.displayMode.resolution,
           (unsigned int)state.displayMode.aspect);
    return 0;
}
"""
    rsx = rsx[:start] + safe_init + rsx[end:]
    rsx_path.write_text(rsx)

if "int init_screen(int command_buffer, int z_method);" not in rsx_header:
    rsx_header = rsx_header.replace(
        "void init_screen(int command_buffer, int z_method);",
        "int init_screen(int command_buffer, int z_method);"
    )
    if "int init_screen(int command_buffer, int z_method);" not in rsx_header:
        raise SystemExit("Could not update Tiny3D init_screen() declaration")
    rsx_header_path.write_text(rsx_header)

if "CLOUDMOON_CHECK_INIT_SCREEN" not in tiny:
    old = "    init_screen(vertex_buff_size * 2, use_Z32);"
    new = """    /* CLOUDMOON_CHECK_INIT_SCREEN: stop if display/RSX setup failed. */
    n = init_screen(vertex_buff_size * 2, use_Z32);
    if (n != 0) {
        inited = 0;
        return TINY3D_INVALID;
    }"""
    if tiny.count(old) != 1:
        raise SystemExit("Could not uniquely locate Tiny3D init_screen() call")
    tiny = tiny.replace(old, new, 1)

if "if (!CtrlReg)" not in tiny:
    old = "    CtrlReg = gcmGetControlRegister((gcmContextData *) context);"
    new = """    CtrlReg = gcmGetControlRegister((gcmContextData *) context);
    if (!CtrlReg) {
        inited = 0;
        return TINY3D_INVALID;
    }"""
    if tiny.count(old) != 1:
        raise SystemExit("Could not uniquely locate Tiny3D control-register setup")
    tiny = tiny.replace(old, new, 1)

if "CLOUDMOON_CHECK_VERTEX_ALLOC" not in tiny:
    old = "    rsx_vertex = rsxtiny_MemAlign(64, vertex_buff_size);"
    new = """    rsx_vertex = rsxtiny_MemAlign(64, vertex_buff_size);
    if (!rsx_vertex) {
        inited = 0;
        return TINY3D_OUTMEMORY;
    }
    /* CLOUDMOON_CHECK_VERTEX_ALLOC */"""
    if tiny.count(old) != 1:
        raise SystemExit("Could not uniquely locate Tiny3D vertex-buffer allocation")
    tiny = tiny.replace(old, new, 1)

tiny_path.write_text(tiny)
PY

export PS3DEV="$PS3DEV"
export PSL1GHT="$PS3DEV"
export PORTLIBS="$PS3DEV/portlibs/ppu"

echo "Building patched Tiny3D against $PS3DEV"
make -C "$TINY3D/lib" clean
make -C "$TINY3D/lib" --no-print-directory
test -s "$TINY3D/lib/libtiny3d.a"
echo "Safe Tiny3D archive ready: $TINY3D/lib/libtiny3d.a"
