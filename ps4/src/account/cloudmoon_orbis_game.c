#include "cloudmoon_orbis_game.h"

#include "cloudmoon_orbis_connection.h"
#include "../platform/cloudmoon_orbis_webrtc.h"

#include <string.h>

typedef struct {
    cm_orbis_webrtc_t *webrtc;
    int failed;
} cm_orbis_game_context_t;

static void cm_orbis_game_signal(void *context,
                                 cm_cloudmoon_client_t *client,
                                 const char *json, size_t length) {
    cm_orbis_game_context_t *game = (cm_orbis_game_context_t *)context;

    if (!game || game->failed || !client || !json || length == 0)
        return;

    if (!game->webrtc) {
        if (cm_orbis_webrtc_create(&game->webrtc, client, 1280, 720) != 0) {
            game->failed = 1;
            cm_cloudmoon_client_request_stop();
            return;
        }
    }

    cm_orbis_webrtc_signal(game->webrtc, client, json, length);
}

int cm_orbis_cloudmoon_play_account(const cm_account_session_t *account,
                                    const char *game_package,
                                    const char *ca_bundle_path,
                                    unsigned timeout_seconds) {
    cm_orbis_game_context_t game;
    int result;

    memset(&game, 0, sizeof(game));
    result = cm_orbis_cloudmoon_run_account(account, game_package,
                                            ca_bundle_path,
                                            timeout_seconds,
                                            cm_orbis_game_signal, &game);

    if (game.webrtc) {
        cm_orbis_webrtc_close(game.webrtc);
        game.webrtc = NULL;
    }
    return game.failed ? -2 : result;
}
