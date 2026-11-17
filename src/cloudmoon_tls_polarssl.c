/* PSL1GHT + PolarSSL 1.3.x TLS transport with bounded I/O. */
#include "cloudmoon_tls_polarssl.h"
#include "cloudmoon_ps3_random.h"

#include <polarssl/ssl.h>
#include <polarssl/net.h>
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
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct cm_tls_connection {
    int socket_fd;
    ssl_context ssl;
    x509_crt trusted_cas;
    entropy_context entropy;
    ctr_drbg_context drbg;
    int ssl_ready;
    unsigned io_timeout_ms;
};

static uint64_t cm_now_ms(void) {
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) return 0;
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
}

static int cm_wait_socket(int fd, int writing, unsigned timeout_ms) {
    fd_set fds;
    struct timeval tv;
    int rc;
    if (fd < 0) return -1;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    tv.tv_sec = (long)(timeout_ms / 1000U);
    tv.tv_usec = (long)((timeout_ms % 1000U) * 1000U);
    do {
        rc = select(fd + 1, writing ? NULL : &fds,
                    writing ? &fds : NULL, NULL, &tv);
    } while (rc < 0 && errno == EINTR);
    return rc;
}

static unsigned cm_remaining_ms(uint64_t deadline) {
    uint64_t now = cm_now_ms();
    uint64_t remaining;
    if (!now || now >= deadline) return 0;
    remaining = deadline - now;
    return remaining > 0xffffffffULL ? 0xffffffffU : (unsigned)remaining;
}

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

static int cm_connect_with_timeout(int fd, const struct sockaddr *address,
                                   socklen_t address_len, unsigned timeout_ms) {
    int flags, error = 0;
    socklen_t error_len = (socklen_t)sizeof(error);
    fd_set writefds;
    struct timeval tv;
    int rc;
    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) return -1;
    rc = connect(fd, address, address_len);
    if (rc < 0 && errno != EINPROGRESS && errno != EWOULDBLOCK && errno != EAGAIN) {
        (void)fcntl(fd, F_SETFL, flags);
        return -1;
    }
    if (rc < 0) {
        FD_ZERO(&writefds);
        FD_SET(fd, &writefds);
        tv.tv_sec = (long)(timeout_ms / 1000U);
        tv.tv_usec = (long)((timeout_ms % 1000U) * 1000U);
        do {
            rc = select(fd + 1, NULL, &writefds, NULL, &tv);
        } while (rc < 0 && errno == EINTR);
        if (rc <= 0 || getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &error_len) < 0 || error) {
            (void)fcntl(fd, F_SETFL, flags);
            return -1;
        }
    }
    if (fcntl(fd, F_SETFL, flags) < 0) return -1;
    return 0;
}

