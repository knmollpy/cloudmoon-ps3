#ifndef CLOUDMOON_WEBRTC_H
#define CLOUDMOON_WEBRTC_H
#include <stddef.h>
#include "cloudmoon_client.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct cm_cloudmoon_webrtc cm_cloudmoon_webrtc_t;
int cm_cloudmoon_webrtc_create(cm_cloudmoon_webrtc_t **out,cm_cloudmoon_client_t *client,int width,int height);
void cm_cloudmoon_webrtc_signal(void *context,const char *json,size_t length);
int cm_cloudmoon_webrtc_send_input(cm_cloudmoon_webrtc_t *rtc,const char *message,size_t length);
void cm_cloudmoon_webrtc_detach_signaling(cm_cloudmoon_webrtc_t *rtc);
int cm_cloudmoon_webrtc_is_connected(const cm_cloudmoon_webrtc_t *rtc);
void cm_cloudmoon_webrtc_close(cm_cloudmoon_webrtc_t *rtc);
#ifdef __cplusplus
}
#endif
#endif
