#ifndef CLOUDMOON_CATALOG_H
#define CLOUDMOON_CATALOG_H

#include <stddef.h>
#include "../account/cloudmoon_session_link.h"

#define CM_CATALOG_MAX_GAMES 512
#define CM_CATALOG_TITLE_MAX 128
#define CM_CATALOG_PACKAGE_MAX 192
#define CM_CATALOG_ICON_MAX 512
#define CM_CATALOG_MAINTENANCE_MAX 160

typedef struct {
    char title[CM_CATALOG_TITLE_MAX];
    char package_name[CM_CATALOG_PACKAGE_MAX];
    char icon_url[CM_CATALOG_ICON_MAX];
    char maintenance_info[CM_CATALOG_MAINTENANCE_MAX];
    int status;
    int is_pc;
    int is_beta;
    int need_login_google;
    int min_vip_level;
} cm_catalog_game_t;

typedef struct {
    cm_catalog_game_t games[CM_CATALOG_MAX_GAMES];
    size_t count;
} cm_catalog_t;

/* Parse the public CloudMoon /game/list JSON response. Only active mobile
   packages with a title and package_name are kept. */
int cm_catalog_parse_json(const char *json, size_t length,
                          cm_catalog_t *catalog);

/* Fetch live catalogue from the API host discovery list used by CloudMoon web.
   The selected API host is returned to allow subsequent requests to reuse it. */
int cm_catalog_fetch(const char *ca_bundle_path, unsigned timeout_seconds,
                     cm_catalog_t *catalog, char *api_host,
                     size_t api_host_capacity);

/* Official CloudMoon web-account API flow. Credentials are submitted from
   the PS4 to api.prod.cloudmoonapp.com over verified TLS. */
int cm_account_session_authenticate(const char *ca_bundle_path,
                                    unsigned timeout_seconds,
                                    const char *provider,
                                    const char *google_id_token,
                                    const char *email,
                                    const char *password,
                                    int server_id,
                                    const char *language,
                                    const char *locale,
                                    cm_account_session_t *account);
/* Validate a CloudMoon bearer token obtained from the official logged-in web client. */
int cm_account_session_import_token(const char *ca_bundle_path,
                                    unsigned timeout_seconds,
                                    const char *token,
                                    const char *user_id,
                                    int server_id,
                                    const char *language,
                                    const char *locale,
                                    cm_account_session_t *account);
int cm_account_session_prepare_game(const char *ca_bundle_path,
                                    unsigned timeout_seconds,
                                    cm_account_session_t *account,
                                    const char *game_package);

#endif
