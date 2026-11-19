#include "orbis_dashboard.h"
#include "orbis_input.h"
#include "orbis_video.h"

#include <orbis/libkernel.h>

int cm_orbis_dashboard_run(cm_ui_model_t *model,
                           cm_orbis_ui_action_fn on_action,
                           cm_orbis_ui_tick_fn on_tick,
                           void *context) {
    cm_ui_state_t state;
    int rc;

    if (!model) return -1;
    rc = cm_orbis_input_init();
    if (rc != 0) return -2;
    rc = cm_orbis_video_init(1280, 720);
    if (rc != 0) {
        cm_orbis_input_shutdown();
        cm_orbis_video_shutdown();
        return -3;
    }

    cm_ui_init(&state);
    for (;;) {
        unsigned int pressed = 0;
        cm_ui_action_t event;
        cm_ui_surface_t *surface;

        if (on_tick) {
            rc = on_tick(model, context);
            if (rc != 0) {
                cm_orbis_input_shutdown();
                cm_orbis_video_shutdown();
                return rc;
            }
        }

        if (cm_orbis_input_poll(&pressed) == 0 && pressed != 0) {
            event = cm_ui_input(&state, pressed, model);
            if (event.type != CM_UI_ACTION_NONE && on_action) {
                rc = on_action(event, model, context);
                if (rc != 0) {
                    cm_orbis_input_shutdown();
                    cm_orbis_video_shutdown();
                    return rc;
                }
            }
        }

        surface = cm_orbis_video_surface();
        if (!surface) {
            cm_orbis_input_shutdown();
            cm_orbis_video_shutdown();
            return -4;
        }
        cm_ui_draw(surface, &state, model);
        if (cm_orbis_video_present() != 0) {
            cm_orbis_input_shutdown();
            cm_orbis_video_shutdown();
            return -5;
        }
        sceKernelUsleep(16000);
    }
}
