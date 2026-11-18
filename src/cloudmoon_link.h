#ifndef CLOUDMOON_LINK_H
#define CLOUDMOON_LINK_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CM_LINK_GAME_CAP 192
#define CM_LINK_USER_CAP 192
#define CM_LINK_INSTANCE_CAP 3072

typedef struct {
    char game[CM_LINK_GAME_CAP];
    char user_id[CM_LINK_USER_CAP];
    char android_instance_id[CM_LINK_INSTANCE_CAP];
} cm_cloudmoon_link_t;

/*
 * Parses a CloudMoon browser session URL.
 * The URL is processed locally; this function never performs network I/O.
 * Returns 0 when game, userid and android_instance_id are present and valid
 * as URL components, otherwise -1.
 */
int cm_cloudmoon_parse_session_url(const char *url, cm_cloudmoon_link_t *link);

#ifdef __cplusplus
}
#endif

#endif
