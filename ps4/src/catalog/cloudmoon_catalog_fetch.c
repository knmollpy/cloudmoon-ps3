#include "cloudmoon_catalog.h"

#include "cloudmoon_random_mbedtls.h"
#include "cloudmoon_tls_polarssl.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define CM_HTTP_RESPONSE_MAX (1024u * 1024u)
#define CM_HTTP_HEADER_MAX 16384u
#define CM_API_HOST_COUNT 5

static int contains_case_range(const char *begin, const char *end, const char *needle) {
    size_t n = strlen(needle);
    const char *p;
    if ((size_t)(end - begin) < n) return 0;
    for (p = begin; p + n <= end; ++p)
        if (strncasecmp(p, needle, n) == 0) return 1;
    return 0;
}


static const char *const s_api_hosts[CM_API_HOST_COUNT] = {
    "api.cloudmoon.cloudbatata.com",
    "api.prod.cloudmoonapp.com",
    "api.prod.geometry.today",
    "hrz5zfjq02.execute-api.us-east-1.amazonaws.com",
    "api.prod.viewoncloud.com"
};

static int find_header_end(const char *data, size_t length, size_t *offset) {
    size_t i;
    if (!data || !offset || length < 4) return 0;
    for (i = 0; i + 3 < length; ++i) {
        if (memcmp(data + i, "\r\n\r\n", 4) == 0) {
            *offset = i + 4;
            return 1;
        }
    }
    return 0;
}

static int parse_content_length(const char *headers, size_t length,
                                size_t *content_length) {
    const char *p = headers;
    const char *end = headers + length;
    if (!content_length) return -1;
    while (p < end) {
        const char *line_end = strstr(p, "\r\n");
        const char *colon;
        if (!line_end || line_end > end) line_end = end;
        colon = memchr(p, ':', (size_t)(line_end - p));
        if (colon && (size_t)(colon - p) == 14 &&
            strncasecmp(p, "Content-Length", 14) == 0) {
            char number[32];
            size_t n = (size_t)(line_end - colon - 1);
            char *tail;
            unsigned long parsed;
            while (n && (*(colon + 1 + n - 1) == ' ' ||
                         *(colon + 1 + n - 1) == '\t')) --n;
            while (n && (*(colon + 1) == ' ' || *(colon + 1) == '\t')) {
                ++colon;
                --n;
            }
            if (n == 0 || n >= sizeof(number)) return -1;
            memcpy(number, colon + 1, n);
            number[n] = 0;
            errno = 0;
            parsed = strtoul(number, &tail, 10);
            if (errno || tail == number || *tail || parsed > CM_HTTP_RESPONSE_MAX)
                return -1;
            *content_length = (size_t)parsed;
            return 0;
        }
        p = line_end < end && end - line_end >= 2 ? line_end + 2 : end;
    }
    return 1;
}

static int is_chunked(const char *headers, size_t length) {
    const char *p = headers;
    const char *end = headers + length;
    while (p < end) {
        const char *line_end = p;
        const char *colon;
        while (line_end + 1 < end &&
               !(line_end[0] == '\r' && line_end[1] == '\n'))
            ++line_end;
        if (line_end + 1 >= end) line_end = end;
        colon = memchr(p, ':', (size_t)(line_end - p));
        if (colon && (size_t)(colon - p) == 17 &&
            strncasecmp(p, "Transfer-Encoding", 17) == 0 &&
            contains_case_range(colon + 1, line_end, "chunked"))
            return 1;
        p = line_end + (line_end < end ? 2 : 0);
    }
    return 0;
}

static int decode_chunked(const char *input, size_t input_length,
                          char *output, size_t output_capacity,
                          size_t *output_length) {
    const char *p = input;
    const char *end = input + input_length;
    size_t used = 0;
    while (p < end) {
        const char *line_end = strstr(p, "\r\n");
        char number[32];
        char *tail;
        unsigned long chunk;
        size_t n;
        if (!line_end || line_end > end) return -1;
        n = (size_t)(line_end - p);
        if (n == 0 || n >= sizeof(number)) return -1;
        memcpy(number, p, n);
        number[n] = 0;
        errno = 0;
        chunk = strtoul(number, &tail, 16);
        if (errno || tail == number || (*tail && *tail != ';') ||
            chunk > output_capacity - used)
            return -1;
        p = line_end + 2;
        if (chunk == 0) {
            *output_length = used;
            return 0;
        }
        if ((size_t)(end - p) < (size_t)chunk + 2 ||
            p[chunk] != '\r' || p[chunk + 1] != '\n')
            return -1;
        memcpy(output + used, p, (size_t)chunk);
        used += (size_t)chunk;
        p += chunk + 2;
    }
    return -1;
}

