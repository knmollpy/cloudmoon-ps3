#ifndef CLOUDMOON_CATALOG_H
#define CLOUDMOON_CATALOG_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;
    const char *package_id;
    const char *icon_file;
} cm_cloudmoon_game_t;

/* Public games and package IDs read from CloudMoon's own catalog artwork URLs. */
size_t cm_cloudmoon_catalog_count(void);
const cm_cloudmoon_game_t *cm_cloudmoon_catalog_at(size_t index);
const cm_cloudmoon_game_t *cm_cloudmoon_catalog_find_package(const char *package_id);

#ifdef __cplusplus
}
#endif
#endif
