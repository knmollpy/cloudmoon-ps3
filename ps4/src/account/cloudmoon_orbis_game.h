#ifndef CLOUDMOON_ORBIS_GAME_H
#define CLOUDMOON_ORBIS_GAME_H

#include "../account/cloudmoon_session_link.h"

/* Blocking game session runner: CloudMoon coordinator -> WebRTC -> PS4 media. */
int cm_orbis_cloudmoon_play_account(const cm_account_session_t *account,
                                    const char *game_package,
                                    const char *ca_bundle_path,
                                    unsigned timeout_seconds);

#endif
