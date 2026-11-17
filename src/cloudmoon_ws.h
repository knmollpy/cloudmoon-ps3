#ifndef CLOUDMOON_WS_H
#define CLOUDMOON_WS_H
#include <stddef.h>
#include <stdint.h>
#define CM_WS_CONTINUATION 0x0
#define CM_WS_TEXT 0x1
#define CM_WS_BINARY 0x2
#define CM_WS_CLOSE 0x8
#define CM_WS_PING 0x9
#define CM_WS_PONG 0xA
typedef struct { uint8_t opcode; int fin; size_t payload_len; } cm_ws_frame_info_t;
int cm_ws_encode_client_frame(uint8_t*out,size_t cap,uint8_t opcode,int fin,const uint8_t*payload,size_t len,const uint8_t mask_key[4]);
int cm_ws_decode_server_frame(const uint8_t*frame,size_t frame_len,cm_ws_frame_info_t*info,uint8_t*out,size_t out_cap);
#endif
