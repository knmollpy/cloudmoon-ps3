#include "cloudmoon_pairing.h"
#include "../catalog/cloudmoon_catalog.h"
#include "../../../src/cloudmoon_session.h"
#include <mbedtls/aes.h>
#include <mbedtls/md.h>

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
#define CM_PAIR_KEY_BYTES 32

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
    char pair_key[CM_PAIR_KEY_BYTES];
    char pair_key_hex[CM_PAIR_KEY_BYTES * 2 + 1];
    char status[128];
    cm_qr_t qr;
    cm_account_session_t account;
};

static const char s_pair_page[] =
"<!doctype html><html lang='es'><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>CloudMoon - Vincular PS4</title>"
"<style>"
"body{margin:0;background:#08111f;color:#eaf2ff;font:16px system-ui;padding:20px}"
"main{max-width:620px;margin:5vh auto;background:#121f32;border:1px solid #29405f;"
"border-radius:18px;padding:24px;box-shadow:0 18px 55px #0008}"
"h1{color:#50dff1;font-size:25px}p{line-height:1.55;color:#b8c9de}"
"label{display:block;margin:12px 0 6px;color:#b8c9de}"
"input,select{box-sizing:border-box;width:100%;padding:13px;margin:5px 0 12px;"
"border:1px solid #405777;border-radius:10px;background:#091523;color:#fff;font-size:16px}"
"button{padding:14px 18px;background:#2c83e8;color:#fff;border:0;border-radius:10px;"
"font-weight:700;width:100%;font-size:16px;margin:5px 0;cursor:pointer}"
"button:disabled{opacity:.55} .google{background:#fff;color:#222}"
".muted{color:#8ea5c2;font-size:13px}.divider{text-align:center;color:#8295ad;margin:15px}"
"#result{margin-top:16px;white-space:pre-wrap;line-height:1.4}"
"</style></head><body><main><h1>Vincular CloudMoon con PS4</h1>"
"<p>Inicia sesión con tu cuenta habitual de CloudMoon. La contraseña o el token "
"de Google se envían cifrados a esta PS4 y la consola los valida directamente con CloudMoon.</p>"
"<label for='server'>Región del servidor</label><select id='server'>"
"<option value='21' selected>Sudamérica</option><option value='22'>Estados Unidos Oeste</option>"
"<option value='23'>Estados Unidos Este</option><option value='3'>Asia</option>"
"<option value='4'>Europa</option></select>"
"<button class='google' id='google' type='button'>Continuar con Google</button>"
"<div class='divider'>O inicia sesión con tu cuenta CloudMoon</div>"
"<form id='passwordForm'><label for='email'>Correo electrónico</label>"
"<input id='email' type='email' autocomplete='username' required>"
"<label for='password'>Contraseña</label>"
"<input id='password' type='password' autocomplete='current-password' required>"
"<button id='passwordButton' type='submit'>Iniciar sesión</button></form>"
"<p class='muted'>Si creaste tu cuenta con Google y aún no tienes contraseña, "
"puedes iniciar sesión en <a href='https://web.cloudmoonapp.com/' target='_blank' rel='noopener'>CloudMoon</a> "
"y configurar una desde tu perfil.</p><div id='result'>Esperando inicio de sesión…</div>"
"</main><script src='https://cdnjs.cloudflare.com/ajax/libs/crypto-js/4.2.0/crypto-js.min.js'></script>"
"<script src='https://accounts.google.com/gsi/client' async defer></script>"
"<script>"
"const KEY_HEX='__PAIR_KEY__';"
"const result=document.getElementById('result'),server=document.getElementById('server');"
"const aesKey=CryptoJS.enc.Hex.parse(KEY_HEX.slice(0,32));"
"const macKey=CryptoJS.enc.Hex.parse(KEY_HEX.slice(32));"
"function encryptPayload(payload){const iv=CryptoJS.lib.WordArray.random(16);"
"const cipher=CryptoJS.AES.encrypt(CryptoJS.enc.Utf8.parse(JSON.stringify(payload)),aesKey,"
"{iv:iv,mode:CryptoJS.mode.CBC,padding:CryptoJS.pad.Pkcs7}).ciphertext;"
"const ivHex=iv.toString(CryptoJS.enc.Hex),cipherHex=cipher.toString(CryptoJS.enc.Hex);"
"const mac=CryptoJS.HmacSHA256(ivHex+cipherHex,macKey).toString(CryptoJS.enc.Hex);"
"return JSON.stringify({iv:ivHex,ciphertext:cipherHex,mac:mac});}"
"async function submitPayload(payload,button){button.disabled=true;result.textContent='Verificando la cuenta con CloudMoon…';"
"try{const response=await fetch(location.pathname,{method:'POST',headers:{'Content-Type':'application/json'},"
"body:encryptPayload(payload)});const message=await response.text();"
"if(!response.ok)throw new Error(message);result.textContent=message;"
"result.style.color='#5ce0b2';}catch(error){result.textContent='No se pudo vincular: '+error.message;"
"result.style.color='#ff9b9b';button.disabled=false;}}"
"document.getElementById('passwordForm').addEventListener('submit',event=>{event.preventDefault();"
"submitPayload({provider:'password',email:document.getElementById('email').value,"
"password:document.getElementById('password').value,server_id:Number(server.value),"
"language:'es',locale:'ar'},document.getElementById('passwordButton'));});"
"function initGoogle(){if(!window.google||!google.accounts||!google.accounts.id)return;"
"google.accounts.id.initialize({client_id:'196443591263-k5447s9icscrq54n57j29lmvm05addbe.apps.googleusercontent.com',"
"callback:response=>submitPayload({provider:'google',google_id_token:response.credential,"
"server_id:Number(server.value),language:'es',locale:'ar'},document.getElementById('google')),"
"auto_select:false,cancel_on_tap_outside:true});"
"google.accounts.id.renderButton(document.getElementById('google'),{theme:'filled_blue',size:'large',"
"text:'continue_with',shape:'rectangular',width:320});}"
"document.getElementById('google').addEventListener('click',()=>{if(window.google&&google.accounts)"
"google.accounts.id.prompt();});"
"window.addEventListener('load',()=>{const start=Date.now();const timer=setInterval(()=>{"
"if(window.google&&google.accounts){clearInterval(timer);initGoogle();}"
"else if(Date.now()-start>10000){clearInterval(timer);document.getElementById('google').textContent='Google no disponible en este navegador';}},200);});"
"</script></body></html>";

