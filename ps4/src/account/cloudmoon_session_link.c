#include "cloudmoon_session_link.h"

#include "cloudmoon_discovery.h"

#include <ctype.h>
#include <string.h>

#define CM_SESSION_URL_MAX 8192

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int decode_component(const char *src, size_t len,
                            char *dst, size_t cap) {
    size_t i, n = 0;
    if (!src || !dst || cap == 0) return -1;
    for (i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)src[i];
        if (c == '+') {
            c = ' ';
        } else if (c == '%') {
            int hi, lo;
            if (i + 2 >= len) return -1;
            hi = hex_value(src[i + 1]);
            lo = hex_value(src[i + 2]);
            if (hi < 0 || lo < 0) return -1;
            c = (unsigned char)((hi << 4) | lo);
            i += 2;
        }
        if (c == 0 || c < 0x20 || c == 0x7f || n + 1 >= cap) return -1;
        dst[n++] = (char)c;
    }
    dst[n] = 0;
    return (int)n;
}

static int copy_query_value(const char *url, const char *key,
                            char *out, size_t out_cap) {
    const char *query, *query_end, *p, *end;
    size_t key_len;
    int found = 0;
    if (!url || !key || !out || !out_cap) return -1;
    query = strchr(url, '?');
    if (!query) return -1;
    ++query;
    query_end = strchr(query, '#');
    if (!query_end) query_end = query + strlen(query);
    key_len = strlen(key);
    p = query;
    while (p < query_end) {
        const char *pair_end = memchr(p, '&', (size_t)(query_end - p));
        const char *equals;
        if (!pair_end) pair_end = query_end;
        end = pair_end;
        equals = memchr(p, '=', (size_t)(end - p));
        if (equals && (size_t)(equals - p) == key_len &&
            memcmp(p, key, key_len) == 0) {
            if (found) return -1; /* duplicate auth fields are ambiguous */
            if (decode_component(equals + 1, (size_t)(end - equals - 1),
                                 out, out_cap) < 0) return -1;
            found = 1;
        }
        p = *pair_end == '&' ? pair_end + 1 : pair_end;
    }
    return found ? 0 : -1;
}

static int valid_value(const char *value, size_t max_len) {
    size_t i, n;
    if (!value || !value[0]) return 0;
    n = strlen(value);
    if (n > max_len) return 0;
    for (i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)value[i];
        if (c < 0x20 || c == 0x7f) return 0;
    }
    return 1;
}

int cm_account_session_parse_url(const char *url, cm_account_session_t *out) {
    const char *authority, *authority_end;
    char host[128], email[321];
    size_t host_len, url_len;

    if (!url || !out) return -1;
    url_len = strlen(url);
    if (url_len == 0 || url_len > CM_SESSION_URL_MAX ||
        strncmp(url, "https://", 8) != 0) return -1;

    authority = url + 8;
    authority_end = authority;
    while (*authority_end && *authority_end != '/' &&
           *authority_end != '?' && *authority_end != '#')
        ++authority_end;
    host_len = (size_t)(authority_end - authority);
    if (host_len == 0 || host_len >= sizeof(host) ||
        memchr(authority, '@', host_len) ||
        memchr(authority, ':', host_len)) return -1;
    memcpy(host, authority, host_len);
    host[host_len] = 0;
    if (strcmp(host, "web.cloudmoonapp.com") != 0) return -1;

    memset(out, 0, sizeof(*out));
    if (copy_query_value(url, "token", out->token, sizeof(out->token)) != 0 ||
        copy_query_value(url, "email", email, sizeof(email)) != 0 ||
        copy_query_value(url, "game", out->game, sizeof(out->game)) != 0 ||
        copy_query_value(url, "userid", out->user_id, sizeof(out->user_id)) != 0 ||
        copy_query_value(url, "android_instance_id",
                         out->android_instance_id,
                         sizeof(out->android_instance_id)) != 0) {
        memset(out, 0, sizeof(*out));
        return -1;
    }
    if (!valid_value(out->token, 1024) ||
        !valid_value(email, 320) ||
        !valid_value(out->game, 128) ||
        !valid_value(out->user_id, 128) ||
        !valid_value(out->android_instance_id, 2048) ||
        !strchr(email, '@')) {
        memset(out, 0, sizeof(*out));
        return -1;
    }
    memcpy(out->email, email, strlen(email) + 1);

    if (cm_cloudmoon_get_coordinator(out->android_instance_id,
                                     out->coordinator_host,
                                     sizeof(out->coordinator_host),
                                     &out->coordinator_port,
                                     out->coordinator_path,
                                     sizeof(out->coordinator_path)) != 0) {
        memset(out, 0, sizeof(*out));
        return -1;
    }
    if (!valid_value(out->coordinator_host, sizeof(out->coordinator_host) - 1) ||
        out->coordinator_port == 0 ||
        strcmp(out->coordinator_path,
               "/client/socket.io/?EIO=3&transport=websocket") != 0) {
        memset(out, 0, sizeof(*out));
        return -1;
    }

    return 0;
}
