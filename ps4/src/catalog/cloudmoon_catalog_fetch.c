#include "cloudmoon_catalog.h"
#include <ctype.h>

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


#include "../account/cloudmoon_session_link.h"
#include "../../../src/cloudmoon_discovery.h"
#include "../../../src/cloudmoon_session.h"

static int cm_find_data_object(const char *json, size_t length,
                               const char **object, size_t *object_length) {
    const char *p = json, *end = json + length, *start;
    int depth = 0, in_string = 0, escaped = 0;
    if (!json || !object || !object_length) return -1;

    while (p + 6 < end) {
        if (*p == '"' && (size_t)(end - p) >= 6 &&
            memcmp(p, "\"data\"", 6) == 0)
            break;
        ++p;
    }
    if (p + 6 >= end) return -1;
    p += 6;
    while (p < end && (*p == ' ' || *p == '\t' ||
           *p == '\r' || *p == '\n')) ++p;
    if (p >= end || *p++ != ':') return -1;
    while (p < end && (*p == ' ' || *p == '\t' ||
           *p == '\r' || *p == '\n')) ++p;
    if (p >= end || *p != '{') return -1;

    start = p;
    do {
        char ch = *p++;
        if (in_string) {
            if (escaped) escaped = 0;
            else if (ch == '\\') escaped = 1;
            else if (ch == '"') in_string = 0;
        } else if (ch == '"') in_string = 1;
        else if (ch == '{') ++depth;
        else if (ch == '}') --depth;
    } while (p < end && depth > 0);
    if (depth != 0 || in_string) return -1;
    *object = start;
    *object_length = (size_t)(p - start);
    return 0;
}

static int cm_url_encode_query(const char *input, char *out, size_t capacity) {
    static const char hex[] = "0123456789ABCDEF";
    size_t used = 0;
    const unsigned char *p = (const unsigned char *)input;
    if (!input || !out || capacity == 0) return -1;

    while (*p) {
        unsigned char c = *p++;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            if (used + 1 >= capacity) return -1;
            out[used++] = (char)c;
        } else {
            if (used + 3 >= capacity) return -1;
            out[used++] = '%';
            out[used++] = hex[c >> 4];
            out[used++] = hex[c & 15];
        }
    }
    out[used] = 0;
    return (int)used;
}

int cm_account_session_fetch(const char *ca_bundle_path,
                             unsigned timeout_seconds,
                             cm_account_session_t *account) {
    size_t i;
    char encoded_sid[1600], path[1800];

    if (!ca_bundle_path || !account || !account->sid[0] ||
        timeout_seconds == 0 ||
        cm_url_encode_query(account->sid, encoded_sid,
                            sizeof(encoded_sid)) < 0)
        return -1;

    (void)snprintf(path, sizeof(path), "/web/sid?sid=%s", encoded_sid);

    for (i = 0; i < CM_API_HOST_COUNT; ++i) {
        char *body = NULL;
        size_t body_length = 0, data_length = 0;
        const char *data_object = NULL;
        char value[2049];
        int code = -1, result;

        if (ping_host(s_api_hosts[i], ca_bundle_path, timeout_seconds) != 0)
            continue;

        result = http_get_json(s_api_hosts[i], path, ca_bundle_path,
                               timeout_seconds, &body, &body_length);
        if (result != 0) {
            free(body);
            continue;
        }

        if (cm_json_get_integer(body, body_length, "code", &code) != 0 ||
            code != 0 ||
            cm_find_data_object(body, body_length,
                                &data_object, &data_length) != 0) {
            free(body);
            continue;
        }

        memset(account->token, 0, sizeof(account->token));
        memset(account->email, 0, sizeof(account->email));
        memset(account->game, 0, sizeof(account->game));
        memset(account->user_id, 0, sizeof(account->user_id));
        memset(account->android_instance_id, 0,
               sizeof(account->android_instance_id));
        memset(account->coordinator_host, 0,
               sizeof(account->coordinator_host));
        account->coordinator_port = 0;
        memset(account->coordinator_path, 0,
               sizeof(account->coordinator_path));

        result =
            cm_json_get_string(data_object, data_length, "token",
                               account->token, sizeof(account->token)) >= 0 &&
            cm_json_get_string(data_object, data_length, "email",
                               account->email, sizeof(account->email)) >= 0 &&
            cm_json_get_string(data_object, data_length, "game",
                               account->game, sizeof(account->game)) >= 0 &&
            cm_json_get_string(data_object, data_length, "android_id",
                               account->user_id, sizeof(account->user_id)) >= 0 &&
            cm_json_get_string(data_object, data_length, "android_instance_id",
                               account->android_instance_id,
                               sizeof(account->android_instance_id)) >= 0;
        if (!result) {
            free(body);
            continue;
        }

        if (cm_cloudmoon_get_coordinator(account->android_instance_id,
                                         account->coordinator_host,
                                         sizeof(account->coordinator_host),
                                         &account->coordinator_port,
                                         account->coordinator_path,
                                         sizeof(account->coordinator_path)) != 0) {
            free(body);
            continue;
        }

        if (!account->token[0] || !account->game[0] ||
            !account->user_id[0] || !account->android_instance_id[0] ||
            account->coordinator_port == 0) {
            free(body);
            continue;
        }

        (void)snprintf(account->api_host, sizeof(account->api_host), "%s",
                       s_api_hosts[i]);
        free(body);
        return 0;
    }

    return -1;
}


