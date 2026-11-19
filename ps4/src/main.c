#include "platform/orbis_dashboard.h"

#include <orbis/Sysmodule.h>
#include <orbis/UserService.h>
#include <orbis/libkernel.h>

#include <string.h>

static int handle_ui_action(cm_ui_action_t action,
                            cm_ui_model_t *model,
                            void *context) {
    (void)context;
    if (!model) return -1;

    switch (action.type) {
    case CM_UI_ACTION_REQUEST_LINK:
        model->status_text =
            "VINCULACION REAL: FALTA INTEGRAR EL SERVICIO DE SESION";
        break;
    case CM_UI_ACTION_PLAY_GAME:
        model->status_text =
            "STREAMING AUN NO DISPONIBLE EN ESTA VERSION DE PRUEBA";
        break;
    case CM_UI_ACTION_OPEN_LIBRARY:
        model->status_text =
            "EL CATALOGO SE MOSTRARA AL VINCULAR UNA SESION REAL";
        break;
    case CM_UI_ACTION_BACK:
    case CM_UI_ACTION_NONE:
    default:
        break;
    }
    return 0;
}

int main(void) {
    cm_ui_model_t model;
    int32_t user_id = -1;
    int rc;

    memset(&model, 0, sizeof(model));
    model.account_label = "CUENTA NO VINCULADA";
    model.status_text = "VINCULA TU SESION CLOUDMOON PARA CONTINUAR";

    /* Load the system services used by the dashboard before opening devices. */
    (void)sceSysmoduleLoadModuleInternal(
        ORBIS_SYSMODULE_INTERNAL_USER_SERVICE);
    (void)sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_PAD);
    (void)sceSysmoduleLoadModuleInternal(
        ORBIS_SYSMODULE_INTERNAL_VIDEO_OUT);

    rc = sceUserServiceInitialize(NULL);
    if (rc < 0 && sceUserServiceGetInitialUser(&user_id) < 0) {
        sceKernelExitProcess(1);
        return 1;
    }

    rc = cm_orbis_dashboard_run(&model, handle_ui_action, NULL);
    sceKernelExitProcess(rc == 0 ? 0 : 1);
    return rc == 0 ? 0 : 1;
}
