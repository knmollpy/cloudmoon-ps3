#include "cloudmoon_client.h"
#include "cloudmoon_protocol.h"
#include "cloudmoon_discovery.h"
#include "cloudmoon_ws.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/time.h>
#define CM_CLIENT_PACKET_MAX 32768

#ifndef CM_CLIENT_PLATFORM_LABEL
#define CM_CLIENT_PLATFORM_LABEL CM_CLIENT_PLATFORM_LABEL
#endif

static volatile int cm_client_stop_requested = 0;
void cm_cloudmoon_client_request_stop(void) {
    __sync_lock_test_and_set(&cm_client_stop_requested, 1);
}
void cm_cloudmoon_client_reset_stop(void) {
    __sync_lock_test_and_set(&cm_client_stop_requested, 0);
}
static int cm_client_send_event(cm_cloudmoon_client_t *client,const char *packet,size_t length){return client->send?client->send(client->send_context,packet,length):-1;}
static uint64_t cm_client_now_ms(void) {
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) return 0;
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
}
static int cm_client_send_registration(cm_cloudmoon_client_t *client) {
    char out[4096];
    int n = cm_socketio_make_client_register(out, sizeof(out),
        client->registration.width, client->registration.height,
        client->registration.game, client->registration.user_id,
        client->registration.instance_id);
    if (n < 0 || cm_client_send_event(client, out, (size_t)n) < 0)
        return -1;
    /* Match the browser client's metrics event after each registration attempt. */
    n = cm_socketio_make_event(out, sizeof(out), "metrics", "\"CloudMoon PS3 native client (PS3; PowerPC)\"");
    if (n < 0 || cm_client_send_event(client, out, (size_t)n) < 0)
        return -1;
    return 0;
}
void cm_cloudmoon_client_init(cm_cloudmoon_client_t *client,const cm_cloudmoon_registration_t *registration,cm_cloudmoon_send_fn send,cm_cloudmoon_signal_fn signal,void *send_context,void *signal_context){if(!client)return;memset(client,0,sizeof(*client));cm_session_init(&client->session);if(registration)client->registration=*registration;client->send=send;client->signal=signal;client->send_context=send_context;client->signal_context=signal_context;}
cm_client_packet_result_t cm_cloudmoon_client_handle_packet(cm_cloudmoon_client_t *client,const char *packet,size_t length){char out[4096],copy[CM_CLIENT_PACKET_MAX+1];int n;cm_sio_event_t event;cm_session_action_t action;if(!client||!packet||!length||length>CM_CLIENT_PACKET_MAX)return CM_CLIENT_PACKET_ERROR;memcpy(copy,packet,length);copy[length]=0;
if(copy[0]=='0'){if(length>1&&copy[1]=='{')return cm_client_send_event(client,"40",2)<0?CM_CLIENT_PACKET_ERROR:CM_CLIENT_PACKET_CONTINUE;return CM_CLIENT_PACKET_ERROR;}
if(copy[0]=='2'){if(length>=sizeof(out))return CM_CLIENT_PACKET_ERROR;out[0]='3';if(length>1)memcpy(out+1,copy+1,length-1);out[length]=0;return cm_client_send_event(client,out,length)<0?CM_CLIENT_PACKET_ERROR:CM_CLIENT_PACKET_CONTINUE;}
if(length>=2&&copy[0]=='4'&&copy[1]=='0'){client->session.state=CM_SESSION_SOCKET_CONNECTED;return CM_CLIENT_PACKET_CONTINUE;}
if(length>=2&&copy[0]=='4'&&copy[1]=='1'){client->session.state=CM_SESSION_CLOSED;return CM_CLIENT_PACKET_STOP;}
if(length>=2&&copy[0]=='4'&&copy[1]=='2'){if(cm_socketio_parse_event(copy,&event)!=0)return CM_CLIENT_PACKET_ERROR;
if(event.type==CM_SIO_EVENT_GETOUT){char decoded[512];const char *json=event.payload;size_t json_length=event.payload_len;int error_code;if(json_length&&json[0]=='"'){int decoded_length=cm_json_decode_string(json,json_length,decoded,sizeof(decoded));if(decoded_length<0)return CM_CLIENT_PACKET_ERROR;json=decoded;json_length=(size_t)decoded_length;}if(cm_json_get_integer(json,json_length,"errorCode",&error_code)!=0)return CM_CLIENT_PACKET_ERROR;if(error_code==10){client->stream_ready=1;client->session.state=CM_SESSION_CLOSED;return CM_CLIENT_PACKET_STOP;}client->session.state=CM_SESSION_ERROR;return CM_CLIENT_PACKET_ERROR;}
action=cm_session_on_event(&client->session,&event);switch(action){case CM_SESSION_ACTION_SEND_CLIENT_REGISTER:return cm_client_send_registration(client)<0?CM_CLIENT_PACKET_ERROR:CM_CLIENT_PACKET_CONTINUE;case CM_SESSION_ACTION_SEND_REGISTER:n=cm_socketio_make_register(out,sizeof(out),client->session.room_id,client->session.client_id);if(n<0)return CM_CLIENT_PACKET_ERROR;return cm_client_send_event(client,out,(size_t)n)<0?CM_CLIENT_PACKET_ERROR:CM_CLIENT_PACKET_CONTINUE;case CM_SESSION_ACTION_HANDLE_SIGNAL:if(client->signal)client->signal(client->signal_context,client,event.payload,event.payload_len);return CM_CLIENT_PACKET_CONTINUE;case CM_SESSION_ACTION_STOP:return CM_CLIENT_PACKET_STOP;default:return client->session.state==CM_SESSION_ERROR?CM_CLIENT_PACKET_ERROR:CM_CLIENT_PACKET_IGNORED;}}
if(copy[0]=='1'){client->session.state=CM_SESSION_CLOSED;return CM_CLIENT_PACKET_STOP;}return CM_CLIENT_PACKET_IGNORED;}
int cm_cloudmoon_client_send_signal(cm_cloudmoon_client_t *client,const char *json,size_t length){char packet[CM_CLIENT_PACKET_MAX],body[CM_CLIENT_PACKET_MAX];int n;if(!client||!json||!length||length>=sizeof(body))return -1;memcpy(body,json,length);body[length]=0;n=cm_socketio_make_event(packet,sizeof(packet),"msg",body);if(n<0)return -1;return cm_client_send_event(client,packet,(size_t)n);}
#define CM_CLIENT_TX_QUEUE_COUNT 8
typedef struct {
    volatile int lock;
    unsigned head,count;
    size_t lengths[CM_CLIENT_TX_QUEUE_COUNT];
    char *storage;
} cm_client_tx_queue;
typedef struct{cm_ws_tls_transport *transport;cm_random_source_fn random_source;void *random_context;cm_client_tx_queue *queue;} cm_client_transport_context;
static void cm_queue_lock(cm_client_tx_queue*q){while(__sync_lock_test_and_set(&q->lock,1)){}}
static void cm_queue_unlock(cm_client_tx_queue*q){__sync_lock_release(&q->lock);}
static int cm_client_transport_send(void *context,const char *packet,size_t length){
    cm_client_transport_context *tx=(cm_client_transport_context*)context;
    cm_client_tx_queue*q;unsigned tail;
    if(!tx||!(q=tx->queue)||!packet||!length||length>=CM_CLIENT_PACKET_MAX)return -1;
    cm_queue_lock(q);
    if(q->count>=CM_CLIENT_TX_QUEUE_COUNT){cm_queue_unlock(q);return -1;}
    tail=(q->head+q->count)%CM_CLIENT_TX_QUEUE_COUNT;
    memcpy(q->storage+(size_t)tail*CM_CLIENT_PACKET_MAX,packet,length);
    q->lengths[tail]=length;q->count++;
    cm_queue_unlock(q);
    return (int)length;
}
static int cm_client_transport_flush(cm_client_transport_context*tx){
    char packet[CM_CLIENT_PACKET_MAX];cm_client_tx_queue*q;size_t length;unsigned head;
    if(!tx||!(q=tx->queue))return -1;
    for(;;){
        cm_queue_lock(q);
        if(!q->count){cm_queue_unlock(q);return 0;}
        head=q->head;length=q->lengths[head];
        memcpy(packet,q->storage+(size_t)head*CM_CLIENT_PACKET_MAX,length);
        q->head=(q->head+1)%CM_CLIENT_TX_QUEUE_COUNT;q->count--;
        cm_queue_unlock(q);
        if(cm_ws_tls_send(tx->transport,CM_WS_TEXT,1,packet,length,tx->random_source,tx->random_context)<0)return -1;
    }
}
int cm_cloudmoon_client_run(const char *hostname,uint16_t port,const char *path,const char *ca_bundle_path,unsigned timeout_seconds,const cm_cloudmoon_registration_t *registration,cm_random_source_fn random_source,void *random_context,cm_cloudmoon_signal_fn signal,void *signal_context) {
    cm_ws_tls_transport *transport=NULL;
    cm_cloudmoon_client_t client;
    cm_client_transport_context tx;
    cm_client_tx_queue *queue=NULL;
    cm_ws_frame_info_t info;
    uint8_t frame_payload[CM_CLIENT_PACKET_MAX];
    uint8_t message[CM_CLIENT_PACKET_MAX];
    size_t message_length=0;
    int fragmented_opcode=0,result=-1;
    unsigned register_attempts=0;
    uint64_t register_retry_at=0;
    if(!registration||!random_source)return -1;
    if(cm_ws_tls_connect(&transport,hostname,port,path,ca_bundle_path,timeout_seconds,random_source,random_context)!=0)return -1;
    if(__sync_fetch_and_add(&cm_client_stop_requested,0)){cm_ws_tls_close(transport);return 0;}
    queue=(cm_client_tx_queue*)calloc(1,sizeof(*queue)+(size_t)CM_CLIENT_TX_QUEUE_COUNT*CM_CLIENT_PACKET_MAX);
    if(!queue){cm_ws_tls_close(transport);return -1;}
    queue->storage=(char*)(queue+1);
    tx.transport=transport;tx.random_source=random_source;tx.random_context=random_context;tx.queue=queue;
    cm_cloudmoon_client_init(&client,registration,cm_client_transport_send,signal,&tx,signal_context);
    for(;;) {
        int ready,n;
        if(__sync_fetch_and_add(&cm_client_stop_requested,0)){result=0;break;}
        if(client.session.state==CM_SESSION_CLIENT_REGISTER_SENT) {
            uint64_t now=cm_client_now_ms();
            if(register_attempts==0) {
                register_attempts=1;
                register_retry_at=now+3000ULL;
            } else if(register_attempts<3 && now>=register_retry_at) {
                if(cm_client_send_registration(&client)<0)break;
                register_attempts++;
                register_retry_at=now+3000ULL;
            }
        } else if(client.session.state==CM_SESSION_ROOM_REGISTER_SENT ||
                  client.session.state==CM_SESSION_SIGNALING) {
            register_attempts=3;
        }
        if(cm_client_transport_flush(&tx)<0)break;
        ready=cm_ws_tls_wait_readable(transport,100);
        if(ready<0)break;
        if(ready==0)continue;
        n=cm_ws_tls_read_frame(transport,&info,frame_payload,sizeof(frame_payload));
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
    /* Detach and drain in-flight RTC senders before releasing the queue/TLS context. */
    if(signal)signal(signal_context,&client,NULL,0);
    (void)cm_client_transport_flush(&tx);
    free(queue);
    cm_ws_tls_close(transport);
    return result;
}
int cm_cloudmoon_client_run_from_instance_id(const char *android_instance_id,const char *ca_bundle_path,unsigned timeout_seconds,const cm_cloudmoon_registration_t *registration,cm_random_source_fn random_source,void *random_context,cm_cloudmoon_signal_fn signal,void *signal_context){char hostname[256],path[1024];uint16_t port;if(__sync_fetch_and_add(&cm_client_stop_requested,0))return 0;if(cm_cloudmoon_get_coordinator(android_instance_id,hostname,sizeof(hostname),&port,path,sizeof(path))!=0)return -1;return cm_cloudmoon_client_run(hostname,port,path,ca_bundle_path,timeout_seconds,registration,random_source,random_context,signal,signal_context);}
