/*
 * One-time local HTTP pairing receiver for CloudMoon PS3.
 * The browser signs in to CloudMoon over HTTPS itself. This receiver accepts
 * only an active CloudMoon session URL, extracts its three connection fields,
 * and stores those fields locally. It never receives Google credentials.
 */
#include "cloudmoon_pairing.h"
#include "cloudmoon_link.h"
#include "cloudmoon_ps3_random.h"
#include "cloudmoon_session.h"
#include "qrcodegen.h"
#include "ui.h"
#include "net_logger.h"

#include <net/netctl.h>
#include <sys/thread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>
#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define CM_PAIR_CONFIG_PATH "/dev_hdd0/game/MNLT00001/USRDIR/cloudmoon.ini"
#define CM_PAIR_PAGE_PATH "/dev_hdd0/game/MNLT00001/USRDIR/cloudmoon-mobile-link.html"
#define CM_PAIR_HTTP_LIMIT 24576
#define CM_PAIR_BODY_LIMIT 16384
#define CM_PAIR_HEADER_LIMIT 4096

static sys_ppu_thread_t cm_pair_thread;
static volatile int cm_pair_running;
static volatile int cm_pair_stop_requested;
static volatile int cm_pair_linked;
static int cm_pair_thread_started;
static int cm_pair_listen_fd = -1;
static char cm_pair_key[33];
static char cm_pair_address[16];

