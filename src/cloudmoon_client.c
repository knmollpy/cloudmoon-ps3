#include "cloudmoon_client.h"
#include "cloudmoon_protocol.h"
#include "cloudmoon_discovery.h"
#include "cloudmoon_ws.h"
#include <string.h>
#define CM_CLIENT_PACKET_MAX 32768
static int cm_client_send_event(cm_cloudmoon_client_t *client,const char *packet,size_t length){return client->send?client->send(client->send_context,packet,length):-1;}
void cm_cloudmoon_client_init(cm_cloudmoon_client_t *client,const cm_cloudmoon_registration_t *registration,cm_cloudmoon_send_fn send,cm_cloudmoon_signal_fn signal,void *send_context,void *signal_context){if(!client)return;memset(client,0,sizeof(*client));cm_session_init(&client->session);if(registration)client->registration=*registration;client->send=send;client->signal=signal;client->send_context=send_context;client->signal_context=signal_context;}
cm_client_packet_result_t cm_cloudmoon_client_handle_packet(cm_cloudmoon_client_t *client,const char *packet,size_t length){char out[4096],copy[CM_CLIENT_PACKET_MAX+1];int n;cm_sio_event_t event;cm_session_action_t action;if(!client||!packet||!length||length>CM_CLIENT_PACKET_MAX)return CM_CLIENT_PACKET_ERROR;memcpy(copy,packet,length);copy[length]=0;
if(copy[0]=='0'){if(length>1&&copy[1]=='{')return cm_client_send_event(client,"40",2)<0?CM_CLIENT_PACKET_ERROR:CM_CLIENT_PACKET_CONTINUE;return CM_CLIENT_PACKET_ERROR;}
if(copy[0]=='2'){if(length>=sizeof(out))return CM_CLIENT_PACKET_ERROR;out[0]='3';if(length>1)memcpy(out+1,copy+1,length-1);out[length]=0;return cm_client_send_event(client,out,length)<0?CM_CLIENT_PACKET_ERROR:CM_CLIENT_PACKET_CONTINUE;}
if(length>=2&&copy[0]=='4'&&copy[1]=='0'){client->session.state=CM_SESSION_SOCKET_CONNECTED;return CM_CLIENT_PACKET_CONTINUE;}
if(length>=2&&copy[0]=='4'&&copy[1]=='1'){client->session.state=CM_SESSION_CLOSED;return CM_CLIENT_PACKET_STOP;}
if(length>=2&&copy[0]=='4'&&copy[1]=='2'){if(cm_socketio_parse_event(copy,&event)!=0)return CM_CLIENT_PACKET_ERROR;
if(event.type==CM_SIO_EVENT_GETOUT){char decoded[512];const char *json=event.payload;size_t json_length=event.payload_len;int error_code;if(json_length&&json[0]=='"'){int decoded_length=cm_json_decode_string(json,json_length,decoded,sizeof(decoded));if(decoded_length<0)return CM_CLIENT_PACKET_ERROR;json=decoded;json_length=(size_t)decoded_length;}if(cm_json_get_integer(json,json_length,"errorCode",&error_code)!=0)return CM_CLIENT_PACKET_ERROR;if(error_code==10){client->stream_ready=1;client->session.state=CM_SESSION_CLOSED;return CM_CLIENT_PACKET_STOP;}client->session.state=CM_SESSION_ERROR;return CM_CLIENT_PACKET_ERROR;}
action=cm_session_on_event(&client->session,&event);switch(action){case CM_SESSION_ACTION_SEND_CLIENT_REGISTER:n=cm_socketio_make_client_register(out,sizeof(out),client->registration.width,client->registration.height,client->registration.game,client->registration.user_id,client->registration.instance_id);if(n<0)return CM_CLIENT_PACKET_ERROR;return cm_client_send_event(client,out,(size_t)n)<0?CM_CLIENT_PACKET_ERROR:CM_CLIENT_PACKET_CONTINUE;case CM_SESSION_ACTION_SEND_REGISTER:n=cm_socketio_make_register(out,sizeof(out),client->session.room_id,client->session.client_id);if(n<0)return CM_CLIENT_PACKET_ERROR;return cm_client_send_event(client,out,(size_t)n)<0?CM_CLIENT_PACKET_ERROR:CM_CLIENT_PACKET_CONTINUE;case CM_SESSION_ACTION_HANDLE_SIGNAL:if(client->signal)client->signal(client->signal_context,client,event.payload,event.payload_len);return CM_CLIENT_PACKET_CONTINUE;case CM_SESSION_ACTION_STOP:return CM_CLIENT_PACKET_STOP;default:return client->session.state==CM_SESSION_ERROR?CM_CLIENT_PACKET_ERROR:CM_CLIENT_PACKET_IGNORED;}}
if(copy[0]=='1'){client->session.state=CM_SESSION_CLOSED;return CM_CLIENT_PACKET_STOP;}return CM_CLIENT_PACKET_IGNORED;}
int cm_cloudmoon_client_send_signal(cm_cloudmoon_client_t *client,const char *json,size_t length){char packet[4096],body[3072];int n;if(!client||!json||!length||length>=sizeof(body))return -1;memcpy(body,json,length);body[length]=0;n=cm_socketio_make_event(packet,sizeof(packet),"msg",body);if(n<0)return -1;return cm_client_send_event(client,packet,(size_t)n);}
typedef struct{cm_ws_tls_transport *transport;cm_random_source_fn random_source;void *random_context;} cm_client_transport_context;
static int cm_client_transport_send(void *context,const char *packet,size_t length){cm_client_transport_context *tx=(cm_client_transport_context*)context;return cm_ws_tls_send(tx->transport,CM_WS_TEXT,1,packet,length,tx->random_source,tx->random_context);}
int cm_cloudmoon_client_run(const char *hostname,uint16_t port,const char *path,const char *ca_bundle_path,unsigned timeout_seconds,const cm_cloudmoon_registration_t *registration,cm_random_source_fn random_source,void *random_context,cm_cloudmoon_signal_fn signal,void *signal_context) {
    cm_ws_tls_transport *transport=NULL;
    cm_cloudmoon_client_t client;
    cm_client_transport_context tx;
    cm_ws_frame_info_t info;
    uint8_t frame_payload[CM_CLIENT_PACKET_MAX];
    uint8_t message[CM_CLIENT_PACKET_MAX];
    size_t message_length=0;
    int fragmented_opcode=0,result=-1;
    if(!registration||!random_source)return -1;
    if(cm_ws_tls_connect(&transport,hostname,port,path,ca_bundle_path,timeout_seconds,random_source,random_context)!=0)return -1;
    tx.transport=transport;tx.random_source=random_source;tx.random_context=random_context;
    cm_cloudmoon_client_init(&client,registration,cm_client_transport_send,signal,&tx,signal_context);
    for(;;) {
        int n=cm_ws_tls_read_frame(transport,&info,frame_payload,sizeof(frame_payload));
        if(n<0)break;
        if(info.opcode==CM_WS_CLOSE){result=0;break;}
        if(info.opcode==CM_WS_PING) {
            if(cm_ws_tls_send(transport,CM_WS_PONG,1,frame_payload,info.payload_len,random_source,random_context)<0)break;
            continue;
        }
        if(info.opcode==CM_WS_PONG)continue;
        if(info.opcode==CM_WS_TEXT) {
            if(fragmented_opcode!=0)break;
            if(info.fin) {
                cm_client_packet_result_t pr=cm_cloudmoon_client_handle_packet(&client,(const char*)frame_payload,info.payload_len);
                if(pr==CM_CLIENT_PACKET_ERROR)break;
                if(pr==CM_CLIENT_PACKET_STOP){result=0;break;}
            } else {
                if(info.payload_len>sizeof(message))break;
                memcpy(message,frame_payload,info.payload_len);
                message_length=info.payload_len;
                fragmented_opcode=CM_WS_TEXT;
            }
            continue;
        }
        if(info.opcode==0) {
            if(fragmented_opcode==0||info.payload_len>sizeof(message)-message_length)break;
            if(info.payload_len)memcpy(message+message_length,frame_payload,info.payload_len);
            message_length+=info.payload_len;
            if(info.fin) {
                cm_client_packet_result_t pr;
                if(fragmented_opcode==CM_WS_TEXT) {
                    pr=cm_cloudmoon_client_handle_packet(&client,(const char*)message,message_length);
                    if(pr==CM_CLIENT_PACKET_ERROR)break;
                    if(pr==CM_CLIENT_PACKET_STOP){result=0;break;}
                }
                fragmented_opcode=0;message_length=0;
            }
            continue;
        }
        if(info.opcode==CM_WS_BINARY) {
            if(fragmented_opcode!=0)break;
            if(!info.fin)fragmented_opcode=CM_WS_BINARY;
            continue;
        }
        if(info.opcode>=8)continue;
        if(!info.fin)break;
    }
    /* Notify the media peer while signal_context and the stack client are still valid. */
    if(signal)signal(signal_context,&client,NULL,0);
    cm_ws_tls_close(transport);
    return result;
}
int cm_cloudmoon_client_run_from_instance_id(const char *android_instance_id,const char *ca_bundle_path,unsigned timeout_seconds,const cm_cloudmoon_registration_t *registration,cm_random_source_fn random_source,void *random_context,cm_cloudmoon_signal_fn signal,void *signal_context){char hostname[256],path[1024];uint16_t port;if(cm_cloudmoon_get_coordinator(android_instance_id,hostname,sizeof(hostname),&port,path,sizeof(path))!=0)return -1;return cm_cloudmoon_client_run(hostname,port,path,ca_bundle_path,timeout_seconds,registration,random_source,random_context,signal,signal_context);}