int cm_tls_connect_polarssl(cm_tls_connection **out, const char *hostname,
                            uint16_t port, const char *ca_bundle_path,
                            unsigned timeout_seconds) {
    cm_tls_connection *c;
    struct hostent *host;
    struct sockaddr_in address;
    struct timeval timeout;
    uint64_t deadline;
    unsigned timeout_ms;
    int ret;

    if (!out || !hostname || !*hostname || !port || !ca_bundle_path ||
        !*ca_bundle_path || timeout_seconds == 0 ||
        timeout_seconds > 86400U) return -1;
    *out = NULL;
    timeout_ms = timeout_seconds * 1000U;

    c = (cm_tls_connection *)calloc(1, sizeof(*c));
    if (!c) return -1;
    c->socket_fd = -1;
    c->io_timeout_ms = timeout_ms;
    x509_crt_init(&c->trusted_cas);
    entropy_init(&c->entropy);
    memset(&c->drbg, 0, sizeof(c->drbg));
    memset(&c->ssl, 0, sizeof(c->ssl));

    ret = x509_crt_parse_file(&c->trusted_cas, ca_bundle_path);
    if (ret < 0) { cm_tls_release(c); return -1; }
    ret = ctr_drbg_init(&c->drbg, cm_entropy, NULL,
                        (const unsigned char *)"CloudMoon-PS3", 13);
    if (ret != 0) { cm_tls_release(c); return -1; }

    /* The system resolver is synchronous; all socket operations below are bounded. */
    host = gethostbyname(hostname);
    if (!host || host->h_addrtype != AF_INET || !host->h_addr_list[0] ||
        host->h_length < (int)sizeof(address.sin_addr)) {
        cm_tls_release(c);
        return -1;
    }
    c->socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (c->socket_fd < 0) { cm_tls_release(c); return -1; }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    memcpy(&address.sin_addr, host->h_addr_list[0], sizeof(address.sin_addr));
    if (cm_connect_with_timeout(c->socket_fd, (struct sockaddr *)&address,
                                (socklen_t)sizeof(address), timeout_ms) != 0) {
        cm_tls_release(c);
        return -1;
    }

    timeout.tv_sec = (long)timeout_seconds;
    timeout.tv_usec = 0;
    (void)setsockopt(c->socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    (void)setsockopt(c->socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    ret = ssl_init(&c->ssl);
    if (ret != 0) { cm_tls_release(c); return -1; }
    c->ssl_ready = 1;
    ssl_set_endpoint(&c->ssl, SSL_IS_CLIENT);
    ssl_set_authmode(&c->ssl, SSL_VERIFY_REQUIRED);
    ssl_set_ca_chain(&c->ssl, &c->trusted_cas, NULL, hostname);
    ssl_set_rng(&c->ssl, ctr_drbg_random, &c->drbg);
    ssl_set_bio(&c->ssl, cm_socket_recv, &c->socket_fd,
                cm_socket_send, &c->socket_fd);
    if (ssl_set_hostname(&c->ssl, hostname) != 0) {
        cm_tls_release(c);
        return -1;
    }
    ssl_set_min_version(&c->ssl, SSL_MAJOR_VERSION_3, SSL_MINOR_VERSION_3);
    ssl_set_max_version(&c->ssl, SSL_MAJOR_VERSION_3, SSL_MINOR_VERSION_3);

    deadline = cm_now_ms() + timeout_ms;
    for (;;) {
        ret = ssl_handshake(&c->ssl);
        if (ret != POLARSSL_ERR_NET_WANT_READ &&
            ret != POLARSSL_ERR_NET_WANT_WRITE) break;
        {
            unsigned remaining = cm_remaining_ms(deadline);
            if (!remaining || cm_wait_socket(c->socket_fd,
                    ret == POLARSSL_ERR_NET_WANT_WRITE, remaining) <= 0) {
                ret = -1;
                break;
            }
        }
    }
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
    uint64_t deadline;
    if (!c || (!data && length) || length > 0x7fffffffU) return -1;
    deadline = cm_now_ms() + c->io_timeout_ms;
    while (sent < length) {
        int n = ssl_write(&c->ssl, p + sent, length - sent);
        if (n == POLARSSL_ERR_NET_WANT_READ ||
            n == POLARSSL_ERR_NET_WANT_WRITE) {
            unsigned remaining = cm_remaining_ms(deadline);
            if (!remaining || cm_wait_socket(c->socket_fd,
                    n == POLARSSL_ERR_NET_WANT_WRITE, remaining) <= 0) return -1;
            continue;
        }
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return (int)sent;
}

int cm_tls_read_polarssl(cm_tls_connection *c, void *data, size_t capacity) {
    uint64_t deadline;
    int n;
    if (!c || !data || capacity == 0 || capacity > 0x7fffffffU) return -1;
    deadline = cm_now_ms() + c->io_timeout_ms;
    for (;;) {
        n = ssl_read(&c->ssl, (unsigned char *)data, capacity);
        if (n != POLARSSL_ERR_NET_WANT_READ &&
            n != POLARSSL_ERR_NET_WANT_WRITE) return n;
        {
            unsigned remaining = cm_remaining_ms(deadline);
            if (!remaining || cm_wait_socket(c->socket_fd,
                    n == POLARSSL_ERR_NET_WANT_WRITE, remaining) <= 0) return -1;
        }
    }
}

int cm_tls_wait_readable(cm_tls_connection *c, unsigned timeout_ms) {
    if (!c || c->socket_fd < 0) return -1;
    if (ssl_get_bytes_avail(&c->ssl) > 0) return 1;
    return cm_wait_socket(c->socket_fd, 0, timeout_ms);
}

void cm_tls_close_polarssl(cm_tls_connection *c) {
    if (!c) return;
    if (c->ssl_ready) (void)ssl_close_notify(&c->ssl);
    cm_tls_release(c);
}