/* CloudMoon web API account authentication and remote-phone session setup.
   API request shape mirrors the current public CloudMoon web client. */
#define CM_WEB_API_HOST "api.prod.cloudmoonapp.com"

static int cm_make_uuid(char *out, size_t capacity) {
    uint8_t bytes[16];
    int n;
    if (!out || capacity < 37 || cm_orbis_random_source(NULL, bytes, sizeof(bytes)) != 0)
        return -1;
    bytes[6] = (uint8_t)((bytes[6] & 0x0fu) | 0x40u);
    bytes[8] = (uint8_t)((bytes[8] & 0x3fu) | 0x80u);
    n = snprintf(out, capacity,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5],
        bytes[6], bytes[7], bytes[8], bytes[9], bytes[10], bytes[11],
        bytes[12], bytes[13], bytes[14], bytes[15]);
    return n == 36 ? 0 : -1;
}

static int cm_api_request_json(const char *method, const char *path,
                               const char *token, const char *device_id,
                               const char *language, const char *locale,
                               const char *request_body,
                               const char *ca_bundle, unsigned timeout,
                               char **body_out, size_t *body_length_out) {
    cm_tls_connection *tls = NULL;
    char request[8192], request_path[2048], query_uuid[37], status_line[64];
    char headers[2048];
    char *response = NULL, *body = NULL;
    size_t response_length = 0, header_length = 0, body_length = 0;
    size_t expected_body = 0, request_body_length = request_body ? strlen(request_body) : 0;
    int expected_found = 1, chunked = 0, status = 0, request_length, header_length_out;
    int rc = -1;
    unsigned char block[4096];

    if (!method || !path || !device_id || !device_id[0] || !ca_bundle ||
        !body_out || !body_length_out || !language || !locale)
        return -1;
    *body_out = NULL;
    *body_length_out = 0;
    if (cm_make_uuid(query_uuid, sizeof(query_uuid)) != 0) return -1;
    request_length = snprintf(request_path, sizeof(request_path),
        "%s%cdevice_type=web&query_uuid=%s&device_id=%s",
        path, strchr(path, '?') ? '&' : '?', query_uuid, device_id);
    if (request_length < 0 || (size_t)request_length >= sizeof(request_path))
        return -1;

    header_length_out = snprintf(headers, sizeof(headers),
        "Host: %s\r\n"
        "Accept: application/json\r\n"
        "Accept-Encoding: identity\r\n"
        "User-Agent: CloudMoon-PS4/0.2\r\n"
        "X-User-Locale: %s\r\n"
        "X-User-Language: %s\r\n"
        "%s%s%s"
        "Content-Type: application/json\r\n"
        "Content-Length: %lu\r\n"
        "Connection: close\r\n\r\n",
        CM_WEB_API_HOST, locale, language,
        token && token[0] ? "X-User-Token: " : "",
        token && token[0] ? token : "",
        token && token[0] ? "\r\n" : "",
        (unsigned long)request_body_length);
    if (header_length_out < 0 || (size_t)header_length_out >= sizeof(headers))
        return -1;
    request_length = snprintf(request, sizeof(request), "%s %s HTTP/1.1\r\n%s",
                              method, request_path, headers);
    if (request_length < 0 || (size_t)request_length >= sizeof(request))
        return -1;
    response = (char *)malloc(CM_HTTP_RESPONSE_MAX + CM_HTTP_HEADER_MAX + 1u);
    if (!response) return -1;

    if (cm_tls_connect_polarssl(&tls, CM_WEB_API_HOST, 443, ca_bundle, timeout) != 0)
        goto done;
    if (cm_tls_write_polarssl(tls, request, (size_t)request_length) != request_length)
        goto done;
    if (request_body_length &&
        cm_tls_write_polarssl(tls, request_body, request_body_length) !=
            (int)request_body_length)
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
                    if (!line_end || line_length >= sizeof(status_line)) goto done;
                    memcpy(status_line, response, line_length);
                    status_line[line_length] = 0;
                    if (sscanf(status_line, "HTTP/%*u.%*u %d", &status) != 1 ||
                        status != 200) goto done;
                }
                expected_found = parse_content_length(response, header_length,
                                                      &expected_body);
                if (expected_found < 0) goto done;
                chunked = is_chunked(response, header_length);
                if (!chunked && expected_found == 0 &&
                    response_length - header_length >= expected_body) break;
            } else if (response_length > CM_HTTP_HEADER_MAX) goto done;
        } else if (!chunked && expected_found == 0 &&
                   response_length - header_length >= expected_body) break;
    }
    if (!header_length || response_length < header_length) goto done;
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

