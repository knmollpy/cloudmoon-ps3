#include "cloudmoon_orbis_game.h"

#include "cloudmoon_orbis_connection.h"
#include "../platform/cloudmoon_orbis_webrtc.h"
#include "../catalog/cloudmoon_catalog.h"

#include <string.h>

typedef struct {
    cm_orbis_webrtc_t *webrtc;
    int failed;
} cm_orbis_game_context_t;

static void cm_orbis_game_signal(void *context,
                                 cm_cloudmoon_client_t *client,
                                 const char *json, size_t length) {
    cm_orbis_game_context_t *game = (cm_orbis_game_context_t *)context;

    if (!game)
        return;

    /* cm_cloudmoon_client_run() invokes the signal callback with a NULL
       packet before it frees its WebSocket transmit queue and TLS context.
       Stop every RTC sender here, while queued sends are still safe. */
    if (!client || !json || length == 0) {
        if (game->webrtc) {
            cm_orbis_webrtc_close(game->webrtc);
            game->webrtc = NULL;
        }
        return;
    }
    if (game->failed)
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
    cm_account_session_t prepared;
    int result;

    if (!account || !game_package || !game_package[0])
        return -1;
    memcpy(&prepared, account, sizeof(prepared));
    if (cm_account_session_prepare_game(ca_bundle_path, timeout_seconds,
                                        &prepared, game_package) != 0) {
        memset(&prepared, 0, sizeof(prepared));
        return -3;
    }

    memset(&game, 0, sizeof(game));
    result = cm_orbis_cloudmoon_run_account(&prepared, game_package,
                                            ca_bundle_path,
                                            timeout_seconds,
                                            cm_orbis_game_signal, &game);

    if (game.webrtc) {
        cm_orbis_webrtc_close(game.webrtc);
        game.webrtc = NULL;
    }
    memset(&prepared, 0, sizeof(prepared));
    return game.failed ? -2 : result;
}
