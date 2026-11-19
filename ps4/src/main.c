#include "platform/orbis_dashboard.h"
#include "platform/orbis_network.h"

#include "account/cloudmoon_pairing.h"
#include "account/cloudmoon_orbis_game.h"
#include "catalog/cloudmoon_catalog.h"

#include <orbis/Sysmodule.h>
#include <orbis/UserService.h>
#include <orbis/libkernel.h>

#include <stdio.h>
#include <string.h>

#define CM_CA_BUNDLE_PATH "/app0/assets/misc/cacert.pem"
#define CM_API_TIMEOUT_SECONDS 12
#define CM_STREAM_TIMEOUT_SECONDS 20

typedef struct {
    cm_catalog_t catalog;
    const char *game_names[CM_CATALOG_MAX_GAMES];
    cm_account_session_t account;
    cm_pairing_service_t *pairing;
    char api_host[128];
    char status[192];
    char account_label[321];
    int catalog_loaded;
    int session_linked;
    int play_pending;
    int play_index;
} cm_app_context_t;

static int load_catalog(cm_app_context_t *app, cm_ui_model_t *model) {
    int count, i;
    if (!app || !model) return -1;
    if (app->catalog_loaded) return 0;

    (void)snprintf(app->status, sizeof(app->status),
                   "Consultando el catalogo oficial de CloudMoon...");
    model->status_text = app->status;
    count = cm_catalog_fetch(CM_CA_BUNDLE_PATH, CM_API_TIMEOUT_SECONDS,
                             &app->catalog, app->api_host,
                             sizeof(app->api_host));
    if (count < 0) {
        (void)snprintf(app->status, sizeof(app->status),
                       "No se pudo cargar el catalogo. Revisa la conexion de PS4.");
        return -1;
    }

    for (i = 0; i < count; ++i)
        app->game_names[i] = app->catalog.games[i].title;
    app->catalog_loaded = 1;
    model->games = app->game_names;
    model->game_count = count;
    (void)snprintf(app->status, sizeof(app->status),
                   "%d juegos activos recibidos desde CloudMoon.", count);
    model->status_text = app->status;
    return 0;
}

static void clear_pairing_view(cm_ui_model_t *model) {
    if (!model) return;
    model->qr_modules = NULL;
    model->qr_size = 0;
    model->link_url = NULL;
    model->link_code = NULL;
}

static int start_pairing(cm_app_context_t *app, cm_ui_model_t *model) {
    int rc;
    if (!app || !model) return -1;

    if (app->session_linked) {
        (void)snprintf(app->status, sizeof(app->status),
                       "Cuenta conectada: %.110s", app->account.email);
        model->status_text = app->status;
        return 0;
    }
    if (!app->pairing) {
        rc = cm_pairing_start(&app->pairing, CM_CA_BUNDLE_PATH,
                              CM_API_TIMEOUT_SECONDS);
        if (rc != 0) {
            (void)snprintf(app->status, sizeof(app->status),
                           "No se pudo abrir el enlace local. Comprueba que la PS4 este conectada al Wi-Fi.");
            model->status_text = app->status;
            return 0;
        }
    }

    {
        const cm_qr_t *qr = cm_pairing_qr(app->pairing);
        if (qr) {
            model->qr_modules = qr->modules;
            model->qr_size = qr->size;
        }
    }
    model->link_url = cm_pairing_url(app->pairing);
    model->link_code = NULL;
    (void)snprintf(app->status, sizeof(app->status), "%s",
                   cm_pairing_status(app->pairing));
    model->status_text = app->status;
    return 0;
}

