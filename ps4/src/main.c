#include "platform/orbis_dashboard.h"
#include "platform/orbis_network.h"

#include <orbis/ImeDialog.h>
#include <orbis/CommonDialog.h>
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
    int32_t psn_user_id;
    char api_host[128];
    char status[192];
    char account_label[321];
    char login_email[321];
    char login_password[1025];
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

static int cm_ime_text_to_utf8(const uint16_t *source,
                              char *output, size_t output_capacity) {
    size_t used = 0;
    size_t i = 0;

    if (!source || !output || output_capacity == 0) return -1;
    while (source[i] != 0) {
        uint32_t cp = source[i++];
        if (cp >= 0xd800u && cp <= 0xdbffu) {
            uint32_t low = source[i];
            if (low < 0xdc00u || low > 0xdfffu) return -1;
            ++i;
            cp = 0x10000u + ((cp - 0xd800u) << 10) + (low - 0xdc00u);
        } else if (cp >= 0xdc00u && cp <= 0xdfffu) {
            return -1;
        }

        if (cp < 0x20u || cp == 0x7fu || cp > 0x10ffffu) return -1;
        if (cp <= 0x7fu) {
            if (used + 1u >= output_capacity) return -1;
            output[used++] = (char)cp;
        } else if (cp <= 0x7ffu) {
            if (used + 2u >= output_capacity) return -1;
            output[used++] = (char)(0xc0u | (cp >> 6));
            output[used++] = (char)(0x80u | (cp & 0x3fu));
        } else if (cp <= 0xffffu) {
            if (used + 3u >= output_capacity) return -1;
            output[used++] = (char)(0xe0u | (cp >> 12));
            output[used++] = (char)(0x80u | ((cp >> 6) & 0x3fu));
            output[used++] = (char)(0x80u | (cp & 0x3fu));
        } else {
            if (used + 4u >= output_capacity) return -1;
            output[used++] = (char)(0xf0u | (cp >> 18));
            output[used++] = (char)(0x80u | ((cp >> 12) & 0x3fu));
            output[used++] = (char)(0x80u | ((cp >> 6) & 0x3fu));
            output[used++] = (char)(0x80u | (cp & 0x3fu));
        }
    }
    output[used] = 0;
    return used ? 0 : -1;
}

static int cm_read_ime_text(uint32_t user_id, OrbisImeType type,
                            const uint16_t *title, uint32_t max_text_length,
                            const char *initial,
                            char *output, size_t output_capacity) {
    OrbisImeDialogSetting setting;
    OrbisDialogResult result;
    uint16_t input[1025];
    int status, rc, polls = 0;

    if (!output || output_capacity < 2 || max_text_length == 0 ||
        max_text_length > 1024)
        return -1;
    memset(input, 0, sizeof(input));
    if (initial) {
        size_t i;
        for (i = 0; initial[i] && i < max_text_length && i < 1024; ++i) {
            unsigned char ch = (unsigned char)initial[i];
            if (ch > 0x7fu) break;
            input[i] = (uint16_t)ch;
        }
    }
    output[0] = 0;
    memset(&setting, 0, sizeof(setting));
    memset(&result, 0, sizeof(result));

    setting.userId = user_id;
    setting.type = type;
    setting.supportedLanguages = 0;
    setting.enterLabel = ORBIS_BUTTON_LABEL_DEFAULT;
    setting.inputMethod = ORBIS__DEFAULT;
    setting.option = (type == ORBIS_TYPE_BASIC_LATIN) ? 4u : 0u; /* ORBIS_IME_OPTION_PASSWORD */
    setting.maxTextLength = max_text_length;
    setting.inputTextBuffer = (wchar_t *)(void *)input;
    setting.posx = 0.0f;
    setting.posy = 0.0f;
    setting.horizontalAlignment = ORBIS_H_LEFT;
    setting.verticalAlignment = ORBIS_V_CENTER;
    setting.placeholder = NULL;
    setting.title = (const wchar_t *)(const void *)title;

    rc = sceImeDialogInit(&setting, NULL);
    if (rc < 0) {
        memset(input, 0, sizeof(input));
        return -1;
    }

    do {
        status = sceImeDialogGetStatus();
        if (status != ORBIS_DIALOG_STATUS_RUNNING) break;
        sceKernelUsleep(20000);
    } while (++polls < 30000);

    if (status == ORBIS_DIALOG_STATUS_RUNNING) {
        (void)sceImeDialogAbort();
        for (polls = 0; polls < 1000; ++polls) {
            status = sceImeDialogGetStatus();
            if (status != ORBIS_DIALOG_STATUS_RUNNING) break;
            sceKernelUsleep(10000);
        }
    }

    rc = sceImeDialogGetResult(&result);
    (void)sceImeDialogTerm();
    if (rc < 0 || result.endstatus != ORBIS_DIALOG_OK) {
        memset(input, 0, sizeof(input));
        return -1;
    }

    rc = cm_ime_text_to_utf8(input, output, output_capacity);
    memset(input, 0, sizeof(input));
    if (rc != 0) memset(output, 0, output_capacity);
    return rc;
}

