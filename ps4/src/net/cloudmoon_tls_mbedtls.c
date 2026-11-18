/*
 * CloudMoon PS4 TLS backend using Mbed TLS 3.x.
 *
 * The legacy PolarSSL symbol names are retained because the shared
 * cloudmoon_ws_tls.c transport uses this stable backend interface.
 * Certificate-chain and hostname verification are always mandatory.
 */
#include "cloudmoon_tls_polarssl.h"
#include "cloudmoon_mbedtls_entropy.h"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

struct cm_tls_connection {
    int socket_fd;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    mbedtls_x509_crt trusted_cas;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    int initialized;
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

static int cm_socket_send(void *context, const unsigned char *buffer,
                          size_t length) {
    const int fd = *(const int *)context;
    ssize_t written = send(fd, buffer, length, 0);
    if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK ||
                        errno == EINTR))
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    return written > 0 ? (int)written : (int)written;
}

static int cm_socket_recv(void *context, unsigned char *buffer,
                          size_t capacity) {
    const int fd = *(const int *)context;
    ssize_t received = recv(fd, buffer, capacity, 0);
    if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK ||
                         errno == EINTR))
        return MBEDTLS_ERR_SSL_WANT_READ;
    return received > 0 ? (int)received : (int)received;
}

static void cm_tls_release(cm_tls_connection *connection) {
    if (!connection) return;
    if (connection->initialized) {
        mbedtls_ssl_free(&connection->ssl);
        mbedtls_ssl_config_free(&connection->config);
    }
    mbedtls_x509_crt_free(&connection->trusted_cas);
    mbedtls_ctr_drbg_free(&connection->drbg);
    mbedtls_entropy_free(&connection->entropy);
    if (connection->socket_fd >= 0) close(connection->socket_fd);
    free(connection);
}

static int cm_connect_with_timeout(int fd, const struct sockaddr *address,
                                   socklen_t address_length,
                                   unsigned timeout_ms) {
    int flags, error = 0;
    socklen_t error_length = (socklen_t)sizeof(error);
    fd_set writefds;
    struct timeval tv;
    int rc;

    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) return -1;

    rc = connect(fd, address, address_length);
    if (rc < 0 && errno != EINPROGRESS && errno != EWOULDBLOCK &&
        errno != EAGAIN) {
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

        if (rc <= 0 ||
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &error_length) < 0 ||
            error != 0) {
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
    cm_tls_connection *connection;
    struct hostent *host;
    struct sockaddr_in address;
    struct timeval timeout;
    uint64_t deadline;
    unsigned timeout_ms;
    int rc;

    if (!out || !hostname || !hostname[0] || !port ||
        !ca_bundle_path || !ca_bundle_path[0] ||
        timeout_seconds == 0 || timeout_seconds > 86400U)
        return -1;
    *out = NULL;
    timeout_ms = timeout_seconds * 1000U;

    connection = (cm_tls_connection *)calloc(1, sizeof(*connection));
    if (!connection) return -1;
    connection->socket_fd = -1;
    connection->io_timeout_ms = timeout_ms;

    mbedtls_ssl_init(&connection->ssl);
    mbedtls_ssl_config_init(&connection->config);
    mbedtls_x509_crt_init(&connection->trusted_cas);
    mbedtls_entropy_init(&connection->entropy);
    mbedtls_ctr_drbg_init(&connection->drbg);
    connection->initialized = 1;

    rc = cm_orbis_entropy_add(&connection->entropy);
    if (rc != 0) goto fail;

    rc = mbedtls_x509_crt_parse_file(&connection->trusted_cas,
                                     ca_bundle_path);
    if (rc < 0) goto fail;

    rc = mbedtls_ctr_drbg_seed(&connection->drbg, mbedtls_entropy_func,
                               &connection->entropy,
                               (const unsigned char *)"CloudMoon-PS4",
                               sizeof("CloudMoon-PS4") - 1);
    if (rc != 0) goto fail;

    /* Resolve IPv4 before opening the socket; socket I/O is deadline-bounded. */
    host = gethostbyname(hostname);
    if (!host || host->h_addrtype != AF_INET || !host->h_addr_list ||
        !host->h_addr_list[0] ||
        host->h_length < (int)sizeof(address.sin_addr))
        goto fail;

    connection->socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (connection->socket_fd < 0) goto fail;

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    memcpy(&address.sin_addr, host->h_addr_list[0],
           sizeof(address.sin_addr));

    if (cm_connect_with_timeout(connection->socket_fd,
                                (const struct sockaddr *)&address,
                                (socklen_t)sizeof(address), timeout_ms) != 0)
        goto fail;

    timeout.tv_sec = (long)timeout_seconds;
    timeout.tv_usec = 0;
    (void)setsockopt(connection->socket_fd, SOL_SOCKET, SO_RCVTIMEO,
                     &timeout, sizeof(timeout));
    (void)setsockopt(connection->socket_fd, SOL_SOCKET, SO_SNDTIMEO,
                     &timeout, sizeof(timeout));

    rc = mbedtls_ssl_config_defaults(&connection->config,
                                     MBEDTLS_SSL_IS_CLIENT,
                                     MBEDTLS_SSL_TRANSPORT_STREAM,
                                     MBEDTLS_SSL_PRESET_DEFAULT);
    if (rc != 0) goto fail;

    mbedtls_ssl_conf_authmode(&connection->config,
                              MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&connection->config,
                              &connection->trusted_cas, NULL);
    mbedtls_ssl_conf_rng(&connection->config, mbedtls_ctr_drbg_random,
                         &connection->drbg);
    mbedtls_ssl_conf_min_version(&connection->config,
                                 MBEDTLS_SSL_MAJOR_VERSION_3,
                                 MBEDTLS_SSL_MINOR_VERSION_3);
    mbedtls_ssl_conf_max_version(&connection->config,
                                 MBEDTLS_SSL_MAJOR_VERSION_3,
                                 MBEDTLS_SSL_MINOR_VERSION_3);

    rc = mbedtls_ssl_setup(&connection->ssl, &connection->config);
    if (rc != 0) goto fail;

    rc = mbedtls_ssl_set_hostname(&connection->ssl, hostname);
    if (rc != 0) goto fail;

    mbedtls_ssl_set_bio(&connection->ssl, &connection->socket_fd,
                        cm_socket_send, cm_socket_recv, NULL);

    deadline = cm_now_ms() + timeout_ms;
    for (;;) {
        rc = mbedtls_ssl_handshake(&connection->ssl);
        if (rc == 0) break;
        if (rc != MBEDTLS_ERR_SSL_WANT_READ &&
            rc != MBEDTLS_ERR_SSL_WANT_WRITE)
            goto fail;
        {
            unsigned remaining = cm_remaining_ms(deadline);
            int ready;
            if (!remaining) goto fail;
            ready = cm_wait_socket(connection->socket_fd,
                                   rc == MBEDTLS_ERR_SSL_WANT_WRITE,
                                   remaining);
            if (ready <= 0) goto fail;
        }
    }

    if (mbedtls_ssl_get_verify_result(&connection->ssl) != 0)
        goto fail;

    *out = connection;
    return 0;

fail:
    cm_tls_release(connection);
    return -1;
}