static int cm_pair_send_all(int fd, const void *data, size_t length) {
    const char *p = (const char *)data;
    size_t sent = 0;
    while (sent < length) {
        int n = (int)send(fd, p + sent, length - sent, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

static int cm_pair_send_response(int fd, int status, const char *reason,
                                 const char *content_type, const char *body) {
    char header[512];
    size_t body_len = body ? strlen(body) : 0;
    int n = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %lu\r\n"
        "Cache-Control: no-store, no-cache, must-revalidate\r\n"
        "Pragma: no-cache\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "Connection: close\r\n\r\n",
        status, reason, content_type, (unsigned long)body_len);
    if (n < 0 || (size_t)n >= sizeof(header)) return -1;
    if (cm_pair_send_all(fd, header, (size_t)n) != 0) return -1;
    return body_len ? cm_pair_send_all(fd, body, body_len) : 0;
}


static int cm_pair_read_request(int fd, char *buffer, size_t capacity,
                                size_t *header_length, size_t *body_length) {
    size_t used = 0, header_end = 0, expected = 0;
    int have_length = 0;
    struct timeval timeout;

    if (!buffer || capacity < 128 || !header_length || !body_length) return -1;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    while (used + 1 < capacity) {
        char *end;
        int n = (int)recv(fd, buffer + used, capacity - used - 1, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        used += (size_t)n;
        buffer[used] = '\0';

        if (!header_end) {
            end = strstr(buffer, "\r\n\r\n");
            if (!end) {
                if (used >= CM_PAIR_HEADER_LIMIT) return -1;
                continue;
            }
            header_end = (size_t)(end - buffer) + 4;
            {
                char *line = strstr(buffer, "\r\n");
                char *p = line ? line + 2 : NULL;
                while (p && p < buffer + header_end - 2) {
                    char *line_end = strstr(p, "\r\n");
                    char *colon;
                    if (!line_end || line_end == p) break;
                    colon = memchr(p, ':', (size_t)(line_end - p));
                    if (colon && (size_t)(line_end - colon) >= 2 &&
                        (size_t)(colon - p) == 14 &&
                        strncasecmp(p, "Content-Length", 14) == 0) {
                        char *q = colon + 1;
                        unsigned long value = 0;
                        while (q < line_end && isspace((unsigned char)*q)) ++q;
                        if (q == line_end) return -1;
                        for (; q < line_end; ++q) {
                            if (*q < '0' || *q > '9') return -1;
                            value = value * 10UL + (unsigned long)(*q - '0');
                            if (value > CM_PAIR_BODY_LIMIT) return -1;
                        }
                        expected = (size_t)value;
                        have_length = 1;
                    }
                    p = line_end + 2;
                }
            }
            if (!have_length) expected = 0;
        }
        if (header_end && used >= header_end + expected) {
            *header_length = header_end;
            *body_length = expected;
            return 0;
        }
    }
    return -1;
}

static int cm_pair_request_line(char *buffer, char *method, size_t method_cap,
                                char *path, size_t path_cap) {
    char *line_end, *first, *second;
    size_t n;
    line_end = strstr(buffer, "\r\n");
    if (!line_end) return -1;
    first = memchr(buffer, ' ', (size_t)(line_end - buffer));
    if (!first) return -1;
    second = memchr(first + 1, ' ', (size_t)(line_end - first - 1));
    if (!second) return -1;
    n = (size_t)(first - buffer);
    if (!n || n >= method_cap) return -1;
    memcpy(method, buffer, n);
    method[n] = '\0';
    n = (size_t)(second - first - 1);
    if (!n || n >= path_cap) return -1;
    memcpy(path, first + 1, n);
    path[n] = '\0';
    return 0;
}

static int cm_pair_path_has_key(const char *path, const char *prefix) {
    size_t n = strlen(prefix);
    return strncmp(path, prefix, n) == 0 &&
           strcmp(path + n, cm_pair_key) == 0;
}

static int cm_pair_write_config(const cm_cloudmoon_link_t *link) {
    const char *tmp = CM_PAIR_CONFIG_PATH ".tmp";
    FILE *f;
    int ok;
    f = fopen(tmp, "w");
    if (!f) return -1;
    ok = fprintf(f,
        "# CloudMoon session linked locally from a phone. Keep this file private.\n"
        "game=%s\nuser_id=%s\nandroid_instance_id=%s\n",
        link->game, link->user_id, link->android_instance_id);
    if (ok < 0 || fflush(f) != 0) {
        fclose(f);
        remove(tmp);
        return -1;
    }
    if (fclose(f) != 0) {
        remove(tmp);
        return -1;
    }
    if (rename(tmp, CM_PAIR_CONFIG_PATH) != 0) {
        remove(tmp);
        return -1;
    }
    return 0;
}

static void cm_pair_handle_client(int fd) {
    char *request, method[8], path[256];
    size_t header_len = 0, body_len = 0;

    request = (char *)malloc(CM_PAIR_HTTP_LIMIT + 1);
    if (!request) return;
    if (cm_pair_read_request(fd, request, CM_PAIR_HTTP_LIMIT + 1,
                             &header_len, &body_len) != 0 ||
        cm_pair_request_line(request, method, sizeof(method),
                             path, sizeof(path)) != 0) {
        (void)cm_pair_send_response(fd, 400, "Bad Request",
            "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"Solicitud HTTP inválida.\"}");
        free(request);
        return;
    }

    if (strcmp(method, "GET") == 0 &&
        cm_pair_path_has_key(path, "/?k=")) {
        FILE *f = fopen(CM_PAIR_PAGE_PATH, "rb");
        if (!f) {
            (void)cm_pair_send_response(fd, 500, "Internal Server Error",
                "text/plain; charset=utf-8", "No se encontró la página de vinculación.");
        } else {
            long size;
            char *page;
            if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 ||
                size > 128 * 1024 || fseek(f, 0, SEEK_SET) != 0) {
                fclose(f);
                (void)cm_pair_send_response(fd, 500, "Internal Server Error",
                    "text/plain; charset=utf-8", "No se pudo leer la página de vinculación.");
            } else {
                page = (char *)malloc((size_t)size);
                if (!page || fread(page, 1, (size_t)size, f) != (size_t)size) {
                    free(page);
                    fclose(f);
                    (void)cm_pair_send_response(fd, 500, "Internal Server Error",
                        "text/plain; charset=utf-8", "No se pudo cargar la página de vinculación.");
                } else {
                    char header[512];
                    int n = snprintf(header, sizeof(header),
                        "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
                        "Content-Length: %lu\r\nCache-Control: no-store\r\n"
                        "X-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n",
                        (unsigned long)size);
                    if (n > 0 && (size_t)n < sizeof(header)) {
                        (void)cm_pair_send_all(fd, header, (size_t)n);
                        (void)cm_pair_send_all(fd, page, (size_t)size);
                    }
                    free(page);
                    fclose(f);
                }
            }
        }
        free(request);
        return;
    }

    if (strcmp(method, "POST") == 0 &&
        cm_pair_path_has_key(path, "/submit?k=") &&
        body_len > 0 && body_len <= CM_PAIR_BODY_LIMIT) {
        cm_cloudmoon_link_t link;
        char session_url[CM_PAIR_BODY_LIMIT + 1];
        const char *body = request + header_len;
        memset(session_url, 0, sizeof(session_url));
        if (cm_json_get_string(body, body_len, "url",
                               session_url, sizeof(session_url)) == 0 &&
            cm_cloudmoon_parse_session_url(session_url, &link) == 0 &&
            cm_pair_write_config(&link) == 0) {
            cm_pair_linked = 1;
            ui_push_log("CloudMoon: phone session linked successfully.");
            ui_set_state(UI_STATE_LINKED);
            (void)cm_pair_send_response(fd, 200, "OK",
                "application/json; charset=utf-8",
                "{\"ok\":true,\"message\":\"Cuenta vinculada. Ya podés volver a la PS3.\"}");
        } else {
            (void)cm_pair_send_response(fd, 400, "Bad Request",
                "application/json; charset=utf-8",
                "{\"ok\":false,\"error\":\"La URL no contiene una sesión CloudMoon válida. Iniciá un juego y copiá su URL completa.\"}");
        }
        free(request);
        return;
    }

    (void)cm_pair_send_response(fd, 404, "Not Found",
        "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"Ruta no encontrada o código vencido.\"}");
    free(request);
}

static void cm_pair_worker(void *unused) {
    (void)unused;
    cm_pair_running = 1;
    while (!cm_pair_stop_requested && !cm_pair_linked) {
        fd_set readfds;
        struct timeval timeout;
        int ready, client;
        FD_ZERO(&readfds);
        FD_SET(cm_pair_listen_fd, &readfds);
        timeout.tv_sec = 0;
        timeout.tv_usec = 200000;
        ready = select(cm_pair_listen_fd + 1, &readfds, NULL, NULL, &timeout);
        if (ready < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ready == 0) continue;
        client = accept(cm_pair_listen_fd, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            break;
        }
        cm_pair_handle_client(client);
        shutdown(client, SHUT_RDWR);
        close(client);
    }
    if (cm_pair_listen_fd >= 0) {
        close(cm_pair_listen_fd);
        cm_pair_listen_fd = -1;
    }
    cm_pair_running = 0;
    sysThreadExit(0);
}

int cm_cloudmoon_pair_start(char *url, size_t url_capacity,
                            uint8_t *modules, size_t modules_capacity,
                            int *module_size) {
    union net_ctl_info info;
    struct sockaddr_in address;
    uint8_t random_bytes[16];
    uint8_t temp[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
    uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
    char candidate[CM_PAIR_URL_CAP];
    unsigned port;
    int qr_size, rc;

    if (!url || !url_capacity || !modules ||
        modules_capacity < CM_PAIR_QR_MAX_MODULES || !module_size) return -1;
    cm_cloudmoon_pair_stop();
    memset(&info, 0, sizeof(info));
    if (netCtlGetInfo(NET_CTL_INFO_IP_ADDRESS, &info) != 0 ||
        !info.ip_address[0] || strcmp(info.ip_address, "0.0.0.0") == 0) {
        ui_push_log("CloudMoon: PS3 has no active network IP address.");
        return -1;
    }
    if (cm_ps3_random_source(NULL, random_bytes, sizeof(random_bytes)) != 0) {
        ui_push_log("CloudMoon: secure QR key generation failed.");
        return -1;
    }
    {
        static const char hex[] = "0123456789abcdef";
        size_t i;
        for (i = 0; i < sizeof(random_bytes); ++i) {
            cm_pair_key[i * 2] = hex[random_bytes[i] >> 4];
            cm_pair_key[i * 2 + 1] = hex[random_bytes[i] & 15];
        }
        cm_pair_key[32] = '\0';
    }
    memset(random_bytes, 0, sizeof(random_bytes));

    cm_pair_listen_fd = (int)socket(AF_INET, SOCK_STREAM, 0);
    if (cm_pair_listen_fd < 0) {
        ui_push_log("CloudMoon: could not open the local pairing socket.");
        return -1;
    }
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = 0;
    if (bind(cm_pair_listen_fd, (struct sockaddr *)&address,
             (socklen_t)sizeof(address)) != 0) {
        close(cm_pair_listen_fd);
        cm_pair_listen_fd = -1;
        ui_push_log("CloudMoon: could not bind the local pairing socket.");
        return -1;
    }
    if (listen(cm_pair_listen_fd, 2) != 0) {
        close(cm_pair_listen_fd);
        cm_pair_listen_fd = -1;
        return -1;
    }
    {
        socklen_t address_len = (socklen_t)sizeof(address);
        if (getsockname(cm_pair_listen_fd, (struct sockaddr *)&address,
                        &address_len) != 0) {
            close(cm_pair_listen_fd);
            cm_pair_listen_fd = -1;
            return -1;
        }
    }
    port = (unsigned)ntohs(address.sin_port);
    rc = snprintf(candidate, sizeof(candidate), "http://%s:%u/?k=%s",
                  info.ip_address, port, cm_pair_key);
    if (rc < 0 || (size_t)rc >= sizeof(candidate) ||
        strlen(candidate) >= url_capacity) {
        close(cm_pair_listen_fd);
        cm_pair_listen_fd = -1;
        return -1;
    }

    if (!qrcodegen_encodeText(candidate, temp, qr, qrcodegen_Ecc_MEDIUM,
                              1, 10, qrcodegen_Mask_AUTO, true)) {
        close(cm_pair_listen_fd);
        cm_pair_listen_fd = -1;
        ui_push_log("CloudMoon: local pairing URL is too long for the QR code.");
        return -1;
    }
    qr_size = qrcodegen_getSize(qr);
    if (qr_size <= 0 || qr_size > CM_PAIR_QR_MAX_SIZE) {
        close(cm_pair_listen_fd);
        cm_pair_listen_fd = -1;
        return -1;
    }
    {
        int x, y;
        for (y = 0; y < qr_size; ++y)
            for (x = 0; x < qr_size; ++x)
                modules[(size_t)y * (size_t)qr_size + (size_t)x] =
                    qrcodegen_getModule(qr, x, y) ? 1U : 0U;
    }
    strcpy(url, candidate);
    *module_size = qr_size;
    cm_pair_stop_requested = 0;
    cm_pair_linked = 0;
    cm_pair_running = 1;
    if (sysThreadCreate(&cm_pair_thread, cm_pair_worker, NULL, 200, 0x30000,
                        THREAD_JOINABLE, "CloudMoonPair") != 0) {
        cm_pair_running = 0;
        close(cm_pair_listen_fd);
        cm_pair_listen_fd = -1;
        ui_push_log("CloudMoon: could not start the local pairing server.");
        return -1;
    }
    cm_pair_thread_started = 1;
    cm_pair_address[0] = '\0';
    strncpy(cm_pair_address, info.ip_address, sizeof(cm_pair_address) - 1);
    NLOG("CloudMoon local pairing server ready at %s:%u", cm_pair_address, port);
    return 0;
}

void cm_cloudmoon_pair_stop(void) {
    if (cm_pair_thread_started) {
        u64 result;
        cm_pair_stop_requested = 1;
        if (cm_pair_listen_fd >= 0) shutdown(cm_pair_listen_fd, SHUT_RDWR);
        sysThreadJoin(cm_pair_thread, &result);
        cm_pair_thread_started = 0;
    }
    cm_pair_running = 0;
    cm_pair_stop_requested = 0;
}

int cm_cloudmoon_pair_is_running(void) {
    return cm_pair_thread_started && cm_pair_running && !cm_pair_linked;
}
