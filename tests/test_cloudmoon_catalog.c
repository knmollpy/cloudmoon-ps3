#include "../src/cloudmoon_catalog.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    size_t i, count = cm_cloudmoon_catalog_count();
    if (count < 30 || !cm_cloudmoon_catalog_at(0)) return 1;
    if (strcmp(cm_cloudmoon_catalog_at(0)->name, "Roblox") != 0) return 2;
    if (!cm_cloudmoon_catalog_find_package("com.roblox.client")) return 3;
    if (!cm_cloudmoon_catalog_find_package("com.epicgames.fortnite")) return 4;
    if (!cm_cloudmoon_catalog_find_package("com.supercell.brawlstars")) return 5;
    if (cm_cloudmoon_catalog_find_package("com.not.a.real.cloudmoon.game")) return 6;
    for (i = 0; i < count; ++i) {
        const cm_cloudmoon_game_t *g = cm_cloudmoon_catalog_at(i);
        if (!g || !g->name || !g->name[0] ||
            !g->package_id || !g->package_id[0] ||
            !g->icon_file || !g->icon_file[0]) return 7;
    }
    puts("PASS: CloudMoon public catalog entries and package identifiers");
    return 0;
}