int cm_tls_write_polarssl(cm_tls_connection *connection,
                          const void *data, size_t length) {
    const unsigned char *bytes = (const unsigned char *)data;
    size_t sent = 0;
    uint64_t deadline;

    if (!connection || (!data && length) || length > 0x7fffffffU)
        return -1;
    deadline = cm_now_ms() + connection->io_timeout_ms;

    while (sent < length) {
        int written = mbedtls_ssl_write(&connection->ssl, bytes + sent,
                                        length - sent);
        if (written == MBEDTLS_ERR_SSL_WANT_READ ||
            written == MBEDTLS_ERR_SSL_WANT_WRITE) {
            unsigned remaining = cm_remaining_ms(deadline);
            if (!remaining ||
                cm_wait_socket(connection->socket_fd,
                               written == MBEDTLS_ERR_SSL_WANT_WRITE,
                               remaining) <= 0)
                return -1;
            continue;
        }
        if (written <= 0) return -1;
        sent += (size_t)written;
    }
    return (int)sent;
}

int cm_tls_read_polarssl(cm_tls_connection *connection,
                         void *data, size_t capacity) {
    uint64_t deadline;
    if (!connection || !data || capacity == 0 ||
        capacity > 0x7fffffffU)
        return -1;

    deadline = cm_now_ms() + connection->io_timeout_ms;
    for (;;) {
        int received = mbedtls_ssl_read(&connection->ssl,
                                        (unsigned char *)data, capacity);
        if (received != MBEDTLS_ERR_SSL_WANT_READ &&
            received != MBEDTLS_ERR_SSL_WANT_WRITE) {
            if (received == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY ||
                received == MBEDTLS_ERR_SSL_CONN_EOF)
                return 0;
            return received;
        }

        {
            unsigned remaining = cm_remaining_ms(deadline);
            if (!remaining ||
                cm_wait_socket(connection->socket_fd,
                               received == MBEDTLS_ERR_SSL_WANT_WRITE,
                               remaining) <= 0)
                return -1;
        }
    }
}

int cm_tls_wait_readable(cm_tls_connection *connection,
                         unsigned timeout_ms) {
    if (!connection || connection->socket_fd < 0) return -1;
    if (mbedtls_ssl_get_bytes_avail(&connection->ssl) > 0) return 1;
    return cm_wait_socket(connection->socket_fd, 0, timeout_ms);
}

void cm_tls_close_polarssl(cm_tls_connection *connection) {
    if (!connection) return;
    (void)mbedtls_ssl_close_notify(&connection->ssl);
    cm_tls_release(connection);
}
