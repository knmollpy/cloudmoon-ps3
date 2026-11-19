#include "../src/account/cloudmoon_session_link.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    cm_account_session_t session;
    const char *valid =
        "https://web.cloudmoonapp.com/run-site/?sid=8ab32f9d-A1_2-xyz";
    const char *valid_root =
        "https://web.cloudmoonapp.com/?sid=abc123";
    const char *wrong_origin =
        "https://evil.example/run-site/?sid=abc123";
    const char *plain_http =
        "http://web.cloudmoonapp.com/run-site/?sid=abc123";
    const char *duplicate =
        "https://web.cloudmoonapp.com/run-site/?sid=one&sid=two";
    const char *fragment_only =
        "https://web.cloudmoonapp.com/run-site/#sid=abc123";
    const char *missing =
        "https://web.cloudmoonapp.com/run-site/?game=com.roblox.client";

    assert(cm_account_session_parse_url(valid, &session) == 0);
    assert(strcmp(session.sid, "8ab32f9d-A1_2-xyz") == 0);
    assert(session.token[0] == 0);
    assert(cm_account_session_parse_url(valid_root, &session) == 0);
    assert(strcmp(session.sid, "abc123") == 0);

    assert(cm_account_session_parse_url(wrong_origin, &session) != 0);
    assert(cm_account_session_parse_url(plain_http, &session) != 0);
    assert(cm_account_session_parse_url(duplicate, &session) != 0);
    assert(cm_account_session_parse_url(fragment_only, &session) != 0);
    assert(cm_account_session_parse_url(missing, &session) != 0);

    puts("CloudMoon sid link smoke tests passed");
    return 0;
}
