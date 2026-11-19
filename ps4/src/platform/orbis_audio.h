#ifndef CLOUDMOON_ORBIS_AUDIO_H
#define CLOUDMOON_ORBIS_AUDIO_H

#include <stddef.h>
#include <stdint.h>

int cm_orbis_audio_start(void);
int cm_orbis_audio_submit_opus(const uint8_t *packet, size_t length);
void cm_orbis_audio_stop(void);

#endif
