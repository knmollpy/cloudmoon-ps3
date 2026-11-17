#include "../src/cloudmoon_input.h"
#include "../src/cloudmoon_protocol.h"
#include "../src/cloudmoon_session.h"
#include "../src/cloudmoon_ws.h"
#include "../src/cloudmoon_upgrade.h"
#include "../src/cloudmoon_random.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int test_rng(void*ctx,uint8_t*out,size_t n){uint8_t*p=(uint8_t*)ctx;size_t i;for(i=0;i<n;i++)out[i]=(*p)++;return 0;}
static int failing_rng(void*c,uint8_t*o,size_t n){(void)c;(void)o;(void)n;return -1;}
int main(void){char out[4096];cm_layout_t l;cm_input_state_t s;cm_pad_t p;int n;cm_sio_event_t ev;cm_session_t session;uint8_t frame[512],decoded[512],mask[4]={1,2,3,4};cm_ws_frame_info_t info;
{uint8_t nonce[16]={0},seed=0;char req[1024],key[25];const char*resp="HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: keep-alive, Upgrade\r\nSec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n";assert(cm_ws_generate_client_key(test_rng,&seed,key)==0&&!strcmp(key,"AAECAwQFBgcICQoLDA0ODw=="));assert(cm_ws_generate_client_key(failing_rng,0,key)!=0);assert(cm_ws_generate_client_key(0,0,key)!=0);n=cm_ws_build_upgrade_request(req,sizeof(req),"cloudmoon.example","/client/socket.io/?EIO=3&transport=websocket",nonce,key);assert(n>0&&strstr(req,"Sec-WebSocket-Version: 13")&&!strcmp(key,"AAAAAAAAAAAAAAAAAAAAAA=="));assert(cm_ws_validate_upgrade_response(resp,strlen(resp),"dGhlIHNhbXBsZSBub25jZQ==")==0);assert(cm_ws_validate_upgrade_response(resp,strlen(resp),key)!=0);assert(cm_ws_validate_upgrade_response(resp,strlen(resp)-2,"dGhlIHNhbXBsZSBub25jZQ==")!=0);assert(cm_ws_build_upgrade_request(req,sizeof(req),"cloudmoon.example","/bad path",nonce,key)<0);assert(cm_ws_build_upgrade_request(req,sizeof(req),"bad\r\nhost","/",nonce,key)<0);}
assert(cm_eio_packet_type("2")==CM_EIO_PING);assert(cm_eio_packet_type("4msg")==CM_EIO_MESSAGE);assert(cm_eio_make_pong(out,sizeof(out))==1&&!strcmp(out,"3"));assert(cm_socketio_make_connect(out,sizeof(out))==2&&!strcmp(out,"40"));n=cm_socketio_make_register(out,sizeof(out),"room-123","1111111");assert(n>0&&strstr(out,"42[\"register\",{\"roomid\":\"room-123\"")==out);
n=cm_socketio_make_client_register(out,sizeof(out),1280,720,"com.example.game","user-1","eyJjb29yVXJsIjoiaHR0cHM6Ly9jb29yIn0=");assert(n>0&&strstr(out,"42[\"client_register\",")==out&&strstr(out,"\"screenRes\":\"1280x720\"")&&strstr(out,"\"slotId\":\"user-1@com.example.game\""));
assert(cm_socketio_parse_event("42[\"hello_server\",{\"wait\":1}]",&ev)==0&&ev.type==CM_SIO_EVENT_HELLO_SERVER);cm_session_init(&session);assert(cm_session_on_event(&session,&ev)==CM_SESSION_ACTION_SEND_CLIENT_REGISTER);
assert(cm_socketio_parse_event("42[\"action_done2\",{\"roomid\":\"r-55\",\"clientid\":\"c-9\"}]",&ev)==0&&cm_session_on_event(&session,&ev)==CM_SESSION_ACTION_SEND_REGISTER);assert(!strcmp(session.room_id,"r-55")&&!strcmp(session.client_id,"c-9"));
assert(cm_socketio_parse_event("42[\"msg\",{\"type\":\"offer\",\"sdp\":\"v=0\"}]",&ev)==0&&cm_session_on_event(&session,&ev)==CM_SESSION_ACTION_HANDLE_SIGNAL);assert(session.state==CM_SESSION_SIGNALING);
assert(cm_socketio_parse_event("42[\"getout\",\"closed\"]",&ev)==0&&cm_session_on_event(&session,&ev)==CM_SESSION_ACTION_STOP);assert(session.state==CM_SESSION_CLOSED);
n=cm_ws_encode_client_frame(frame,sizeof(frame),CM_WS_TEXT,1,(const uint8_t*)"hello",5,mask);assert(n==11&&frame[0]==0x81&&frame[1]==0x85);assert(cm_ws_decode_server_frame((const uint8_t*)"\x81\x05hello",7,&info,decoded,sizeof(decoded))==7&&info.opcode==CM_WS_TEXT&&info.fin&&info.payload_len==5&&!memcmp(decoded,"hello",5));assert(cm_ws_decode_server_frame((const uint8_t*)"\x89\x01x",3,&info,decoded,sizeof(decoded))==3&&info.opcode==CM_WS_PING&&info.payload_len==1);
cm_layout_default(&l,1280,720);cm_input_reset(&s);memset(&p,0,sizeof(p));p.lx=32767;p.cross=1;n=cm_input_translate(&l,&s,&p,out,sizeof(out));assert(n>0&&strstr(out,"touch>Touch start:10,"));assert(strstr(out+strlen(out)+1,"touch>Touch start:20,"));
memset(&p,0,sizeof(p));n=cm_input_translate(&l,&s,&p,out,sizeof(out));assert(n>0&&strstr(out,"touch>Touch end:10,")&&strstr(out+strlen(out)+1,"touch>Touch end:20,"));
puts("PASS: Engine.IO, Socket.IO session, RFC6455 frames + HTTP Upgrade validation, fail-closed RNG, registration JSON, touch input");return 0;}