static int cm_get_data_string(const char *json, size_t length,
                              const char *key, char *out, size_t capacity) {
    const char *data;
    size_t data_length;
    if (cm_find_data_object(json, length, &data, &data_length) != 0) return -1;
    return cm_json_get_string(data, data_length, key, out, capacity);
}

static int cm_find_first_list_item(const char *json, size_t length,
                                   const char **item, size_t *item_length) {
    const char *p, *end, *start;
    int depth = 0, in_string = 0, escaped = 0;
    if (!json || !item || !item_length) return -1;
    end = json + length;
    p = json;
    while (p + 6 < end && memcmp(p, "\"list\"", 6) != 0) ++p;
    if (p + 6 >= end) return -1;
    p += 6;
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
    if (p >= end || *p++ != ':') return -1;
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
    if (p >= end || *p++ != '[') return -1;
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
    if (p >= end || *p != '{') return -1;
    start = p;
    do {
        char ch = *p++;
        if (in_string) {
            if (escaped) escaped = 0;
            else if (ch == '\\') escaped = 1;
            else if (ch == '"') in_string = 0;
        } else if (ch == '"') in_string = 1;
        else if (ch == '{') ++depth;
        else if (ch == '}') --depth;
    } while (p < end && depth > 0);
    if (depth != 0 || in_string) return -1;
    *item = start;
    *item_length = (size_t)(p - start);
    return 0;
}

static int cm_api_success(const char *json, size_t length) {
    int code = -1;
    return cm_json_get_integer(json, length, "code", &code) == 0 && code == 0;
}