static int build_pair_page(const cm_pairing_service_t *service,
                           char *output, size_t capacity) {
    static const char marker[] = "__PAIR_KEY__";
    const char *where;
    size_t prefix, suffix, key_length;
    if (!service || !output || capacity == 0) return -1;
    where = strstr(s_pair_page, marker);
    if (!where) return -1;
    prefix = (size_t)(where - s_pair_page);
    key_length = strlen(service->pair_key_hex);
    suffix = strlen(where + sizeof(marker) - 1u);
    if (prefix + key_length + suffix + 1u > capacity) return -1;
    memcpy(output, s_pair_page, prefix);
    memcpy(output + prefix, service->pair_key_hex, key_length);
    memcpy(output + prefix + key_length,
           where + sizeof(marker) - 1u, suffix + 1u);
    return (int)(prefix + key_length + suffix);
}

static int hex_decode(const char *input, uint8_t *output, size_t capacity) {
    size_t i, length;
    if (!input || !output) return -1;
    length = strlen(input);
    if ((length & 1u) != 0 || length / 2u > capacity) return -1;
    for (i = 0; i < length; i += 2u) {
        int a, b;
        char ca = input[i], cb = input[i + 1u];
        a = ca >= '0' && ca <= '9' ? ca - '0' :
            ca >= 'a' && ca <= 'f' ? ca - 'a' + 10 :
            ca >= 'A' && ca <= 'F' ? ca - 'A' + 10 : -1;
        b = cb >= '0' && cb <= '9' ? cb - '0' :
            cb >= 'a' && cb <= 'f' ? cb - 'a' + 10 :
            cb >= 'A' && cb <= 'F' ? cb - 'A' + 10 : -1;
        if (a < 0 || b < 0) return -1;
        output[i / 2u] = (uint8_t)((a << 4) | b);
    }
    return (int)(length / 2u);
}

