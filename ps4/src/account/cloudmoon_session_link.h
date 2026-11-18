#ifndef CLOUDMOON_SESSION_LINK_H
#define CLOUDMOON_SESSION_LINK_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    char token[1025];
    char email[321];
    char game[129];
    char user_id[129];
    char android_instance_id[2049];
    char coordinator_host[256];
    uint16_t coordinator_port;
    char coordinator_path[128];
} cm_account_session_t;

/* Accepts only an HTTPS web.cloudmoonapp.com session URL. The parser extracts
   the existing authenticated browser session; it never accepts passwords. */
int cm_account_session_parse_url(const char *url, cm_account_session_t *out);

#endif
