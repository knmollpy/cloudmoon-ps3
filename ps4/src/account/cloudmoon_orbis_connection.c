#include "cloudmoon_orbis_connection.h"

#include "../net/cloudmoon_random_mbedtls.h"

#include <string.h>

static int cm_account_is_ready(const cm_account_session_t *account) {
    if (!account || !account->token[0] || !account->email[0] ||
        !account->game[0] || !account->user_id[0] ||
        !account->android_instance_id[0] ||
        !account->coordinator_host[0] ||
        account->coordinator_port == 0 ||
        account->coordinator_path[0] != '/')
        return 0;
    return 1;
}

int cm_orbis_cloudmoon_run_account(const cm_account_session_t *account,
                                   const char *ca_bundle_path,
                                   unsigned timeout_seconds,
                                   cm_cloudmoon_signal_fn on_signal,
                                   void *signal_context) {
    cm_cloudmoon_registration_t registration;

    if (!cm_account_is_ready(account) || !ca_bundle_path ||
        !ca_bundle_path[0] || timeout_seconds == 0)
        return -1;

    if (cm_orbis_random_init() != 0)
        return -2;

    memset(&registration, 0, sizeof(registration));
    registration.width = 1280;
    registration.height = 720;
    registration.game = account->game;
    registration.user_id = account->user_id;
    registration.instance_id = account->android_instance_id;

    cm_cloudmoon_client_reset_stop();
    return cm_cloudmoon_client_run(account->coordinator_host,
                                   account->coordinator_port,
                                   account->coordinator_path,
                                   ca_bundle_path,
                                   timeout_seconds,
                                   &registration,
                                   cm_orbis_random_source,
                                   NULL,
                                   on_signal,
                                   signal_context);
}

void cm_orbis_cloudmoon_request_stop(void) {
    cm_cloudmoon_client_request_stop();
}
