#ifndef CLOUDMOON_CLIENT_H
#define CLOUDMOON_CLIENT_H
#include <stddef.h>
#include <stdint.h>
#include "cloudmoon_session.h"
#include "cloudmoon_ws_tls.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef int (*cm_cloudmoon_send_fn)(void *context,const char *packet,size_t length);
typedef void (*cm_cloudmoon_signal_fn)(void *context,const char *json,size_t length);
typedef struct { int width,height; const char *game,*user_id,*instance_id; } cm_cloudmoon_registration_t;
typedef struct { cm_session_t session; cm_cloudmoon_registration_t registration; cm_cloudmoon_send_fn send; cm_cloudmoon_signal_fn signal; void *send_context; void *signal_context; int stream_ready; } cm_cloudmoon_client_t;
typedef enum { CM_CLIENT_PACKET_ERROR=-1,CM_CLIENT_PACKET_IGNORED=0,CM_CLIENT_PACKET_CONTINUE=1,CM_CLIENT_PACKET_STOP=2 } cm_client_packet_result_t;
void cm_cloudmoon_client_init(cm_cloudmoon_client_t *client,const cm_cloudmoon_registration_t *registration,cm_cloudmoon_send_fn send,cm_cloudmoon_signal_fn signal,void *send_context,void *signal_context);
cm_client_packet_result_t cm_cloudmoon_client_handle_packet(cm_cloudmoon_client_t *client,const char *packet,size_t packet_length);
int cm_cloudmoon_client_send_signal(cm_cloudmoon_client_t *client,const char *json,size_t json_length);
int cm_cloudmoon_client_run_from_instance_id(const char *android_instance_id,const char *ca_bundle_path,unsigned timeout_seconds,const cm_cloudmoon_registration_t *registration,cm_random_source_fn random_source,void *random_context,cm_cloudmoon_signal_fn signal,void *signal_context);
int cm_cloudmoon_client_run(const char *hostname,uint16_t port,const char *path,const char *ca_bundle_path,unsigned timeout_seconds,const cm_cloudmoon_registration_t *registration,cm_random_source_fn random_source,void *random_context,cm_cloudmoon_signal_fn signal,void *signal_context);
#ifdef __cplusplus
}
#endif
#endif
