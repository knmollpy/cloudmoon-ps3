#ifndef CLOUDMOON_ORBIS_DASHBOARD_H
#define CLOUDMOON_ORBIS_DASHBOARD_H

#include "../ui/cloudmoon_ui.h"

typedef int (*cm_orbis_ui_action_fn)(cm_ui_action_t action,
                                     cm_ui_model_t *model,
                                     void *context);
typedef int (*cm_orbis_ui_tick_fn)(cm_ui_model_t *model, void *context);

/* The tick callback is run once per frame, allowing pairing and background
   network work to update the live dashboard without blocking input/rendering. */
int cm_orbis_dashboard_run(cm_ui_model_t *model,
                           cm_orbis_ui_action_fn on_action,
                           cm_orbis_ui_tick_fn on_tick,
                           void *context);

#endif
