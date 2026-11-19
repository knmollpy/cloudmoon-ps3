#include "cloudmoon_pairing.h"

#include "../net/cloudmoon_random_mbedtls.h"
#include "../platform/orbis_network.h"

#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#define CM_PAIR_PORT 8787
#define CM_PAIR_REQUEST_MAX 16384
#define CM_PAIR_BODY_MAX 8192
#define CM_PAIR_NONCE_BYTES 16

struct cm_pairing_service {
    int listen_fd;
    volatile int running;
    volatile int ready;
    pthread_t thread;
    char ca_bundle_path[256];
    unsigned timeout_seconds;
    char nonce[CM_PAIR_NONCE_BYTES * 2 + 1];
    char ip[16];
    char url[128];
    char status[128];
    cm_qr_t qr;
    cm_account_session_t account;
};

static const char s_pair_page[] =
"<!doctype html><html lang='es'><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>CloudMoon - Vincular PS4</title>"
"<style>"
"body{margin:0;background:#08111f;color:#eaf2ff;font:16px system-ui;padding:24px}"
"main{max-width:620px;margin:8vh auto;background:#121f32;border:1px solid #29405f;"
"border-radius:18px;padding:28px;box-shadow:0 18px 55px #0008}"
"h1{color:#50dff1;font-size:26px}p,li{line-height:1.6;color:#b8c9de}"
"a{color:#50dff1}input{box-sizing:border-box;width:100%;padding:14px;margin:12px 0;"
"border:1px solid #405777;border-radius:10px;background:#091523;color:#fff}"
"button{padding:14px 20px;background:#2c83e8;color:#fff;border:0;border-radius:10px;"
"font-weight:700;width:100%}#result{margin-top:16px;white-space:pre-wrap}"
"</style><main><h1>Vincular CloudMoon con PS4</h1>"
"<p>La PS4 está esperando una sesión autorizada de CloudMoon en esta red local.</p>"
"<ol><li>Abre <a href='https://web.cloudmoonapp.com/' target='_blank' rel='noopener'>"
"CloudMoon</a> e inicia sesión en tu teléfono.</li>"
"<li>Abre cualquier juego para crear una sesión web.</li>"
"<li>Vuelve aquí y pega la dirección de la página de juego. Debe contener "
"<code>?sid=</code>.</li></ol>"
"<form id='f'><label for='u'>Enlace de sesión de CloudMoon</label>"
"<input id='u' type='url' autocomplete='off' required "
"placeholder='https://web.cloudmoonapp.com/run-site/?sid=...'>"
"<button id='b' type='submit'>Vincular cuenta con PS4</button></form>"
"<div id='result'></div></main><script>"
"const f=document.getElementById('f'),u=document.getElementById('u'),"
"b=document.getElementById('b'),r=document.getElementById('result');"
"f.addEventListener('submit',async e=>{e.preventDefault();b.disabled=true;"
"r.textContent='Verificando la sesión con CloudMoon...';try{"
"const x=await fetch(location.pathname,{method:'POST',headers:{"
"'Content-Type':'text/plain;charset=UTF-8'},body:u.value.trim()});"
"const t=await x.text();r.textContent=t;if(!x.ok)throw new Error(t);"
"b.textContent='Cuenta vinculada';}catch(e){b.disabled=false;"
"r.textContent='No se pudo vincular: '+e.message;}});</script></html>";

