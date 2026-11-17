#ifndef CLOUDMOON_SESSION_H
#define CLOUDMOON_SESSION_H
#include <stddef.h>
#include "cloudmoon_protocol.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { CM_SIO_EVENT_NONE=0,CM_SIO_EVENT_HELLO_SERVER,CM_SIO_EVENT_ACTION_DONE2,CM_SIO_EVENT_MSG,CM_SIO_EVENT_GETOUT,CM_SIO_EVENT_OTHER,CM_SIO_EVENT_INVALID } cm_sio_event_type_t;
typedef struct { cm_sio_event_type_t type; const char *name; size_t name_len; const char *payload; size_t payload_len; } cm_sio_event_t;
typedef enum { CM_SESSION_DISCONNECTED=0,CM_SESSION_SOCKET_CONNECTED,CM_SESSION_CLIENT_REGISTER_SENT,CM_SESSION_ROOM_REGISTER_SENT,CM_SESSION_SIGNALING,CM_SESSION_CLOSED,CM_SESSION_ERROR } cm_session_state_t;
typedef enum { CM_SESSION_ACTION_NONE=0,CM_SESSION_ACTION_SEND_CLIENT_REGISTER,CM_SESSION_ACTION_SEND_REGISTER,CM_SESSION_ACTION_HANDLE_SIGNAL,CM_SESSION_ACTION_STOP } cm_session_action_t;
typedef struct { cm_session_state_t state; char room_id[256]; char client_id[128]; } cm_session_t;
int cm_socketio_parse_event(const char *packet,cm_sio_event_t *event);
int cm_json_get_string(const char *json,size_t json_len,const char *key,char *out,size_t out_cap);
void cm_session_init(cm_session_t *session);
cm_session_action_t cm_session_on_event(cm_session_t *session,const cm_sio_event_t *event);
#ifdef __cplusplus
}
#endif
#endif
