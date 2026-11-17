#include "cloudmoon_ws_tls.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
struct cm_tls_connection { int unused; };
static struct cm_tls_connection fake_tls;
static unsigned char server_bytes[2048],client_bytes[4096];
static size_t server_length,server_offset,client_length;
static int closed;
int cm_tls_connect_polarssl(cm_tls_connection**out,const char*host,uint16_t port,const char*ca,unsigned timeout){assert(host&&!strcmp(host,"cloudmoon.example"));assert(port==443);assert(ca&&!strcmp(ca,"ca.pem"));assert(timeout==10);*out=&fake_tls;return 0;}
int cm_tls_write_polarssl(cm_tls_connection*c,const void*data,size_t n){assert(c==&fake_tls);if(client_length+n>sizeof(client_bytes))return -1;memcpy(client_bytes+client_length,data,n);client_length+=n;if(client_length>4&&!server_length){const char*r="HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: Bz3qJYTGdOe8gUSpLosEdiLKDrk=\r\n\r\n";const unsigned char f[]={0x81,5,'h','e','l','l','o'};server_length=strlen(r)+sizeof(f);memcpy(server_bytes,r,strlen(r));memcpy(server_bytes+strlen(r),f,sizeof(f));}return (int)n;}
int cm_tls_wait_readable(cm_tls_connection*c,unsigned ms){(void)c;(void)ms;return 1;}
int cm_tls_read_polarssl(cm_tls_connection*c,void*out,size_t cap){size_t n;assert(c==&fake_tls);if(server_offset>=server_length)return 0;n=server_length-server_offset;if(n>cap)n=cap;memcpy(out,server_bytes+server_offset,n);server_offset+=n;return (int)n;}
void cm_tls_close_polarssl(cm_tls_connection*c){assert(c==&fake_tls);closed=1;}
static int contains(const unsigned char*h,size_t hn,const char*n){size_t i,z=strlen(n);for(i=0;i+z<=hn;i++)if(!memcmp(h+i,n,z))return 1;return 0;}
static int test_rng(void*ctx,uint8_t*out,size_t n){uint8_t*p=(uint8_t*)ctx;size_t i;for(i=0;i<n;i++)out[i]=(*p)++;return 0;}
int main(void){cm_ws_tls_transport*t=NULL;cm_ws_frame_info_t info;char payload[16]={0};uint8_t seed=0;int n;assert(cm_ws_tls_connect(&t,"cloudmoon.example",443,"/client/socket.io/?EIO=3&transport=websocket","ca.pem",10,test_rng,&seed)==0);assert(t);assert(contains(client_bytes,client_length,"GET /client/socket.io/?EIO=3&transport=websocket HTTP/1.1"));assert(contains(client_bytes,client_length,"Sec-WebSocket-Key: AAECAwQFBgcICQoLDA0ODw=="));n=cm_ws_tls_read_frame(t,&info,payload,sizeof(payload));assert(n==7&&info.opcode==CM_WS_TEXT&&info.fin&&info.payload_len==5&&!memcmp(payload,"hello",5));n=cm_ws_tls_send(t,CM_WS_TEXT,1,"ping",4,test_rng,&seed);assert(n==10&&client_length>=(size_t)n);assert((client_bytes[client_length-(size_t)n]&15)==CM_WS_TEXT);assert(client_bytes[client_length-(size_t)n+1]&128);cm_ws_tls_close(t);assert(closed);puts("PASS: TLS/WebSocket Upgrade, post-header frame preservation, masked client send");return 0;}