static int send_all(int fd, const char *data, size_t length) {
    size_t sent = 0;
    while (sent < length) {
        ssize_t n = send(fd, data + sent, length - sent, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

static int send_response(int fd, int status, const char *reason,
                         const char *content_type,
                         const char *body, size_t body_length) {
    char header[512];
    int n = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %lu\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n"
        "X-Content-Type-Options: nosniff\r\n\r\n",
        status, reason, content_type, (unsigned long)body_length);
    if (n < 0 || (size_t)n >= sizeof(header)) return -1;
    if (send_all(fd, header, (size_t)n) != 0) return -1;
    return body_length ? send_all(fd, body, body_length) : 0;
}

static int parse_content_length(const char *headers, size_t length,
                                size_t *out) {
    const char *p = headers, *end = headers + length;
    while (p < end) {
        const char *line_end = p;
        const char *colon;
        while (line_end + 1 < end &&
               !(line_end[0] == '\r' && line_end[1] == '\n'))
            ++line_end;
        if (line_end + 1 >= end) line_end = end;
        colon = memchr(p, ':', (size_t)(line_end - p));
        if (colon && (size_t)(colon - p) == 14 &&
            strncasecmp(p, "Content-Length", 14) == 0) {
            char number[24], *tail;
            size_t n = (size_t)(line_end - colon - 1);
            unsigned long v;
            while (n && (colon[1] == ' ' || colon[1] == '\t')) {
                ++colon; --n;
            }
            if (!n || n >= sizeof(number)) return -1;
            memcpy(number, colon + 1, n);
            number[n] = 0;
            errno = 0;
            v = strtoul(number, &tail, 10);
            if (errno || tail == number || *tail || v > CM_PAIR_BODY_MAX)
                return -1;
            *out = (size_t)v;
            return 0;
        }
        if (line_end >= end || end - line_end < 2) break;
        p = line_end + 2;
    }
    *out = 0;
    return 0;
}

static int valid_pair_path(const cm_pairing_service_t *service,
                           const char *path) {
    char expected[96];
    int n = snprintf(expected, sizeof(expected), "/pair/%s", service->nonce);
    return n > 0 && (size_t)n < sizeof(expected) &&
           strcmp(path, expected) == 0;
}

static void handle_client(cm_pairing_service_t *service, int fd) {
    char request[CM_PAIR_REQUEST_MAX + 1];
    char method[8], path[256];
    const char *header_end;
    size_t used = 0, content_length = 0, header_length;
    int n;

    while (used < CM_PAIR_REQUEST_MAX) {
        ssize_t got = recv(fd, request + used, CM_PAIR_REQUEST_MAX - used, 0);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) return;
        used += (size_t)got;
        request[used] = 0;
        header_end = strstr(request, "\r\n\r\n");
        if (header_end) break;
    }
    if (!header_end) {
        (void)send_response(fd, 413, "Request Too Large", "text/plain",
                            "Request too large", 18);
        return;
    }
    header_length = (size_t)(header_end + 4 - request);
    if (sscanf(request, "%7s %255s", method, path) != 2 ||
        !valid_pair_path(service, path)) {
        (void)send_response(fd, 404, "Not Found", "text/plain",
                            "Unknown pairing link", 20);
        return;
    }

    if (strcmp(method, "GET") == 0) {
        (void)send_response(fd, 200, "OK", "text/html; charset=utf-8",
                            s_pair_page, sizeof(s_pair_page) - 1);
        return;
    }

    if (strcmp(method, "POST") != 0 ||
        parse_content_length(request, header_length, &content_length) != 0 ||
        content_length == 0) {
        (void)send_response(fd, 400, "Bad Request", "text/plain",
                            "Invalid pairing request", 24);
        return;
    }

    while (used - header_length < content_length &&
           used < CM_PAIR_REQUEST_MAX) {
        ssize_t got = recv(fd, request + used, CM_PAIR_REQUEST_MAX - used, 0);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) break;
        used += (size_t)got;
    }
    if (used - header_length < content_length) {
        (void)send_response(fd, 400, "Bad Request", "text/plain",
                            "Incomplete request body", 23);
        return;
    }
    request[header_length + content_length] = 0;

    {
        cm_account_session_t candidate;
        memset(&candidate, 0, sizeof(candidate));
        if (cm_account_session_parse_url(request + header_length,
                                         &candidate) != 0 ||
            cm_account_session_fetch(service->ca_bundle_path,
                                     service->timeout_seconds,
                                     &candidate) != 0) {
            (void)send_response(fd, 400, "Bad Request", "text/plain",
                                "CloudMoon rejected this session link. "
                                "Make sure it contains a valid ?sid= value "
                                "and that the game session is still active.",
                                130);
            return;
        }

        memcpy(&service->account, &candidate, sizeof(candidate));
        __sync_lock_test_and_set(&service->ready, 1);
        (void)snprintf(service->status, sizeof(service->status),
                       "Cuenta vinculada: %.90s", candidate.email);
    }

    {
        static const char success[] =
            "Cuenta vinculada correctamente. Ya puedes volver a la PS4.";
        (void)send_response(fd, 200, "OK", "text/plain; charset=utf-8",
                            success, sizeof(success) - 1);
    }
}

