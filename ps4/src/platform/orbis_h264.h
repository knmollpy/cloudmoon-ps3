#ifndef CLOUDMOON_ORBIS_H264_H
#define CLOUDMOON_ORBIS_H264_H

#include <stddef.h>
#include <stdint.h>

int cm_orbis_h264_start(int width, int height);
int cm_orbis_h264_submit(const uint8_t *annex_b, size_t length, uint64_t pts_us);
void cm_orbis_h264_stop(void);

#endif
