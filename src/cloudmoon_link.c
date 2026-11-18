#include "cloudmoon_link.h"

#include <string.h>
#include <ctype.h>

#define CM_LINK_URL_MAX 16384

static int cm_link_hex(unsigned char c) {
    if (c >= '0' && c <= '9') return (int)(c - '0');
    if (c >= 'a' && c <= 'f') return (int)(c - 'a') + 10;
    if (c >= 'A' && c <= 'F') return (int)(c - 'A') + 10;
    return -1;
}

static int cm_link_decode(const char *src, size_t length,
                          char *dst, size_t capacity, int plus_as_space) {
    size_t i, out = 0;
    if (!src || !dst || !capacity) return -1;

    for (i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)src[i];
        if (c == '%') {
            int hi, lo;
            if (i + 2 >= length) return -1;
            hi = cm_link_hex((unsigned char)src[i + 1]);
            lo = cm_link_hex((unsigned char)src[i + 2]);
            if (hi < 0 || lo < 0) return -1;
            c = (unsigned char)((hi << 4) | lo);
            i += 2;
        } else if (c == '+' && plus_as_space) {
            c = ' ';
        }

        /* Do not allow embedded NULs or control characters in INI values. */
        if (c == 0 || c < 0x20 || c == 0x7f) return -1;
        if (out + 1 >= capacity) return -1;
        dst[out++] = (char)c;
    }
    dst[out] = '\0';
    return (int)out;
}

static int cm_link_host_allowed(const char *begin, const char *end) {
    char host[256];
    size_t n = (size_t)(end - begin), i, host_len;
    const char *colon = NULL;

    if (!n || n >= sizeof(host) || memchr(begin, '@', n) ||
        memchr(begin, '[', n) || memchr(begin, ']', n)) return 0;

    for (i = 0; i < n; ++i) {
        if (begin[i] == ':') {
            if (colon) return 0;
            colon = begin + i;
        }
    }
    host_len = colon ? (size_t)(colon - begin) : n;
    if (!host_len || host_len >= sizeof(host)) return 0;
    for (i = 0; i < host_len; ++i) {
        unsigned char c = (unsigned char)begin[i];
        if (!(isalnum(c) || c == '-' || c == '.')) return 0;
        host[i] = (char)tolower(c);
    }
    host[host_len] = '\0';

    /* Only accept URLs originating from CloudMoon's own HTTPS site. */
    {
        int allowed = 0;
        if (!strcmp(host, "cloudmoonapp.com")) {
            allowed = 1;
        } else {
            static const char suffix[] = ".cloudmoonapp.com";
            size_t suffix_len = sizeof(suffix) - 1;
            if (host_len > suffix_len &&
                !strcmp(host + host_len - suffix_len, suffix)) allowed = 1;
        }
        if (colon) {
            unsigned long port = 0;
            const char *q = colon + 1;
            if (q >= end) return 0;
            for (; q < end; ++q) {
                if (*q < '0' || *q > '9') return 0;
                port = port * 10UL + (unsigned long)(*q - '0');
                if (port > 65535UL) return 0;
            }
            if (!port) return 0;
        }
        return allowed;
    }
}

int cm_cloudmoon_parse_session_url(const char *url, cm_cloudmoon_link_t *link) {
    const char *authority, *authority_end, *query, *query_end, *p;
    size_t url_length;

    if (!url || !link) return -1;
    memset(link, 0, sizeof(*link));
    url_length = strlen(url);
    if (url_length < 12 || url_length >= CM_LINK_URL_MAX ||
        strncmp(url, "https://", 8) != 0) return -1;

    authority = url + 8;
    authority_end = authority;
    while (*authority_end && *authority_end != '/' &&
           *authority_end != '?' && *authority_end != '#') {
        ++authority_end;
    }
    if (!cm_link_host_allowed(authority, authority_end)) return -1;

    query = strchr(authority_end, '?');
    if (!query) return -1;
    ++query;
    query_end = strchr(query, '#');
    if (!query_end) query_end = url + url_length;

    p = query;
    while (p < query_end) {
        const char *part_end = p, *equals;
        char key[64];
        size_t key_len;
        while (part_end < query_end && *part_end != '&') ++part_end;
        equals = p;
        while (equals < part_end && *equals != '=') ++equals;
        key_len = (size_t)(equals - p);

        if (key_len && cm_link_decode(p, key_len, key, sizeof(key), 1) >= 0 &&
            equals < part_end) {
            const char *value = equals + 1;
            size_t value_len = (size_t)(part_end - value);
            if (!strcmp(key, "game")) {
                if (cm_link_decode(value, value_len, link->game,
                                   sizeof(link->game), 1) < 0) return -1;
            } else if (!strcmp(key, "userid") || !strcmp(key, "user_id")) {
                if (cm_link_decode(value, value_len, link->user_id,
                                   sizeof(link->user_id), 1) < 0) return -1;
            } else if (!strcmp(key, "android_instance_id")) {
                if (cm_link_decode(value, value_len, link->android_instance_id,
                                   sizeof(link->android_instance_id), 1) < 0) return -1;
                /* Base64 '+' is often represented as a space by form URLs. */
                {
                    char *q;
                    for (q = link->android_instance_id; *q; ++q) {
                        if (*q == ' ') *q = '+';
                    }
                }
            }
        }
        p = part_end < query_end ? part_end + 1 : query_end;
    }

    if (!link->game[0] || !link->user_id[0] ||
        !link->android_instance_id[0]) return -1;
    return 0;
}
