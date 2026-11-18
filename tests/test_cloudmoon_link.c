#include "cloudmoon_link.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        failures++; \
    } \
} while (0)

int main(void) {
    cm_cloudmoon_link_t link;
    const char *valid =
        "https://web.cloudmoonapp.com/es/?token=a%2Bb&email=player%40example.com"
        "&game=com.roblox.client&userid=12345"
        "&android_instance_id=eyJjb29yVXJsIjoiaHR0cHM6Ly9jb29yLmNsb3VkbW9vbi5hcHAifQ%3D%3D";
    int rc;

    rc = cm_cloudmoon_parse_session_url(valid, &link);
    CHECK(rc == 0);
    CHECK(strcmp(link.game, "com.roblox.client") == 0);
    CHECK(strcmp(link.user_id, "12345") == 0);
    CHECK(strcmp(link.android_instance_id,
        "eyJjb29yVXJsIjoiaHR0cHM6Ly9jb29yLmNsb3VkbW9vbi5hcHAifQ==") == 0);

    rc = cm_cloudmoon_parse_session_url(
        "https://web.cloudmoonapp.com/play?game=pkg&user_id=u"
        "&android_instance_id=abc+def%3D", &link);
    CHECK(rc == 0);
    CHECK(strcmp(link.android_instance_id, "abc+def=") == 0);

    CHECK(cm_cloudmoon_parse_session_url(
        "http://web.cloudmoonapp.com/?game=pkg&userid=u&android_instance_id=a",
        &link) < 0);
    CHECK(cm_cloudmoon_parse_session_url(
        "https://cloudmoonapp.com.evil.test/?game=pkg&userid=u&android_instance_id=a",
        &link) < 0);
    CHECK(cm_cloudmoon_parse_session_url(
        "https://evilcloudmoonapp.com/?game=pkg&userid=u&android_instance_id=a",
        &link) < 0);
    CHECK(cm_cloudmoon_parse_session_url(
        "https://web.cloudmoonapp.com/?game=pkg&userid=u", &link) < 0);
    CHECK(cm_cloudmoon_parse_session_url(
        "https://web.cloudmoonapp.com/?game=pkg&userid=u&android_instance_id=%ZZ",
        &link) < 0);
    CHECK(cm_cloudmoon_parse_session_url(
        "https://web.cloudmoonapp.com:99999/?game=pkg&userid=u&android_instance_id=a",
        &link) < 0);

    if (failures) {
        fprintf(stderr, "%d CloudMoon session URL test(s) failed.\n", failures);
        return 1;
    }
    puts("CloudMoon session URL parser tests passed.");
    return 0;
}