static int decrypt_pair_payload(const cm_pairing_service_t *service,
                                const char *envelope, size_t envelope_length,
                                char *plaintext, size_t plaintext_capacity) {
    char iv_hex[33], cipher_hex[7000], mac_hex[65];
    char authenticated[7100];
    uint8_t iv[16], cipher[3500], expected_mac[32], actual_mac[32];
    size_t cipher_length, plain_length, i;
    const mbedtls_md_info_t *md;
    mbedtls_aes_context aes;
    int padding, diff = 0, rc = -1;

    if (!service || !envelope || !plaintext || plaintext_capacity == 0 ||
        cm_json_get_string(envelope, envelope_length, "iv",
                           iv_hex, sizeof(iv_hex)) < 0 ||
        cm_json_get_string(envelope, envelope_length, "ciphertext",
                           cipher_hex, sizeof(cipher_hex)) < 0 ||
        cm_json_get_string(envelope, envelope_length, "mac",
                           mac_hex, sizeof(mac_hex)) < 0 ||
        strlen(iv_hex) != 32 || strlen(mac_hex) != 64)
        return -1;
    if (hex_decode(iv_hex, iv, sizeof(iv)) != 16 ||
        hex_decode(mac_hex, expected_mac, sizeof(expected_mac)) != 32)
        return -1;
    {
        int decoded = hex_decode(cipher_hex, cipher, sizeof(cipher));
        if (decoded <= 0 || (decoded % 16) != 0) return -1;
        cipher_length = (size_t)decoded;
    }
    if (snprintf(authenticated, sizeof(authenticated), "%s%s",
                 iv_hex, cipher_hex) < 0 ||
        strlen(authenticated) >= sizeof(authenticated))
        return -1;
    md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!md || mbedtls_md_hmac(md, (const unsigned char *)service->pair_key + 16,
                               16, (const unsigned char *)authenticated,
                               strlen(authenticated), actual_mac) != 0)
        return -1;
    for (i = 0; i < sizeof(actual_mac); ++i)
        diff |= actual_mac[i] ^ expected_mac[i];
    if (diff != 0) return -1;

    mbedtls_aes_init(&aes);
    if (mbedtls_aes_setkey_dec(&aes,
            (const unsigned char *)service->pair_key, 128) != 0)
        goto done;
    if (mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, cipher_length,
                              iv, cipher, cipher) != 0)
        goto done;
    padding = cipher[cipher_length - 1u];
    if (padding < 1 || padding > 16 || (size_t)padding > cipher_length)
        goto done;
    for (i = 0; i < (size_t)padding; ++i)
        if (cipher[cipher_length - 1u - i] != (uint8_t)padding) goto done;
    plain_length = cipher_length - (size_t)padding;
    if (plain_length == 0 || plain_length + 1u > plaintext_capacity)
        goto done;
    memcpy(plaintext, cipher, plain_length);
    plaintext[plain_length] = 0;
    rc = (int)plain_length;
done:
    mbedtls_aes_free(&aes);
    memset(cipher, 0, sizeof(cipher));
    memset(actual_mac, 0, sizeof(actual_mac));
    return rc;
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
        char page[8192];
        int page_length = build_pair_page(service, page, sizeof(page));
        if (page_length < 0) {
            (void)send_response(fd, 500, "Internal Server Error", "text/plain",
                                "Could not prepare pairing page", 31);
            return;
        }
        (void)send_response(fd, 200, "OK", "text/html; charset=utf-8",
                            page, (size_t)page_length);
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
        cm_account_session_t candidate;
        memset(&candidate, 0, sizeof(candidate));
        if (cm_account_session_parse_url(request + header_length,
                                         &candidate) != 0 ||
            cm_account_session_fetch(service->ca_bundle_path,
                                     service->timeout_seconds,
                                     &candidate) != 0) {
            {
                static const char rejected[] =
                    "CloudMoon rejected this session link. Make sure it "
                    "contains a valid ?sid= value and the game session is active.";
                (void)send_response(fd, 400, "Bad Request", "text/plain",
                                   rejected, sizeof(rejected) - 1);
            }
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
    uint8_t random_bytes[CM_PAIR_NONCE_BYTES + CM_PAIR_KEY_BYTES];
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
    memcpy(service->pair_key, random_bytes + CM_PAIR_NONCE_BYTES,
           CM_PAIR_KEY_BYTES);
    for (i = 0; i < CM_PAIR_KEY_BYTES; ++i) {
        service->pair_key_hex[i * 2] =
            "0123456789abcdef"[(unsigned char)service->pair_key[i] >> 4];
        service->pair_key_hex[i * 2 + 1] =
            "0123456789abcdef"[(unsigned char)service->pair_key[i] & 15];
    }
    service->pair_key_hex[CM_PAIR_KEY_BYTES * 2] = 0;

    n = snprintf(service->url, sizeof(service->url),
                 "http://%s:%d/pair/%s#k=%s",
                 service->ip, CM_PAIR_PORT, service->nonce,
                 service->pair_key_hex);
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
                   "Escanea el QR e inicia sesion en CloudMoon desde el telefono");
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
