#ifndef CLOUDMOON_ORBIS_WEBRTC_H
#define CLOUDMOON_ORBIS_WEBRTC_H

#include <stddef.h>

#include "cloudmoon_client.h"

typedef struct cm_orbis_webrtc cm_orbis_webrtc_t;

int cm_orbis_webrtc_create(cm_orbis_webrtc_t **out,
                           cm_cloudmoon_client_t *client,
                           int width, int height);
void cm_orbis_webrtc_signal(void *context,
                            cm_cloudmoon_client_t *client,
                            const char *json, size_t length);
int cm_orbis_webrtc_send_input(cm_orbis_webrtc_t *rtc,
                               const char *message, size_t length);
int cm_orbis_webrtc_is_connected(const cm_orbis_webrtc_t *rtc);
void cm_orbis_webrtc_close(cm_orbis_webrtc_t *rtc);

#endif
