#ifndef CLOUDMOON_SESSION_LINK_H
#define CLOUDMOON_SESSION_LINK_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    char sid[513];
    char token[1025];
    char email[321];
    char game[129];
    char user_id[129];
    char android_instance_id[2049];
    char coordinator_host[256];
    uint16_t coordinator_port;
    char coordinator_path[128];
    char api_host[128];
} cm_account_session_t;

/* Accept only a CloudMoon HTTPS session URL containing a unique sid parameter.
   The sid is exchanged with the real CloudMoon API before the account is used. */
int cm_account_session_parse_url(const char *url, cm_account_session_t *out);

/* Exchange the short-lived sid with CloudMoon /web/sid and resolve the
   coordinator. No passwords or Google credentials are collected by the PS4. */
int cm_account_session_fetch(const char *ca_bundle_path,
                             unsigned timeout_seconds,
                             cm_account_session_t *account);

#endif