int cm_account_session_authenticate(const char *ca_bundle_path,
                                    unsigned timeout_seconds,
                                    const char *provider,
                                    const char *google_id_token,
                                    const char *email,
                                    const char *password,
                                    int server_id,
                                    const char *language,
                                    const char *locale,
                                    cm_account_session_t *account) {
    char request_body[6144], *response = NULL;
    size_t response_length = 0, used;
    const char *credential;
    char *escaped;
    int n, rc;
    static const int allowed_servers[] = {21, 22, 23, 3, 4};
    size_t i;
    int server_ok = 0;

    if (!ca_bundle_path || !account || timeout_seconds == 0 ||
        !provider || !language || !locale) return -1;
    for (i = 0; i < sizeof(allowed_servers) / sizeof(allowed_servers[0]); ++i)
        if (server_id == allowed_servers[i]) server_ok = 1;
    if (!server_ok) return -1;
    if (strcmp(provider, "google") == 0) {
        credential = google_id_token;
        if (!credential || !credential[0] || strlen(credential) > 4096) return -1;
        n = snprintf(request_body, sizeof(request_body),
                     "{\"google_id_token\":\"%s\"}", credential);
        if (n < 0 || (size_t)n >= sizeof(request_body)) return -1;
        rc = cm_orbis_random_init();
        if (rc != 0) return -1;
        memset(account->device_id, 0, sizeof(account->device_id));
        if (cm_make_uuid(account->device_id, sizeof(account->device_id)) != 0) return -1;
        rc = cm_api_request_json("POST", "/login/google", NULL,
                                 account->device_id, language, locale,
                                 request_body, ca_bundle_path, timeout_seconds,
                                 &response, &response_length);
    } else if (strcmp(provider, "password") == 0) {
        if (!email || !email[0] || !password || !password[0] ||
            strlen(email) > 300 || strlen(password) > 1024) return -1;
        /* JSON escape the two user-supplied strings before constructing body. */
        used = 0;
        escaped = request_body;
        for (i = 0; email[i]; ++i) {
            unsigned char ch = (unsigned char)email[i];
            if (ch == '"' || ch == '\\') {
                if (used + 2 >= sizeof(request_body)) return -1;
                escaped[used++] = '\\'; escaped[used++] = (char)ch;
            } else if (ch < 0x20 || used + 1 >= sizeof(request_body)) return -1;
            else escaped[used++] = (char)ch;
        }
        escaped[used] = 0;
        {
            char email_json[640];
            (void)snprintf(email_json, sizeof(email_json), "%s", escaped);
            used = 0;
            for (i = 0; password[i]; ++i) {
                unsigned char ch = (unsigned char)password[i];
                if (ch == '"' || ch == '\\') {
                    if (used + 2 >= sizeof(request_body)) return -1;
                    escaped[used++] = '\\'; escaped[used++] = (char)ch;
                } else if (ch < 0x20 || used + 1 >= sizeof(request_body)) return -1;
                else escaped[used++] = (char)ch;
            }
            escaped[used] = 0;
            n = snprintf(request_body, sizeof(request_body),
                         "{\"email\":\"%s\",\"password\":\"%s\"}",
                         email_json, escaped);
            if (n < 0 || (size_t)n >= sizeof(request_body)) return -1;
        }
        rc = cm_orbis_random_init();
        if (rc != 0) return -1;
        memset(account->device_id, 0, sizeof(account->device_id));
        if (cm_make_uuid(account->device_id, sizeof(account->device_id)) != 0) return -1;
        rc = cm_api_request_json("POST", "/login/pwd", NULL,
                                 account->device_id, language, locale,
                                 request_body, ca_bundle_path, timeout_seconds,
                                 &response, &response_length);
    } else return -1;

    /* Do not retain account credentials in the temporary JSON request body. */
    memset(request_body, 0, sizeof(request_body));
    if (rc != 0 || !response || !cm_api_success(response, response_length)) {
        free(response);
        return -1;
    }
    memset(account->token, 0, sizeof(account->token));
    memset(account->email, 0, sizeof(account->email));
    if (cm_get_data_string(response, response_length, "token",
                           account->token, sizeof(account->token)) < 0) {
        free(response);
        return -1;
    }
    free(response);
    response = NULL;

    if (cm_api_request_json("GET", "/user/info", account->token,
                            account->device_id, language, locale, NULL,
                            ca_bundle_path, timeout_seconds,
                            &response, &response_length) == 0 &&
        cm_api_success(response, response_length))
        (void)cm_get_data_string(response, response_length, "email",
                                 account->email, sizeof(account->email));
    free(response);
    response = NULL;
    if (!account->email[0] && email && email[0])
        (void)snprintf(account->email, sizeof(account->email), "%s", email);
    if (!account->email[0]) return -1;

    account->server_id = server_id;
    (void)snprintf(account->language, sizeof(account->language), "%.7s", language);
    (void)snprintf(account->locale, sizeof(account->locale), "%.7s", locale);
    (void)snprintf(account->api_host, sizeof(account->api_host), "%s", CM_WEB_API_HOST);
    account->sid[0] = 0;
    account->game[0] = 0;
    account->android_instance_id[0] = 0;
    account->coordinator_host[0] = 0;
    account->coordinator_path[0] = 0;
    account->coordinator_port = 0;
    return account->token[0] ? 0 : -1;
}