static int handle_ui_action(cm_ui_action_t action,
                            cm_ui_model_t *model,
                            void *context) {
    cm_app_context_t *app = (cm_app_context_t *)context;
    if (!app || !model) return -1;

    switch (action.type) {
    case CM_UI_ACTION_REQUEST_LINK:
        return start_pairing(app, model);

    case CM_UI_ACTION_OPEN_LIBRARY:
        (void)load_catalog(app, model);
        return 0;

    case CM_UI_ACTION_PLAY_GAME:
        if (!app->session_linked) {
            (void)snprintf(app->status, sizeof(app->status),
                           "Primero vincula tu cuenta en la seccion VINCULAR.");
            model->status_text = app->status;
            return 0;
        }
        if (action.game_index < 0 ||
            (size_t)action.game_index >= app->catalog.count) {
            (void)snprintf(app->status, sizeof(app->status),
                           "Seleccion de juego no valida.");
            model->status_text = app->status;
            return 0;
        }
        app->play_index = action.game_index;
        app->play_pending = 1;
        return 1; /* Close dashboard devices before starting the video stream. */

    case CM_UI_ACTION_BACK:
    case CM_UI_ACTION_NONE:
    default:
        return 0;
    }
}

static int dashboard_tick(cm_ui_model_t *model, void *context) {
    cm_app_context_t *app = (cm_app_context_t *)context;
    cm_account_session_t account;

    if (!app || !model) return -1;
    if (app->pairing) {
        if (cm_pairing_take_account(app->pairing, &account)) {
            cm_pairing_stop(app->pairing);
            app->pairing = NULL;
            memcpy(&app->account, &account, sizeof(account));
            app->session_linked = 1;
            (void)snprintf(app->account_label, sizeof(app->account_label),
                           "%s", app->account.email);
            model->session_linked = 1;
            model->account_label = app->account_label;
            clear_pairing_view(model);
            (void)snprintf(app->status, sizeof(app->status),
                           "Cuenta conectada. Ya puedes abrir un juego del catalogo.");
            model->status_text = app->status;
        } else {
            (void)snprintf(app->status, sizeof(app->status), "%s",
                           cm_pairing_status(app->pairing));
            model->status_text = app->status;
        }
    }
    return 0;
}

static void app_run(void) {
    /* The catalog contains up to 512 fixed-size records; keep it out of the
       small Orbis main-thread stack. */
    static cm_app_context_t app;
    cm_ui_model_t model;
    int32_t user_id = -1;
    int user_rc;

    memset(&app, 0, sizeof(app));
    memset(&model, 0, sizeof(model));
    app.play_index = -1;

    (void)sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_USER_SERVICE);
    (void)sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_PAD);
    (void)sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_VIDEO_OUT);

    user_rc = sceUserServiceInitialize(NULL);
    if (user_rc < 0 && sceUserServiceGetInitialUser(&user_id) < 0) {
        for (;;) sceKernelUsleep(1000000);
    }

    if (cm_orbis_network_init() != 0) {
        (void)snprintf(app.status, sizeof(app.status),
                       "No se pudo inicializar la red de PS4.");
    } else {
        (void)snprintf(app.status, sizeof(app.status),
                       "Listo. Abre el catalogo o vincula tu cuenta.");
    }

    model.account_label = "CUENTA NO VINCULADA";
    model.status_text = app.status;
    model.session_linked = 0;

    for (;;) {
        int rc = cm_orbis_dashboard_run(&model, handle_ui_action,
                                        dashboard_tick, &app);
        if (rc == 1 && app.play_pending) {
            const cm_catalog_game_t *game =
                &app.catalog.games[app.play_index];
            int stream_rc;

            app.play_pending = 0;
            (void)snprintf(app.status, sizeof(app.status),
                           "Conectando con %s...", game->title);
            stream_rc = cm_orbis_cloudmoon_play_account(
                &app.account, game->package_name,
                CM_CA_BUNDLE_PATH, CM_STREAM_TIMEOUT_SECONDS);

            (void)snprintf(app.status, sizeof(app.status),
                           stream_rc == 0 ? "La sesion de %s termino." :
                           "No se pudo iniciar %s. Comprueba la sesion de CloudMoon.",
                           game->title);
            model.status_text = app.status;
            continue;
        }

        (void)snprintf(app.status, sizeof(app.status),
                       "Dashboard reiniciado. Codigo: %d", rc);
        model.status_text = app.status;
        sceKernelUsleep(500000);
    }
}

int main(void) {
    app_run();
    for (;;)
        sceKernelUsleep(1000000);
    return 0;
}
