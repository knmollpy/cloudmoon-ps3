#ifndef CLOUDMOON_CATALOG_H
#define CLOUDMOON_CATALOG_H

#include <stddef.h>

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

#endif