static int http_get_json(const char *host, const char *path,
                         const char *ca_bundle, unsigned timeout,
                         char **body_out, size_t *body_length_out) {
    cm_tls_connection *tls = NULL;
    char request[2048], status_line[64];
    char *response = NULL, *body = NULL;
    size_t response_length = 0, header_length = 0, body_length = 0;
    size_t expected_body = 0;
    int expected_found = 1;
    int chunked = 0;
    int status = 0;
    int request_length, rc = -1;
    unsigned char block[4096];

    if (!host || !path || !ca_bundle || !body_out || !body_length_out)
        return -1;
    *body_out = NULL;
    *body_length_out = 0;
    response = (char *)malloc(CM_HTTP_RESPONSE_MAX + CM_HTTP_HEADER_MAX + 1u);
    if (!response) return -1;

    request_length = snprintf(request, sizeof(request),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Accept: application/json\r\n"
        "Accept-Encoding: identity\r\n"
        "User-Agent: CloudMoon-PS4/0.1\r\n"
        "Connection: close\r\n\r\n", path, host);
    if (request_length < 0 || (size_t)request_length >= sizeof(request))
        goto done;

    if (cm_tls_connect_polarssl(&tls, host, 443, ca_bundle, timeout) != 0)
        goto done;
    if (cm_tls_write_polarssl(tls, request, (size_t)request_length) != request_length)
        goto done;

    while (response_length < CM_HTTP_RESPONSE_MAX + CM_HTTP_HEADER_MAX) {
        int n = cm_tls_read_polarssl(tls, block, sizeof(block));
        if (n < 0) goto done;
        if (n == 0) break;
        if ((size_t)n > CM_HTTP_RESPONSE_MAX + CM_HTTP_HEADER_MAX - response_length)
            goto done;
        memcpy(response + response_length, block, (size_t)n);
        response_length += (size_t)n;
        response[response_length] = 0;

        if (!header_length) {
            size_t offset;
            if (find_header_end(response, response_length, &offset)) {
                header_length = offset;
                {
                    const char *line_end = strstr(response, "\r\n");
                    size_t line_length = line_end ? (size_t)(line_end - response) : 0;
                    if (!line_end || line_length >= sizeof(status_line))
                        goto done;
                    memcpy(status_line, response, line_length);
                    status_line[line_length] = 0;
                    if (sscanf(status_line, "HTTP/%*u.%*u %d", &status) != 1 ||
                        status != 200)
                        goto done;
                }
                expected_found = parse_content_length(response, header_length,
                                                      &expected_body);
                if (expected_found < 0) goto done;
                chunked = is_chunked(response, header_length);
                if (!chunked && expected_found == 0 &&
                    response_length - header_length >= expected_body)
                    break;
            } else if (response_length > CM_HTTP_HEADER_MAX) {
                goto done;
            }
        } else if (!chunked && expected_found == 0 &&
                   response_length - header_length >= expected_body) {
            break;
        }
    }

    if (!header_length) goto done;
    if (response_length < header_length) goto done;
    body = (char *)malloc(CM_HTTP_RESPONSE_MAX + 1u);
    if (!body) goto done;
    if (chunked) {
        if (decode_chunked(response + header_length,
                           response_length - header_length,
                           body, CM_HTTP_RESPONSE_MAX, &body_length) != 0)
            goto done;
    } else {
        size_t available = response_length - header_length;
        if (expected_found == 0) {
            if (available < expected_body) goto done;
            available = expected_body;
        }
        if (available == 0 || available > CM_HTTP_RESPONSE_MAX) goto done;
        memcpy(body, response + header_length, available);
        body_length = available;
    }
    body[body_length] = 0;
    *body_out = body;
    *body_length_out = body_length;
    body = NULL;
    rc = 0;

done:
    if (tls) cm_tls_close_polarssl(tls);
    free(body);
    free(response);
    return rc;
}

static int ping_host(const char *host, const char *ca_bundle,
                     unsigned timeout) {
    char *body = NULL;
    size_t length = 0;
    int rc = http_get_json(host, "/_ping", ca_bundle, timeout, &body, &length);
    if (rc == 0 &&
        (length == 0 || (!strstr(body, "pong") && !strstr(body, "success") &&
                         !strstr(body, "\"code\""))))
        rc = -1;
    free(body);
    return rc;
}

int cm_catalog_fetch(const char *ca_bundle_path, unsigned timeout_seconds,
                     cm_catalog_t *catalog, char *api_host,
                     size_t api_host_capacity) {
    size_t i;
    if (!ca_bundle_path || !catalog || !api_host || api_host_capacity == 0)
        return -1;
    memset(catalog, 0, sizeof(*catalog));
    api_host[0] = 0;

    for (i = 0; i < CM_API_HOST_COUNT; ++i) {
        char *body = NULL;
        size_t body_length = 0;
            int result;

        if (ping_host(s_api_hosts[i], ca_bundle_path, timeout_seconds) != 0)
            continue;

        result = http_get_json(s_api_hosts[i], "/game/list", ca_bundle_path,
                               timeout_seconds, &body, &body_length);
        if (result == 0)
            result = cm_catalog_parse_json(body, body_length, catalog);
        free(body);
        if (result > 0) {
            (void)snprintf(api_host, api_host_capacity, "%s", s_api_hosts[i]);
            return result;
        }
    }
    return -1;
}