static void *pairing_thread(void *argument) {
    cm_pairing_service_t *service = (cm_pairing_service_t *)argument;
    while (service && __sync_fetch_and_add(&service->running, 0)) {
        fd_set readfds;
        struct timeval timeout;
        int ready;
        FD_ZERO(&readfds);
        FD_SET(service->listen_fd, &readfds);
        timeout.tv_sec = 0;
        timeout.tv_usec = 250000;
        ready = select(service->listen_fd + 1, &readfds, NULL, NULL, &timeout);
        if (ready < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ready == 0) continue;
        {
            int fd = accept(service->listen_fd, NULL, NULL);
            if (fd < 0) {
                if (errno == EINTR || errno == EAGAIN) continue;
                break;
            }
            handle_client(service, fd);
            shutdown(fd, SHUT_RDWR);
            close(fd);
        }
    }
    return NULL;
}

int cm_pairing_start(cm_pairing_service_t **out,
                     const char *ca_bundle_path,
                     unsigned timeout_seconds) {
    cm_pairing_service_t *service;
    uint8_t random_bytes[CM_PAIR_NONCE_BYTES];
    static const char hex[] = "0123456789ABCDEF";
    struct sockaddr_in address;
    int i, n;

    if (!out || !ca_bundle_path || !ca_bundle_path[0] ||
        strlen(ca_bundle_path) >= sizeof(service->ca_bundle_path) ||
        timeout_seconds == 0)
        return -1;
    *out = NULL;

    if (cm_orbis_network_init() != 0 || cm_orbis_random_init() != 0)
        return -2;

    service = (cm_pairing_service_t *)calloc(1, sizeof(*service));
    if (!service) return -3;
    service->listen_fd = -1;
    service->timeout_seconds = timeout_seconds;
    (void)snprintf(service->ca_bundle_path, sizeof(service->ca_bundle_path),
                   "%s", ca_bundle_path);
    if (cm_orbis_network_ipv4(service->ip, sizeof(service->ip)) != 0) {
        free(service);
        return -4;
    }
    if (cm_orbis_random_source(NULL, random_bytes, sizeof(random_bytes)) != 0) {
        free(service);
        return -5;
    }
    for (i = 0; i < CM_PAIR_NONCE_BYTES; ++i) {
        service->nonce[i * 2] = hex[random_bytes[i] >> 4];
        service->nonce[i * 2 + 1] = hex[random_bytes[i] & 15];
    }
    service->nonce[CM_PAIR_NONCE_BYTES * 2] = 0;

    n = snprintf(service->url, sizeof(service->url), "http://%s:%d/pair/%s",
                 service->ip, CM_PAIR_PORT, service->nonce);
    if (n < 0 || (size_t)n >= sizeof(service->url) ||
        cm_qr_encode(service->url, &service->qr) < 0) {
        free(service);
        return -6;
    }

    service->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (service->listen_fd < 0) {
        free(service);
        return -7;
    }
    {
        int enabled = 1;
        (void)setsockopt(service->listen_fd, SOL_SOCKET, SO_REUSEADDR,
                         &enabled, sizeof(enabled));
    }
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(CM_PAIR_PORT);
    address.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(service->listen_fd, (struct sockaddr *)&address,
             sizeof(address)) != 0 ||
        listen(service->listen_fd, 2) != 0) {
        close(service->listen_fd);
        free(service);
        return -8;
    }

    service->running = 1;
    (void)snprintf(service->status, sizeof(service->status),
                   "Escanea el QR con el telefono y pega el enlace de sesion");
    if (pthread_create(&service->thread, NULL, pairing_thread, service) != 0) {
        close(service->listen_fd);
        free(service);
        return -9;
    }

    *out = service;
    return 0;
}

void cm_pairing_stop(cm_pairing_service_t *service) {
    if (!service) return;
    __sync_lock_test_and_set(&service->running, 0);
    if (service->listen_fd >= 0) {
        shutdown(service->listen_fd, SHUT_RDWR);
        close(service->listen_fd);
        service->listen_fd = -1;
    }
    (void)pthread_join(service->thread, NULL);
    free(service);
}

int cm_pairing_take_account(cm_pairing_service_t *service,
                            cm_account_session_t *account) {
    if (!service || !account ||
        !__sync_fetch_and_add(&service->ready, 0))
        return 0;
    memcpy(account, &service->account, sizeof(*account));
    __sync_lock_test_and_set(&service->ready, 0);
    return 1;
}

const char *cm_pairing_status(const cm_pairing_service_t *service) {
    return service ? service->status : "No disponible";
}

const char *cm_pairing_url(const cm_pairing_service_t *service) {
    return service ? service->url : "";
}

const cm_qr_t *cm_pairing_qr(const cm_pairing_service_t *service) {
    return service ? &service->qr : NULL;
}
