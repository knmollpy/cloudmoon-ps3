#include "cloudmoon_pairing.h"
#include "../catalog/cloudmoon_catalog.h"
#include "../../../src/cloudmoon_session.h"

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
    char url[256];
    char status[128];
    cm_qr_t qr;
    cm_account_session_t account;
};

static const char s_pair_page[] =
"<!doctype html>"
"<html lang=\"es\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<title>Vincular CloudMoon - PS4</title>"
"<style>"
"*{box-sizing:border-box}body{margin:0;background:#08111f;color:#eaf2ff;font:16px system-ui;padding:18px}"
"main{max-width:680px;margin:3vh auto;background:#121f32;border:1px solid #29405f;border-radius:18px;padding:24px}"
"h1{color:#50dff1;font-size:25px}p{line-height:1.55;color:#b8c9de}"
"a,button{display:block;padding:14px 18px;border:0;border-radius:10px;background:#287fe0;color:white;font-weight:700;text-align:center;text-decoration:none;margin:12px 0;width:100%;font-size:16px}"
"textarea{width:100%;min-height:116px;padding:13px;border:1px solid #405777;border-radius:10px;background:#091523;color:#fff;font:14px system-ui;overflow-wrap:anywhere;resize:vertical}"
".small{font-size:13px;color:#91a7c2}.step{background:#0b1728;border:1px solid #29405f;border-radius:12px;padding:14px;margin:12px 0}"
"button:disabled{opacity:.55}#status{padding:12px;border-radius:8px;background:#0b1728;color:#50dff1;min-height:44px}"
"</style></head><body><main>"
"<h1>Vincular CloudMoon con PS4</h1>"
"<p>Esta página conecta tu cuenta de CloudMoon con la consola. No pide tu contraseña ni necesita marcadores.</p>"
"<div class=\"step\"><b>1. Abre CloudMoon</b><a href=\"https://web.cloudmoonapp.com/\" target=\"_blank\" rel=\"noopener\">Abrir CloudMoon oficial</a><p class=\"small\">Inicia sesión normalmente y abre cualquier juego hasta que aparezca la página de la partida.</p></div>"
"<div class=\"step\"><b>2. Copia el enlace de la partida</b><p class=\"small\">En la pestaña de CloudMoon, copia la dirección completa de la página donde se abrió el juego. Debe ser de web.cloudmoonapp.com e incluir el parámetro sid.</p></div>"
"<div class=\"step\"><b>3. Pega el enlace aquí</b><textarea id=\"gameUrl\" autocomplete=\"off\" autocapitalize=\"off\" spellcheck=\"false\" placeholder=\"https://web.cloudmoonapp.com/...?...sid=...\"></textarea><button id=\"pairButton\" type=\"button\">VINCULAR ESTA CUENTA</button></div>"
"<p id=\"status\" role=\"status\">Esperando el enlace de tu partida…</p>"
"<p class=\"small\">La PS4 valida el identificador de sesión directamente con CloudMoon. El identificador no se guarda en esta página.</p>"
"</main>"
"<script>"
"const statusBox=document.getElementById('status'),button=document.getElementById('pairButton');"
"button.addEventListener('click',async()=>{"
" const raw=document.getElementById('gameUrl').value.trim();"
" if(!raw){statusBox.textContent='Pega primero el enlace de la partida.';return;}"
" let sid;"
" try{"
"  const u=new URL(raw);"
"  if(u.protocol!=='https:'||u.hostname!=='web.cloudmoonapp.com'||u.username||u.password){throw new Error('El enlace debe pertenecer a web.cloudmoonapp.com.');}"
"  const values=u.searchParams.getAll('sid');"
"  if(values.length!==1||!values[0]||values[0].length>512){throw new Error('El enlace no contiene un parámetro sid válido. Abre un juego y copia su dirección completa.');}"
"  sid=values[0];"
" }catch(e){statusBox.textContent=e.message||'El enlace no es válido.';return;}"
" button.disabled=true;statusBox.textContent='Verificando tu sesión con CloudMoon…';"
" try{"
"  const response=await fetch(location.pathname,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded;charset=UTF-8'},body:'sid='+encodeURIComponent(sid),cache:'no-store'});"
"  const message=await response.text();"
"  if(!response.ok)throw new Error(message||('Error HTTP '+response.status));"
"  document.getElementById('gameUrl').value='';"
"  statusBox.textContent='Cuenta vinculada correctamente. Vuelve a tu PS4.';"
" }catch(e){statusBox.textContent='No se pudo vincular: '+(e.message||'error de conexión');button.disabled=false;}"
"});"
"</script></body></html>";

