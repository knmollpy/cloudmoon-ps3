#include "../src/catalog/cloudmoon_catalog.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    static const char json[] =
        "{\"code\":0,\"data\":{\"list\":["
        "{\"icon_url\":\"https://cdn.example/icon.webp\","
        "\"title\":\"Roblox\",\"package_name\":\"com.roblox.client\","
        "\"status\":1,\"is_pc\":false,\"is_beta\":false,"
        "\"min_vip_level\":1,\"categories\":[\"simulation\",\"casual\"],"
        "\"maintenance_info\":\"Sorry, game is under maintenance.\"},"
        "{\"title\":\"Genshin \\u0054\\u0065\\u0073\\u0074\","
        "\"package_name\":\"com.mihoyo.test\",\"status\":1,"
        "\"is_pc\":false,\"need_login_google\":true},"
        "{\"title\":\"Disabled\",\"package_name\":\"disabled.app\","
        "\"status\":0,\"is_pc\":false},"
        "{\"title\":\"PC Game\",\"package_name\":\"pc.game\","
        "\"status\":1,\"is_pc\":true},"
        "{\"title\":\"\", \"package_name\":\"missing.title\","
        "\"status\":1,\"is_pc\":false}"
        "]}}";
    static const char invalid[] =
        "{\"code\":1,\"data\":{\"list\":[{\"title\":\"No\",\"package_name\":\"no.app\"}]}}";
    static const char no_package[] =
        "{\"code\":0,\"data\":{\"list\":[{\"title\":\"No package\"}]}}";
    cm_catalog_t catalog;

    assert(cm_catalog_parse_json(json, sizeof(json) - 1u, &catalog) == 2);
    assert(catalog.count == 2);
    assert(strcmp(catalog.games[0].title, "Roblox") == 0);
    assert(strcmp(catalog.games[0].package_name, "com.roblox.client") == 0);
    assert(strcmp(catalog.games[0].icon_url,
                  "https://cdn.example/icon.webp") == 0);
    assert(strcmp(catalog.games[0].maintenance_info,
                  "Sorry, game is under maintenance.") == 0);
    assert(strcmp(catalog.games[1].title, "Genshin Test") == 0);
    assert(catalog.games[1].need_login_google == 1);

    assert(cm_catalog_parse_json(invalid, sizeof(invalid) - 1u, &catalog) == -1);
    assert(cm_catalog_parse_json(no_package, sizeof(no_package) - 1u,
                                 &catalog) == -1);
    assert(cm_catalog_parse_json("{bad", 4, &catalog) == -1);

    puts("CloudMoon catalogue parser tests passed");
    return 0;
}
