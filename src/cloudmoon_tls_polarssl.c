/* PSL1GHT + PolarSSL 1.3.x TLS transport.
 * This file is intentionally excluded from host tests; it needs the PS3 SDK
 * and the PolarSSL headers/libraries shipped with the target toolchain.
 */
#include "cloudmoon_tls_polarssl.h"
#include "cloudmoon_ps3_random.h"

#include <polarssl/ssl.h>
#include <polarssl/x509_crt.h>
#include <polarssl/ctr_drbg.h>
#include <polarssl/entropy.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

struct cm_tls_connection {
    int socket_fd;
    ssl_context ssl;
    x509_crt trusted_cas;
    entropy_context entropy;
    ctr_drbg_context drbg;
    int ssl_ready;
};

static int cm_entropy(void *ctx, unsigned char *out, size_t len) {
    return cm_ps3_random_source(ctx, out, len);
}

static int cm_socket_send(void *ctx, const unsigned char *buf, size_t len) {
    int fd = *(int *)ctx;
    int n = (int)send(fd, buf, len, 0);
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        return POLARSSL_ERR_NET_WANT_WRITE;
    if (n < 0 && errno == EINTR) return POLARSSL_ERR_NET_WANT_WRITE;
    return n;
}

static int cm_socket_recv(void *ctx, unsigned char *buf, size_t len) {
    int fd = *(int *)ctx;
    int n = (int)recv(fd, buf, len, 0);
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        return POLARSSL_ERR_NET_WANT_READ;
    if (n < 0 && errno == EINTR) return POLARSSL_ERR_NET_WANT_READ;
    return n;
}

static void cm_tls_release(cm_tls_connection *c) {
    if (!c) return;
    if (c->ssl_ready) ssl_free(&c->ssl);
    x509_crt_free(&c->trusted_cas);
    ctr_drbg_free(&c->drbg);
    entropy_free(&c->entropy);
    if (c->socket_fd >= 0) close(c->socket_fd);
    free(c);
}

int cm_tls_connect_polarssl(cm_tls_connection **out, const char *hostname,
                            uint16_t port, const char *ca_bundle_path,
                            unsigned timeout_seconds) {
    cm_tls_connection *c;
    struct hostent *host;
    struct sockaddr_in address;
    struct timeval timeout;
    int ret;

    if (!out || !hostname || !*hostname || !port || !ca_bundle_path ||
        !*ca_bundle_path || timeout_seconds == 0) return -1;
    *out = NULL;

    c = (cm_tls_connection *)calloc(1, sizeof(*c));
    if (!c) return -1;
    c->socket_fd = -1;
    x509_crt_init(&c->trusted_cas);
    entropy_init(&c->entropy);
    memset(&c->drbg, 0, sizeof(c->drbg));
    memset(&c->ssl, 0, sizeof(c->ssl));

    /* Trust store is mandatory. An absent/invalid CA bundle is fatal. */
    ret = x509_crt_parse_file(&c->trusted_cas, ca_bundle_path);
    if (ret < 0) { cm_tls_release(c); return -1; }

    ret = ctr_drbg_init(&c->drbg, cm_entropy, NULL,
                        (const unsigned char *)"CloudMoon-PS3", 13);
    if (ret != 0) { cm_tls_release(c); return -1; }

    host = gethostbyname(hostname);
    if (!host || host->h_addrtype != AF_INET || !host->h_addr_list[0]) {
        cm_tls_release(c);
        return -1;
    }

    c->socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (c->socket_fd < 0) { cm_tls_release(c); return -1; }
    timeout.tv_sec = (long)timeout_seconds;
    timeout.tv_usec = 0;
    (void)setsockopt(c->socket_fd, SOL_SOCKET, SO_RCVTIMEO,
                     &timeout, sizeof(timeout));
    (void)setsockopt(c->socket_fd, SOL_SOCKET, SO_SNDTIMEO,
                     &timeout, sizeof(timeout));

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    memcpy(&address.sin_addr, host->h_addr_list[0], sizeof(address.sin_addr));
    if (connect(c->socket_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        cm_tls_release(c);
        return -1;
    }

    ret = ssl_init(&c->ssl);
    if (ret != 0) { cm_tls_release(c); return -1; }
    c->ssl_ready = 1;
    ssl_set_endpoint(&c->ssl, SSL_IS_CLIENT);
    ssl_set_authmode(&c->ssl, SSL_VERIFY_REQUIRED);
    ssl_set_ca_chain(&c->ssl, &c->trusted_cas, NULL, hostname);
    ssl_set_rng(&c->ssl, ctr_drbg_random, &c->drbg);
    ssl_set_bio(&c->ssl, cm_socket_recv, &c->socket_fd,
                cm_socket_send, &c->socket_fd);

    /* SNI and peer-name verification use the same requested DNS hostname. */
    if (ssl_set_hostname(&c->ssl, hostname) != 0) {
        cm_tls_release(c);
        return -1;
    }
    ssl_set_min_version(&c->ssl, SSL_MAJOR_VERSION_3, SSL_MINOR_VERSION_3);
    ssl_set_max_version(&c->ssl, SSL_MAJOR_VERSION_3, SSL_MINOR_VERSION_3);

    do {
        ret = ssl_handshake(&c->ssl);
    } while (ret == POLARSSL_ERR_NET_WANT_READ ||
             ret == POLARSSL_ERR_NET_WANT_WRITE);
    if (ret != 0 || c->ssl.state != SSL_HANDSHAKE_OVER ||
        ssl_get_verify_result(&c->ssl) != 0) {
        cm_tls_release(c);
        return -1;
    }

    *out = c;
    return 0;
}

int cm_tls_write_polarssl(cm_tls_connection *c,
                          const void *data, size_t length) {
    const unsigned char *p = (const unsigned char *)data;
    size_t sent = 0;
    if (!c || (!data && length)) return -1;
    while (sent < length) {
        int n = ssl_write(&c->ssl, p + sent, length - sent);
        if (n == POLARSSL_ERR_NET_WANT_READ ||
            n == POLARSSL_ERR_NET_WANT_WRITE) continue;
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return (int)sent;
}

int cm_tls_read_polarssl(cm_tls_connection *c, void *data, size_t capacity) {
    int n;
    if (!c || !data || capacity == 0 || capacity > 0x7fffffffU) return -1;
    do {
        n = ssl_read(&c->ssl, (unsigned char *)data, capacity);
    } while (n == POLARSSL_ERR_NET_WANT_READ ||
             n == POLARSSL_ERR_NET_WANT_WRITE);
    return n;
}


int cm_tls_wait_readable(cm_tls_connection *c, unsigned timeout_ms) {
    fd_set readfds;
    struct timeval timeout;
    int rc;
    if (!c || c->socket_fd < 0) return -1;
    if (ssl_get_bytes_avail(&c->ssl) > 0) return 1;
    FD_ZERO(&readfds);
    FD_SET(c->socket_fd, &readfds);
    timeout.tv_sec = (long)(timeout_ms / 1000U);
    timeout.tv_usec = (long)((timeout_ms % 1000U) * 1000U);
    rc = select(c->socket_fd + 1, &readfds, NULL, NULL, &timeout);
    if (rc < 0 && errno == EINTR) return 0;
    return rc < 0 ? -1 : rc;
}

void cm_tls_close_polarssl(cm_tls_connection *c) {
    if (!c) return;
    if (c->ssl_ready) (void)ssl_close_notify(&c->ssl);
    cm_tls_release(c);
}