int cm_account_session_prepare_game(const char *ca_bundle_path,
                                    unsigned timeout_seconds,
                                    cm_account_session_t *account,
                                    const char *game_package) {
    char *response = NULL, request_body[2048];
    char locale_params[8];
    size_t response_length = 0, item_length = 0, locale_index;
    const char *item = NULL;
    int n, rc;

    if (!ca_bundle_path || !account || !account->token[0] ||
        !account->device_id[0] || !game_package || !game_package[0] ||
        timeout_seconds == 0)
        return -1;

    rc = cm_api_request_json("GET", "/phone/list", account->token,
                             account->device_id,
                             account->language[0] ? account->language : "en",
                             account->locale[0] ? account->locale : "AR",
                             NULL, ca_bundle_path, timeout_seconds,
                             &response, &response_length);
    if (rc != 0 || !cm_api_success(response, response_length) ||
        cm_find_first_list_item(response, response_length,
                                &item, &item_length) != 0 ||
        cm_json_get_string(item, item_length, "android_id",
                           account->user_id, sizeof(account->user_id)) < 0) {
        free(response);
        return -1;
    }
    free(response);
    response = NULL;

    (void)snprintf(locale_params, sizeof(locale_params), "%.7s",
                   account->locale[0] ? account->locale : "AR");
    for (locale_index = 0; locale_params[locale_index]; ++locale_index)
        locale_params[locale_index] =
            (char)tolower((unsigned char)locale_params[locale_index]);
    n = snprintf(request_body, sizeof(request_body),
        "{\"android_id\":\"%s\",\"game_name\":\"%s\","
        "\"screen_res\":\"720x1280\",\"server_id\":%d,"
        "\"params\":\"{\\\"language\\\":\\\"%s\\\",\\\"locale\\\":\\\"%s\\\"}\"}",
        account->user_id, game_package, account->server_id,
        account->language[0] ? account->language : "en", locale_params);
    if (n < 0 || (size_t)n >= sizeof(request_body)) return -1;

    rc = cm_api_request_json("POST", "/phone/connect", account->token,
                             account->device_id,
                             account->language[0] ? account->language : "en",
                             account->locale[0] ? account->locale : "AR",
                             request_body, ca_bundle_path, timeout_seconds,
                             &response, &response_length);
    if (rc != 0 || !cm_api_success(response, response_length) ||
        cm_get_data_string(response, response_length, "android_instance_id",
                           account->android_instance_id,
                           sizeof(account->android_instance_id)) < 0 ||
        cm_get_data_string(response, response_length, "coordinator_host",
                           account->coordinator_host,
                           sizeof(account->coordinator_host)) < 0) {
        free(response);
        return -1;
    }
    free(response);

    /* Match the regional coordinator aliases used by CloudMoon's web client. */
    {
        const char *coordinator_url = account->coordinator_host;
        char normalized_url[320], parsed_path[128];
        if (strcmp(coordinator_url,
                   "https://coordinator-sgp-cf.prod.nenly.cn") == 0)
            coordinator_url = "https://coor-sgp.prod.cloudmoonapp.com";
        else if (strcmp(coordinator_url,
                        "https://coordinator-sgp-cf-sp.prod.nenly.cn") == 0)
            coordinator_url = "https://coor-nl.prod.cloudmoonapp.com";
        else if (strcmp(coordinator_url,
                        "https://coordinator-funplus-va.prod.nenly.cn") == 0)
            coordinator_url = "https://coor-la.prod.cloudmoonapp.com";
        (void)snprintf(normalized_url, sizeof(normalized_url), "%s",
                       coordinator_url);
        memset(account->coordinator_host, 0, sizeof(account->coordinator_host));
        if (cm_cloudmoon_parse_https_url(normalized_url,
                                         account->coordinator_host,
                                         sizeof(account->coordinator_host),
                                         &account->coordinator_port,
                                         parsed_path, sizeof(parsed_path)) != 0)
            return -1;
        (void)snprintf(account->coordinator_path,
                       sizeof(account->coordinator_path),
                       "/client/socket.io/?EIO=3&transport=websocket");
    }
    (void)snprintf(account->game, sizeof(account->game), "%.128s", game_package);
    return account->coordinator_host[0] && account->coordinator_port != 0 ? 0 : -1;
}
