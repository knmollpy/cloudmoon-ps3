#include "orbis_input.h"

#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include <orbis/libkernel.h>

#include <string.h>

static int s_pad = -1;
static unsigned int s_previous;
static uint64_t s_repeat_start;
static uint64_t s_repeat_last;

static uint64_t now_us(void) {
    return sceKernelGetProcessTime();
}

static unsigned int map_buttons(const OrbisPadData *pad) {
    unsigned int keys = 0;
    if (pad->buttons & ORBIS_PAD_BUTTON_UP)      keys |= CM_UI_KEY_UP;
    if (pad->buttons & ORBIS_PAD_BUTTON_DOWN)    keys |= CM_UI_KEY_DOWN;
    if (pad->buttons & ORBIS_PAD_BUTTON_LEFT)    keys |= CM_UI_KEY_LEFT;
    if (pad->buttons & ORBIS_PAD_BUTTON_RIGHT)   keys |= CM_UI_KEY_RIGHT;
    if (pad->buttons & ORBIS_PAD_BUTTON_CROSS)   keys |= CM_UI_KEY_CROSS;
    if (pad->buttons & ORBIS_PAD_BUTTON_CIRCLE)  keys |= CM_UI_KEY_CIRCLE;
    if (pad->buttons & ORBIS_PAD_BUTTON_L1)      keys |= CM_UI_KEY_L1;
    if (pad->buttons & ORBIS_PAD_BUTTON_R1)      keys |= CM_UI_KEY_R1;
    if (pad->buttons & ORBIS_PAD_BUTTON_OPTIONS) keys |= CM_UI_KEY_OPTIONS;
    return keys;
}

int cm_orbis_input_init(void) {
    int32_t user_id = 0;
    int rc;
    if (s_pad >= 0) return 0;

    rc = scePadInit();
    if (rc < 0) return -1;

    rc = sceUserServiceGetInitialUser(&user_id);
    if (rc < 0) return -1;

    s_pad = scePadOpen(user_id, ORBIS_PAD_PORT_TYPE_STANDARD, 0, NULL);
    if (s_pad < 0) {
        s_pad = -1;
        return -1;
    }

    s_previous = 0;
    s_repeat_start = 0;
    s_repeat_last = 0;
    return 0;
}

void cm_orbis_input_shutdown(void) {
    if (s_pad >= 0) {
        (void)scePadClose(s_pad);
        s_pad = -1;
    }
}

int cm_orbis_input_poll(unsigned int *pressed) {
    OrbisPadData pad;
    unsigned int held, edges;
    const unsigned int dpad = CM_UI_KEY_UP | CM_UI_KEY_DOWN |
                              CM_UI_KEY_LEFT | CM_UI_KEY_RIGHT;
    uint64_t now;

    if (!pressed || s_pad < 0) return -1;
    *pressed = 0;
    memset(&pad, 0, sizeof(pad));
    if (scePadReadState(s_pad, &pad) < 0) return -1;

    held = map_buttons(&pad);
    edges = held & ~s_previous;
    now = now_us();

    if (held & dpad) {
        if (edges & dpad) {
            s_repeat_start = now;
            s_repeat_last = now;
        } else if (s_repeat_start && now - s_repeat_start >= 350000u &&
                   now - s_repeat_last >= 90000u) {
            edges |= held & dpad;
            s_repeat_last = now;
        }
    } else {
        s_repeat_start = 0;
        s_repeat_last = 0;
    }

    s_previous = held;
    *pressed = edges;
    return 0;
}
