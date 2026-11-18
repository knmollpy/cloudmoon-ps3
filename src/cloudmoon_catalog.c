#include "cloudmoon_catalog.h"

#include <string.h>

#define CLOUDMOON_ICON_PREFIX "game-icons/"

static const cm_cloudmoon_game_t cm_games[] = {
    {"Roblox", "com.roblox.client", CLOUDMOON_ICON_PREFIX "com.roblox.client_20261103.webp"},
    {"Google Chrome", "com.android.chrome", CLOUDMOON_ICON_PREFIX "com.android.chrome_20261103.webp"},
    {"Clash Royale", "com.supercell.clashroyale", CLOUDMOON_ICON_PREFIX "com.supercell.clashroyale_20261103.webp"},
    {"Brawl Stars", "com.supercell.brawlstars", CLOUDMOON_ICON_PREFIX "com.supercell.brawlstars_20261103.webp"},
    {"TikTok", "com.zhiliaoapp.musically", CLOUDMOON_ICON_PREFIX "com.zhiliaoapp.musically_20261103.webp"},
    {"Fortnite", "com.epicgames.fortnite", CLOUDMOON_ICON_PREFIX "com.epicgames.fortnite_20261103.webp"},
    {"YouTube", "com.google.android.youtube", CLOUDMOON_ICON_PREFIX "com.google.android.youtube_20261103.webp"},
    {"Instagram", "com.instagram.android", CLOUDMOON_ICON_PREFIX "com.instagram.android_20261103.webp"},
    {"EA SPORTS FC Mobile 27", "com.ea.gp.fifamobile", CLOUDMOON_ICON_PREFIX "com.ea.gp.fifamobile_20261103.webp"},
    {"Discord", "com.discord", CLOUDMOON_ICON_PREFIX "com.discord_20261103.webp"},
    {"eFootball", "jp.konami.pesam", CLOUDMOON_ICON_PREFIX "jp.konami.pesam_20261103.webp"},
    {"PolyBuzz", "ai.socialapps.speakmaster", CLOUDMOON_ICON_PREFIX "ai.socialapps.speakmaster_20261103.webp"},
    {"CookieRun: Kingdom", "com.devsisters.ck", CLOUDMOON_ICON_PREFIX "com.devsisters.ck_20261103.webp"},
    {"Among Us", "com.innersloth.spacemafia", CLOUDMOON_ICON_PREFIX "com.innersloth.spacemafia_20261103.webp"},
    {"Genshin Impact", "com.miHoYo.GenshinImpact", CLOUDMOON_ICON_PREFIX "com.miHoYo.GenshinImpact_20261103.webp"},
    {"Limbus Company", "com.ProjectMoon.LimbusCompany", CLOUDMOON_ICON_PREFIX "com.ProjectMoon.LimbusCompany_20261103.webp"},
    {"Clash of Clans", "com.supercell.clashofclans", CLOUDMOON_ICON_PREFIX "com.supercell.clashofclans_20261103.webp"},
    {"Madden NFL Mobile", "com.ea.gp.maddennfl21mobile", CLOUDMOON_ICON_PREFIX "com.ea.gp.maddennfl21mobile_20261103.webp"},
    {"Talkie", "com.weaver.app.prod", CLOUDMOON_ICON_PREFIX "com.weaver.app.prod_20261103.webp"},
    {"Mobile Legends: Bang Bang", "com.mobile.legends", CLOUDMOON_ICON_PREFIX "com.mobile.legends_20261103.webp"},
    {"Free Fire", "com.dts.freefireth", CLOUDMOON_ICON_PREFIX "com.dts.freefireth_20261103.webp"},
    {"ZEPETO", "me.zepeto.main", CLOUDMOON_ICON_PREFIX "me.zepeto.main_20261103.webp"},
    {"Pokémon TCG Pocket", "jp.pokemon.pokemontcgp", CLOUDMOON_ICON_PREFIX "jp.pokemon.pokemontcgp_20261103.webp"},
    {"Spotify", "com.spotify.music", CLOUDMOON_ICON_PREFIX "com.spotify.music_20261103.webp"},
    {"Block Blast!", "com.block.juggle", CLOUDMOON_ICON_PREFIX "com.block.juggle_20261103.webp"},
    {"Honkai: Star Rail", "com.HoYoverse.hkrpgoversea", CLOUDMOON_ICON_PREFIX "com.HoYoverse.hkrpgoversea_20261103.webp"},
    {"Survivor.io", "com.dxx.firenow", CLOUDMOON_ICON_PREFIX "com.dxx.firenow_20261103.webp"},
    {"PUBG MOBILE", "com.tencent.ig", CLOUDMOON_ICON_PREFIX "com.tencent.ig_20261103.webp"},
    {"Umamusume: Pretty Derby", "com.cygames.umamusume", CLOUDMOON_ICON_PREFIX "com.cygames.umamusume_20261103.webp"},
    {"Geometry Dash Lite", "com.robtopx.geometryjumplite", CLOUDMOON_ICON_PREFIX "com.robtopx.geometryjumplite_20261103.webp"},
    {"Love and Deepspace", "com.papegames.lysk.en", CLOUDMOON_ICON_PREFIX "com.papegames.lysk.en_20261103.webp"},
    {"Free Fire MAX", "com.dts.freefiremax", CLOUDMOON_ICON_PREFIX "com.dts.freefiremax_20261103.webp"},
    {"DRAGON BALL LEGENDS", "com.bandainamcoent.dblegends_ww", CLOUDMOON_ICON_PREFIX "com.bandainamcoent.dblegends_ww_20261103.webp"},
    {"Zenless Zone Zero", "com.HoYoverse.Nap", CLOUDMOON_ICON_PREFIX "com.HoYoverse.Nap_20261103.webp"},
    {"zeta: AI Chat, Story, Play", "com.scatterlab.messenger", CLOUDMOON_ICON_PREFIX "com.scatterlab.messenger_20261103.webp"},
    {"Wuthering Waves", "com.kurogame.wutheringwaves.global", CLOUDMOON_ICON_PREFIX "com.kurogame.wutheringwaves.global_20261103.webp"},
    {"Character AI", "ai.character.app", CLOUDMOON_ICON_PREFIX "ai.character.app_20261103.webp"},
    {"The Battle Cats", "jp.co.ponos.battlecatsen", CLOUDMOON_ICON_PREFIX "jp.co.ponos.battlecatsen_20261103.webp"},
    {"BASEBALL 9", "us.kr.baseballnine", CLOUDMOON_ICON_PREFIX "us.kr.baseballnine_20261103.webp"}
};

size_t cm_cloudmoon_catalog_count(void) {
    return sizeof(cm_games) / sizeof(cm_games[0]);
}

const cm_cloudmoon_game_t *cm_cloudmoon_catalog_at(size_t index) {
    if (index >= cm_cloudmoon_catalog_count()) return NULL;
    return &cm_games[index];
}

const cm_cloudmoon_game_t *cm_cloudmoon_catalog_find_package(const char *package_id) {
    size_t i;
    if (!package_id || !package_id[0]) return NULL;
    for (i = 0; i < cm_cloudmoon_catalog_count(); ++i) {
        if (strcmp(cm_games[i].package_id, package_id) == 0) return &cm_games[i];
    }
    return NULL;
}
