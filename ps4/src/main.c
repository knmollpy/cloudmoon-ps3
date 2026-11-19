#include "platform/orbis_dashboard.h"
#include "platform/orbis_network.h"
#include "account/cloudmoon_pairing.h"

#include <orbis/Sysmodule.h>
#include <orbis/UserService.h>
#include <orbis/libkernel.h>

#include <string.h>

#define CM_CA_BUNDLE_PATH "/app0/assets/cacert.pem"

typedef struct {
    cm_pairing_service_t *pairing;
    cm_account_session_t account;
    int account_linked;
} cm_app_context_t;

static int handle_ui_action(cm_ui_action_t action,
                            cm_ui_model_t *model,
                            void *context) {
    cm_app_context_t *app = (cm_app_context_t *)context;
    if (!model || !app) return -1;

    switch (action.type) {
    case CM_UI_ACTION_REQUEST_LINK:
        if (app->account_linked) {
            model->status_text = "TU CUENTA CLOUDMOON YA ESTA VINCULADA";
        } else if (app->pairing) {
            model->status_text = cm_pairing_status(app->pairing);
        } else {
            model->status_text = "NO SE PUDO INICIAR EL SERVICIO DE VINCULACION";
        }
        break;
    case CM_UI_ACTION_PLAY_GAME:
        model->status_text =
            "CUENTA VINCULADA. EL MOTOR DE STREAMING SIGUE EN INTEGRACION.";
        break;
    case CM_UI_ACTION_OPEN_LIBRARY:
        model->status_text = app->account_linked
            ? "CUENTA VALIDADA; CATALOGO EN PREPARACION"
            : "VINCULA TU SESION PARA CARGAR EL CATALOGO";
        break;
    case CM_UI_ACTION_BACK:
    case CM_UI_ACTION_NONE:
    default:
        break;
    }
    return 0;
}

static int update_dashboard(cm_ui_model_t *model, void *context) {
    cm_app_context_t *app = (cm_app_context_t *)context;
    const cm_qr_t *qr;

    if (!app || !model) return -1;
    if (!app->pairing) return 0;

    if (!app->account_linked &&
        cm_pairing_take_account(app->pairing, &app->account) == 1) {
        app->account_linked = 1;
        model->session_linked = 1;
        model->account_label = app->account.email[0]
            ? app->account.email : "CUENTA CLOUDMOON VINCULADA";
        model->status_text = "SESION VALIDADA CON CLOUDMOON";
        model->qr_modules = NULL;
        model->qr_size = 0;
        model->link_url = NULL;
        return 0;
    }

    if (!app->account_linked) {
        qr = cm_pairing_qr(app->pairing);
        model->status_text = cm_pairing_status(app->pairing);
        model->link_url = cm_pairing_url(app->pairing);
        if (qr) {
            model->qr_modules = qr->modules;
            model->qr_size = qr->size;
        }
    }
    return 0;
}

int main(void) {
    cm_ui_model_t model;
    cm_app_context_t app;
    int32_t user_id = -1;
    int rc;

    memset(&model, 0, sizeof(model));
    memset(&app, 0, sizeof(app));
    model.account_label = "CUENTA NO VINCULADA";
    model.status_text = "INICIANDO RED Y VINCULACION CLOUDMOON";

    /* Load the system services used by the dashboard and pairing adapter. */
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

    rc = cm_pairing_start(&app.pairing, CM_CA_BUNDLE_PATH, 8);
    if (rc != 0) {
        model.status_text = "RED NO DISPONIBLE: REVISA WIFI Y CERTIFICADOS";
    } else {
        model.status_text = cm_pairing_status(app.pairing);
        model.link_url = cm_pairing_url(app.pairing);
    }

    rc = cm_orbis_dashboard_run(&model, handle_ui_action,
                                update_dashboard, &app);

    if (app.pairing) {
        cm_pairing_stop(app.pairing);
        app.pairing = NULL;
    }
    cm_orbis_network_shutdown();

    sceKernelExitProcess(rc == 0 ? 0 : 1);
    return rc == 0 ? 0 : 1;
}
