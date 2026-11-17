#ifndef CLOUDMOON_TLS_POLARSSL_H
#define CLOUDMOON_TLS_POLARSSL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cm_tls_connection cm_tls_connection;

/*
 * Opens a direct TCP + TLS 1.2 connection to hostname:port.
 * ca_bundle_path must point to a PEM bundle of trusted public CA roots.
 * Certificate-chain and hostname verification are mandatory; there is no
 * insecure mode and no self-signed-certificate exception.
 */
int cm_tls_connect_polarssl(cm_tls_connection **out, const char *hostname,
                            uint16_t port, const char *ca_bundle_path,
                            unsigned timeout_seconds);

/* Returns bytes transferred, 0 on orderly EOF, or -1 on error. */
int cm_tls_write_polarssl(cm_tls_connection *connection,
                          const void *data, size_t length);
int cm_tls_read_polarssl(cm_tls_connection *connection,
                         void *data, size_t capacity);
void cm_tls_close_polarssl(cm_tls_connection *connection);

#ifdef __cplusplus
}
#endif
#endif
