#include "orbis_audio.h"

#include <opus/opus.h>
#include <orbis/AudioOut.h>
#include <orbis/Sysmodule.h>
#include <orbis/UserService.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CM_AUDIO_RATE 48000
#define CM_AUDIO_CHANNELS 2
#define CM_AUDIO_GRAIN 256
#define CM_AUDIO_RING_FRAMES (CM_AUDIO_GRAIN * 12)
#define CM_AUDIO_MAX_FRAME 5760

static OpusDecoder *s_decoder;
static int32_t s_audio_handle = -1;
static int s_audio_initialized;
static int s_ring_head;
static int s_ring_tail;
static int s_ring_count;
static int16_t s_ring[CM_AUDIO_RING_FRAMES * CM_AUDIO_CHANNELS];
static int16_t s_decode[CM_AUDIO_MAX_FRAME * CM_AUDIO_CHANNELS];
static int16_t s_out[CM_AUDIO_GRAIN * CM_AUDIO_CHANNELS];

int cm_orbis_audio_start(void) {
    int error = OPUS_OK;
    int32_t user_id = ORBIS_USER_SERVICE_USER_ID_SYSTEM;
    int rc;

    if (s_decoder && s_audio_handle >= 0)
        return 0;

    s_decoder = opus_decoder_create(CM_AUDIO_RATE, CM_AUDIO_CHANNELS, &error);
    if (!s_decoder || error != OPUS_OK) {
        s_decoder = NULL;
        return -1;
    }

    (void)sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_AUDIOOUT);
    if (!s_audio_initialized) {
        rc = sceAudioOutInit();
        if (rc < 0) {
            opus_decoder_destroy(s_decoder);
            s_decoder = NULL;
            return -1;
        }
        s_audio_initialized = 1;
    }

    {
        int32_t initial_user = -1;
        if (sceUserServiceGetInitialUser(&initial_user) == 0 && initial_user > 0)
            user_id = initial_user;
    }

    s_audio_handle = sceAudioOutOpen(user_id,
                                     ORBIS_AUDIO_OUT_PORT_TYPE_MAIN,
                                     0,
                                     CM_AUDIO_GRAIN,
                                     CM_AUDIO_RATE,
                                     ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_STEREO);
    if (s_audio_handle <= 0) {
        s_audio_handle = sceAudioOutOpen(ORBIS_USER_SERVICE_USER_ID_SYSTEM,
                                         ORBIS_AUDIO_OUT_PORT_TYPE_MAIN,
                                         0,
                                         CM_AUDIO_GRAIN,
                                         CM_AUDIO_RATE,
                                         ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_STEREO);
    }

    if (s_audio_handle <= 0) {
        opus_decoder_destroy(s_decoder);
        s_decoder = NULL;
        s_audio_handle = -1;
        return -1;
    }

    s_ring_head = s_ring_tail = s_ring_count = 0;
    memset(s_ring, 0, sizeof(s_ring));
    return 0;
}

static void cm_audio_ring_push(const int16_t *pcm, int frames) {
    int i;
    if (!pcm || frames <= 0)
        return;
    if (frames > CM_AUDIO_RING_FRAMES) {
        pcm += (frames - CM_AUDIO_RING_FRAMES) * CM_AUDIO_CHANNELS;
        frames = CM_AUDIO_RING_FRAMES;
    }
    if (s_ring_count + frames > CM_AUDIO_RING_FRAMES) {
        int drop = s_ring_count + frames - CM_AUDIO_RING_FRAMES;
        s_ring_head = (s_ring_head + drop) % CM_AUDIO_RING_FRAMES;
        s_ring_count -= drop;
    }

    for (i = 0; i < frames; ++i) {
        const int src = i * CM_AUDIO_CHANNELS;
        const int dst = s_ring_tail * CM_AUDIO_CHANNELS;
        s_ring[dst] = pcm[src];
        s_ring[dst + 1] = pcm[src + 1];
        s_ring_tail = (s_ring_tail + 1) % CM_AUDIO_RING_FRAMES;
    }
    s_ring_count += frames;
}

static void cm_audio_ring_pop(int16_t *output, int frames) {
    int i;
    for (i = 0; i < frames; ++i) {
        const int src = s_ring_head * CM_AUDIO_CHANNELS;
        const int dst = i * CM_AUDIO_CHANNELS;
        output[dst] = s_ring[src];
        output[dst + 1] = s_ring[src + 1];
        s_ring_head = (s_ring_head + 1) % CM_AUDIO_RING_FRAMES;
    }
    s_ring_count -= frames;
}

int cm_orbis_audio_submit_opus(const uint8_t *packet, size_t length) {
    int decoded;
    if (!s_decoder || s_audio_handle < 0 || !packet || length == 0 ||
        length > 65535u)
        return -1;

    decoded = opus_decode(s_decoder, packet, (opus_int32)length,
                          s_decode, CM_AUDIO_MAX_FRAME, 0);
    if (decoded <= 0)
        return -1;

    cm_audio_ring_push(s_decode, decoded);
    while (s_ring_count >= CM_AUDIO_GRAIN) {
        cm_audio_ring_pop(s_out, CM_AUDIO_GRAIN);
        if (sceAudioOutOutput(s_audio_handle, s_out) < 0) {
            /* Keep the newest audio; never let a stalled device build latency. */
            s_ring_head = s_ring_tail;
            s_ring_count = 0;
            return -1;
        }
    }
    return decoded;
}

void cm_orbis_audio_stop(void) {
    if (s_audio_handle >= 0) {
        (void)sceAudioOutOutput(s_audio_handle, NULL);
        (void)sceAudioOutClose(s_audio_handle);
        s_audio_handle = -1;
    }
    if (s_decoder) {
        opus_decoder_destroy(s_decoder);
        s_decoder = NULL;
    }
    s_ring_head = s_ring_tail = s_ring_count = 0;
    memset(s_ring, 0, sizeof(s_ring));
}