static int decode_form_sid(const char *body, size_t body_length,
                             char *output, size_t output_capacity) {
    static const char key[] = "sid=";
    size_t i = 0, used = 0;
    int found = 0;
    if (!body || !output || output_capacity == 0) return -1;
    while (i < body_length) {
        size_t start = i, end;
        while (i < body_length && body[i] != '&') ++i;
        end = i;
        if (i < body_length) ++i;
        if (end - start < sizeof(key) - 1u ||
            memcmp(body + start, key, sizeof(key) - 1u) != 0) continue;
        if (found) return -1;
        found = 1;
        start += sizeof(key) - 1u;
        while (start < end) {
            unsigned char ch = (unsigned char)body[start++];
            if (ch == '+') ch = ' ';
            else if (ch == '%') {
                int hi, lo;
                if (end - start < 2) return -1;
                hi = body[start] >= '0' && body[start] <= '9' ? body[start] - '0' :
                     body[start] >= 'a' && body[start] <= 'f' ? body[start] - 'a' + 10 :
                     body[start] >= 'A' && body[start] <= 'F' ? body[start] - 'A' + 10 : -1;
                lo = body[start + 1] >= '0' && body[start + 1] <= '9' ? body[start + 1] - '0' :
                     body[start + 1] >= 'a' && body[start + 1] <= 'f' ? body[start + 1] - 'a' + 10 :
                     body[start + 1] >= 'A' && body[start + 1] <= 'F' ? body[start + 1] - 'A' + 10 : -1;
                if (hi < 0 || lo < 0) return -1;
                ch = (unsigned char)((hi << 4) | lo);
                start += 2;
            }
            if (ch == 0 || ch < 0x20 || ch == 0x7f ||
                used + 1u >= output_capacity) return -1;
            output[used++] = (char)ch;
        }
    }
    if (!found || used < 8u) return -1;
    output[used] = 0;
    return (int)used;
}

static int pairing_make_device_uuid(char *out, size_t capacity) {
    uint8_t bytes[16];
    int n;
    if (!out || capacity < 37 ||
        cm_orbis_random_source(NULL, bytes, sizeof(bytes)) != 0) return -1;
    bytes[6] = (uint8_t)((bytes[6] & 0x0fu) | 0x40u);
    bytes[8] = (uint8_t)((bytes[8] & 0x3fu) | 0x80u);
    n = snprintf(out, capacity,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5],
        bytes[6], bytes[7], bytes[8], bytes[9], bytes[10], bytes[11],
        bytes[12], bytes[13], bytes[14], bytes[15]);
    return n == 36 ? 0 : -1;
}

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

    /* Reserve one byte for the terminating NUL before receiving the body. */
    if (header_length >= CM_PAIR_REQUEST_MAX ||
        content_length > CM_PAIR_REQUEST_MAX - header_length - 1u) {
        (void)send_response(fd, 413, "Request Too Large", "text/plain",
                            "Pairing body too large", 23);
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
        char cloud_sid[513];
        int sid_length, auth_rc = -1;
        cm_account_session_t candidate;

        memset(cloud_sid, 0, sizeof(cloud_sid));
        memset(&candidate, 0, sizeof(candidate));
        if (__sync_fetch_and_add(&service->ready, 0)) {
            static const char already[] = "Esta consola ya fue vinculada. Vuelve a la PS4.";
            (void)send_response(fd, 409, "Conflict", "text/plain",
                                already, sizeof(already) - 1);
            return;
        }
        sid_length = decode_form_sid(request + header_length, content_length,
                                     cloud_sid, sizeof(cloud_sid));
        if (sid_length < 0) {
            static const char invalid[] =
                "Enlace invalido. Comprueba que incluya ?sid=.";
            (void)send_response(fd, 400, "Bad Request", "text/plain",
                                invalid, sizeof(invalid) - 1);
            memset(cloud_sid, 0, sizeof(cloud_sid));
            return;
        }

        (void)snprintf(candidate.sid, sizeof(candidate.sid), "%s", cloud_sid);
        if (cm_account_session_fetch(service->ca_bundle_path,
                                     service->timeout_seconds, &candidate) == 0 &&
            pairing_make_device_uuid(candidate.device_id,
                                     sizeof(candidate.device_id)) == 0) {
            candidate.server_id = 21;
            (void)snprintf(candidate.language, sizeof(candidate.language), "en");
            (void)snprintf(candidate.locale, sizeof(candidate.locale), "AR");
            auth_rc = 0;
        }
        memset(cloud_sid, 0, sizeof(cloud_sid));
        if (auth_rc != 0) {
            static const char rejected[] =
                "CloudMoon no valido el enlace. Abre un juego y copia la direccion completa que contiene ?sid=.";
            (void)send_response(fd, 401, "Unauthorized", "text/plain",
                                rejected, sizeof(rejected) - 1);
            memset(&candidate, 0, sizeof(candidate));
            return;
        }

        memcpy(&service->account, &candidate, sizeof(candidate));
        __sync_lock_test_and_set(&service->ready, 1);
        (void)snprintf(service->status, sizeof(service->status),
                       "Cuenta conectada: %.90s", candidate.email);
        memset(&candidate, 0, sizeof(candidate));
    }

    {
        static const char success[] =
            "<!doctype html><meta name='viewport' content='width=device-width'>"
            "<body style='background:#08111f;color:white;font:18px system-ui;padding:24px'>"
            "<h2>CloudMoon vinculada</h2>"
            "<p>La sesión fue validada. Ya puedes volver a tu PS4.</p>"
            "</body>";
        (void)send_response(fd, 200, "OK", "text/html; charset=utf-8",
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
            {
                struct timeval client_timeout;
                client_timeout.tv_sec = 6;
                client_timeout.tv_usec = 0;
                (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
                                 &client_timeout, sizeof(client_timeout));
                (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO,
                                 &client_timeout, sizeof(client_timeout));
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
    n = snprintf(service->url, sizeof(service->url),
                 "http://%s:%d/pair/%s",
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
                   "Escanea el QR, abre CloudMoon y pega el enlace de una partida");
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
