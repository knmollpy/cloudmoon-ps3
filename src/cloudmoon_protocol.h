#ifndef CLOUDMOON_PROTOCOL_H
#define CLOUDMOON_PROTOCOL_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { CM_EIO_NOOP=0, CM_EIO_SEND_UPGRADE=1, CM_EIO_PING=2, CM_EIO_PONG=3, CM_EIO_MESSAGE=4, CM_EIO_CLOSE=5, CM_EIO_INVALID=-1 } cm_eio_type_t;
cm_eio_type_t cm_eio_packet_type(const char *packet);
int cm_eio_make_pong(char *out,size_t cap);
int cm_socketio_make_connect(char *out,size_t cap);
int cm_socketio_make_event(char *out,size_t cap,const char *event,const char *json_object);
int cm_socketio_make_register(char *out,size_t cap,const char *room_id,const char *client_id);
int cm_socketio_make_client_register(char *out,size_t cap,int width,int height,const char *game,const char *user_id,const char *instance_id);
#ifdef __cplusplus
}
#endif
#endif