static int edit_login_field(cm_app_context_t *app,
                             cm_ui_model_t *model, int password_field) {
    static const uint16_t email_title[] = {
        'G','m','a','i','l',' ','d','e',' ','C','l','o','u','d','M','o','o','n',0
    };
    static const uint16_t password_title[] = {
        'C','o','n','t','r','a','s','e','n','a',' ','d','e',' ','C','l','o','u','d','M','o','o','n',0
    };
    int rc;
    if (!app || !model || app->session_linked) return -1;

    if (password_field) {
        rc = cm_read_ime_text((uint32_t)app->psn_user_id,
                              ORBIS_TYPE_BASIC_LATIN, password_title, 256,
                              app->login_password, app->login_password,
                              sizeof(app->login_password));
        if (rc == 0)
            (void)snprintf(app->status, sizeof(app->status),
                           "Contrasena guardada. Selecciona INICIAR SESION.");
        else
            (void)snprintf(app->status, sizeof(app->status),
                           "No se modifico la contrasena.");
    } else {
        rc = cm_read_ime_text((uint32_t)app->psn_user_id,
                              ORBIS_TYPE_MAIL, email_title, 256,
                              app->login_email, app->login_email,
                              sizeof(app->login_email));
        if (rc == 0)
            (void)snprintf(app->status, sizeof(app->status),
                           "Gmail guardado. Selecciona el campo contrasena.");
        else
            (void)snprintf(app->status, sizeof(app->status),
                           "No se modifico el Gmail.");
    }
    model->login_email = app->login_email;
    model->login_password_set = app->login_password[0] != 0;
    model->status_text = app->status;
    return 0;
}

static int start_password_login(cm_app_context_t *app,
                                cm_ui_model_t *model) {
    cm_account_session_t authenticated;
    int rc;

    if (!app || !model) return -1;
    if (app->session_linked) {
        (void)snprintf(app->status, sizeof(app->status),
                       "Sesion activa: %.110s", app->account.email);
        model->status_text = app->status;
        return 0;
    }
    if (!app->login_email[0] || !app->login_password[0]) {
        (void)snprintf(app->status, sizeof(app->status),
                       "Completa Gmail y contrasena antes de iniciar sesion.");
        model->status_text = app->status;
        model->login_email = app->login_email;
        model->login_password_set = app->login_password[0] != 0;
        return 0;
    }

    memset(&authenticated, 0, sizeof(authenticated));
    (void)snprintf(app->status, sizeof(app->status),
                   "Iniciando sesion en CloudMoon...");
    model->status_text = app->status;
    rc = cm_account_session_authenticate(
        CM_CA_BUNDLE_PATH, CM_API_TIMEOUT_SECONDS,
        "password", NULL, app->login_email, app->login_password,
        21, "en", "AR", &authenticated);

    memset(app->login_password, 0, sizeof(app->login_password));
    model->login_password_set = 0;

    if (rc != 0) {
        memset(&authenticated, 0, sizeof(authenticated));
        (void)snprintf(app->status, sizeof(app->status),
                       "No se pudo iniciar sesion. Revisa Gmail, contrasena o conexion.");
        model->status_text = app->status;
        model->login_email = app->login_email;
        return 0;
    }

    memcpy(&app->account, &authenticated, sizeof(app->account));
    memset(&authenticated, 0, sizeof(authenticated));
    app->session_linked = 1;
    app->catalog_loaded = 0;
    memset(app->login_email, 0, sizeof(app->login_email));
    (void)snprintf(app->account_label, sizeof(app->account_label),
                   "%s", app->account.email);
    model->session_linked = 1;
    model->account_label = app->account_label;
    model->login_email = "";
    (void)snprintf(app->status, sizeof(app->status),
                   "Sesion iniciada. Ya puedes abrir el catalogo de CloudMoon.");
    model->status_text = app->status;
    return 0;
}

static int handle_ui_action(cm_ui_action_t action,
                            cm_ui_model_t *model,
                            void *context) {
    cm_app_context_t *app = (cm_app_context_t *)context;
    if (!app || !model) return -1;

    switch (action.type) {
    case CM_UI_ACTION_EDIT_EMAIL:
        return edit_login_field(app, model, 0);

    case CM_UI_ACTION_EDIT_PASSWORD:
        return edit_login_field(app, model, 1);

    case CM_UI_ACTION_LOGIN:
        return start_password_login(app, model);

    case CM_UI_ACTION_REQUEST_LINK:
        model->login_email = app->login_email;
        model->login_password_set = app->login_password[0] != 0;
        return 0;

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
    (void)sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_COMMON_DIALOG);
    (void)sceCommonDialogInitialize();
    (void)sceSysmoduleLoadModule(ORBIS_SYSMODULE_LIBIME);
    (void)sceSysmoduleLoadModule(ORBIS_SYSMODULE_IME_DIALOG);

    user_rc = sceUserServiceInitialize(NULL);
    (void)user_rc;
    if (sceUserServiceGetInitialUser(&user_id) < 0 || user_id < 0) {
        for (;;) sceKernelUsleep(1000000);
    }
    app.psn_user_id = user_id;

    if (cm_orbis_network_init() != 0) {
        (void)snprintf(app.status, sizeof(app.status),
                       "No se pudo inicializar la red de PS4.");
    } else {
        (void)snprintf(app.status, sizeof(app.status),
                       "Listo. Abre el catalogo o vincula tu cuenta.");
    }

    model.account_label = "CUENTA NO VINCULADA";
    model.login_email = app.login_email;
    model.login_password_set = 0;
    model.status_text = app.status;
    model.session_linked = 0;

    for (;;) {
        int rc = cm_orbis_dashboard_run(&model, handle_ui_action,
                                        NULL, &app);
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
