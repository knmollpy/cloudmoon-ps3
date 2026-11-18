#ifndef CLOUDMOON_ORBIS_DASHBOARD_H
#define CLOUDMOON_ORBIS_DASHBOARD_H

#include "../ui/cloudmoon_ui.h"

typedef int (*cm_orbis_ui_action_fn)(cm_ui_action_t action,
                                     cm_ui_model_t *model,
                                     void *context);

/* Runs the independent CloudMoon dashboard. Return non-zero from the action
   callback to leave the dashboard and hand control back to the app. */
int cm_orbis_dashboard_run(cm_ui_model_t *model,
                           cm_orbis_ui_action_fn on_action,
                           void *context);

#endif
