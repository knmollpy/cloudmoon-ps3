#ifndef CLOUDMOON_WS_TLS_H
#define CLOUDMOON_WS_TLS_H
#include <stddef.h>
#include <stdint.h>
#include "cloudmoon_random.h"
#include "cloudmoon_ws.h"
#include "cloudmoon_tls_polarssl.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct cm_ws_tls_transport cm_ws_tls_transport;
int cm_ws_tls_connect(cm_ws_tls_transport **out,const char *hostname,uint16_t port,const char *path,const char *ca_bundle_path,unsigned timeout_seconds,cm_random_source_fn random_source,void *random_context);
int cm_ws_tls_send(cm_ws_tls_transport *transport,uint8_t opcode,int fin,const void *payload,size_t payload_length,cm_random_source_fn random_source,void *random_context);
int cm_ws_tls_read_frame(cm_ws_tls_transport *transport,cm_ws_frame_info_t *info,void *payload,size_t payload_capacity);
void cm_ws_tls_close(cm_ws_tls_transport *transport);
#ifdef __cplusplus
}
#endif
#endif
