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
    int server_id;
    char language[8];
    char locale[8];
    char device_id[37];
} cm_account_session_t;

/* Legacy sid parser retained for compatibility tests; the PS4 pairing flow now
   authenticates with CloudMoon's official Google or email/password API. */
int cm_account_session_parse_url(const char *url, cm_account_session_t *out);

/* Legacy sid exchange retained for compatibility; not used by QR pairing. */
int cm_account_session_fetch(const char *ca_bundle_path,
                             unsigned timeout_seconds,
                             cm_account_session_t *account);

#endif
