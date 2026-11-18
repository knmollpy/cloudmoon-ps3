#include "../src/account/cloudmoon_session_link.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    cm_account_session_t session;
    const char *valid =
        "https://web.cloudmoonapp.com/es/?"
        "token=short-lived-token"
        "&email=player%40example.net"
        "&game=com.roblox.client"
        "&userid=user-42"
        "&android_instance_id=eyJjb29yVXJsIjoiaHR0cHM6Ly9jb29yZC5leGFtcGxlLm5ldCJ9";
    const char *wrong_origin =
        "https://evil.example/?token=t&email=a%40b.net&game=x&userid=y"
        "&android_instance_id=eyJjb29yVXJsIjoiaHR0cHM6Ly9jb29yZC5leGFtcGxlLm5ldCJ9";
    const char *duplicate =
        "https://web.cloudmoonapp.com/?token=one&token=two&email=a%40b.net"
        "&game=x&userid=y&android_instance_id=eyJjb29yVXJsIjoiaHR0cHM6Ly9jb29yZC5leGFtcGxlLm5ldCJ9";
    const char *plain_http =
        "http://web.cloudmoonapp.com/?token=t&email=a%40b.net&game=x&userid=y"
        "&android_instance_id=eyJjb29yVXJsIjoiaHR0cHM6Ly9jb29yZC5leGFtcGxlLm5ldCJ9";

    assert(cm_account_session_parse_url(valid, &session) == 0);
    assert(strcmp(session.token, "short-lived-token") == 0);
    assert(strcmp(session.email, "player@example.net") == 0);
    assert(strcmp(session.game, "com.roblox.client") == 0);
    assert(strcmp(session.user_id, "user-42") == 0);
    assert(strcmp(session.coordinator_host, "coord.example.net") == 0);
    assert(session.coordinator_port == 443);
    assert(strcmp(session.coordinator_path,
                  "/client/socket.io/?EIO=3&transport=websocket") == 0);

    assert(cm_account_session_parse_url(wrong_origin, &session) != 0);
    assert(cm_account_session_parse_url(duplicate, &session) != 0);
    assert(cm_account_session_parse_url(plain_http, &session) != 0);

    puts("CloudMoon session-link smoke tests passed");
    return 0;
}
