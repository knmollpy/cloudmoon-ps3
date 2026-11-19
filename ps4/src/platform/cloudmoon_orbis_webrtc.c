#include "cloudmoon_orbis_webrtc.h"

#include "orbis_audio.h"
#include "orbis_h264.h"
#include "orbis_input.h"

#include "../../../src/cloudmoon_input.h"
#include "../../../src/cloudmoon_session.h"
#include <rtc/rtc.h>

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CM_ORBIS_SDP_MAX 16384
#define CM_ORBIS_CANDIDATE_MAX 2048

struct cm_orbis_webrtc {
    int pc;
    int data_channel;
    int video_track;
    int audio_track;
    int width;
    int height;
    volatile int connected;
    volatile int running;
    volatile int closing;
    volatile int input_started;
    pthread_t input_thread;
    cm_cloudmoon_client_t *client;
    cm_layout_t layout;
    cm_input_state_t input_state;
};

static int cm_json_quote(char *out, size_t capacity, const char *value) {
    size_t used = 0;
    if (!out || !capacity || !value || capacity < 3) return -1;
    out[used++] = '"';
    while (*value) {
        unsigned char c = (unsigned char)*value++;
        if (c == '"' || c == '\\') {
            if (used + 2 >= capacity) return -1;
            out[used++] = '\\';
            out[used++] = (char)c;
        } else if (c < 0x20) {
            int n = snprintf(out + used, capacity - used, "\\u%04x", c);
            if (n != 6 || used + 6 >= capacity) return -1;
            used += 6;
        } else {
            if (used + 1 >= capacity) return -1;
            out[used++] = (char)c;
        }
    }
    if (used + 2 > capacity) return -1;
    out[used++] = '"';
    out[used] = 0;
    return (int)used;
}

static int cm_send_signal(cm_orbis_webrtc_t *rtc, const char *type,
                          const char *value, const char *mid, int label) {
    size_t value_len, quoted_capacity, json_capacity;
    char *quoted = NULL, *json = NULL;
    char quoted_mid[128];
    int n, result = -1;

    if (!rtc || !rtc->client || !type || !value ||
        __sync_fetch_and_add(&rtc->closing, 0))
        return -1;

    value_len = strlen(value);
    if (value_len > CM_ORBIS_SDP_MAX) return -1;
    quoted_capacity = value_len * 6u + 3u;
    json_capacity = quoted_capacity + 256u;

    quoted = (char *)malloc(quoted_capacity);
    json = (char *)malloc(json_capacity);
    if (!quoted || !json) goto done;
    if (cm_json_quote(quoted, quoted_capacity, value) < 0) goto done;

    if (mid) {
        if (cm_json_quote(quoted_mid, sizeof(quoted_mid), mid) < 0) goto done;
        n = snprintf(json, json_capacity,
                     "{\"type\":\"%s\",\"candidate\":%s,\"label\":%d,\"id\":%s}",
                     type, quoted, label, quoted_mid);
    } else {
        n = snprintf(json, json_capacity,
                     "{\"type\":\"%s\",\"sdp\":%s}", type, quoted);
    }
    if (n >= 0 && (size_t)n < json_capacity)
        result = cm_cloudmoon_client_send_signal(rtc->client, json, (size_t)n);

done:
    free(json);
    free(quoted);
    return result;
}

static void cm_rtc_local_description(int pc, const char *sdp,
                                     const char *type, void *context) {
    cm_orbis_webrtc_t *rtc = (cm_orbis_webrtc_t *)context;
    if (!rtc || pc != rtc->pc || !sdp || !type ||
        __sync_fetch_and_add(&rtc->closing, 0))
        return;
    (void)cm_send_signal(rtc, type, sdp, NULL, 0);
}

static void cm_rtc_local_candidate(int pc, const char *candidate,
                                   const char *mid, void *context) {
    cm_orbis_webrtc_t *rtc = (cm_orbis_webrtc_t *)context;
    int label = 0;
    if (!rtc || pc != rtc->pc || !candidate ||
        __sync_fetch_and_add(&rtc->closing, 0))
        return;

    /* The CloudMoon browser client deliberately does not publish host ICE. */
    if (strstr(candidate, " typ host"))
        return;
    if (mid && mid[0] >= '0' && mid[0] <= '9')
        label = atoi(mid);
    (void)cm_send_signal(rtc, "candidate", candidate, mid ? mid : "0", label);
}

static void cm_rtc_state_changed(int pc, rtcState state, void *context) {
    cm_orbis_webrtc_t *rtc = (cm_orbis_webrtc_t *)context;
    if (!rtc || pc != rtc->pc || __sync_fetch_and_add(&rtc->closing, 0))
        return;
    __sync_lock_test_and_set(&rtc->connected, state == RTC_CONNECTED);
    if (state == RTC_FAILED) {
        __sync_lock_test_and_set(&rtc->running, 0);
        cm_cloudmoon_client_request_stop();
    }
}

static void cm_rtc_data_channel_open(int id, void *context) {
    cm_orbis_webrtc_t *rtc = (cm_orbis_webrtc_t *)context;
    char initial[64];
    int n;
    if (!rtc || __sync_fetch_and_add(&rtc->closing, 0))
        return;
    __sync_lock_test_and_set(&rtc->data_channel, id);
    n = snprintf(initial, sizeof(initial), "res>%dx%d", rtc->width, rtc->height);
    if (n > 0 && (size_t)n < sizeof(initial))
        (void)rtcSendMessage(id, initial, n);
}

static void cm_rtc_data_channel_message(int id, const char *message,
                                        int size, void *context) {
    cm_orbis_webrtc_t *rtc = (cm_orbis_webrtc_t *)context;
    (void)id;
    if (!rtc || !message || size <= 0 ||
        __sync_fetch_and_add(&rtc->closing, 0))
        return;
    if (size >= 8 && memcmp(message, "kickout>", 8) == 0) {
        __sync_lock_test_and_set(&rtc->running, 0);
        __sync_lock_test_and_set(&rtc->connected, 0);
        cm_cloudmoon_client_request_stop();
    }
}

static void cm_rtc_data_channel_closed(int id, void *context) {
    cm_orbis_webrtc_t *rtc = (cm_orbis_webrtc_t *)context;
    if (rtc && __sync_fetch_and_add(&rtc->data_channel, 0) == id)
        __sync_lock_test_and_set(&rtc->data_channel, -1);
}

static void cm_rtc_data_channel(int pc, int dc, void *context) {
    cm_orbis_webrtc_t *rtc = (cm_orbis_webrtc_t *)context;
    (void)pc;
    if (!rtc || dc < 0 || __sync_fetch_and_add(&rtc->closing, 0))
        return;
    __sync_lock_test_and_set(&rtc->data_channel, dc);
    rtcSetUserPointer(dc, rtc);
    rtcSetOpenCallback(dc, cm_rtc_data_channel_open);
    rtcSetMessageCallback(dc, cm_rtc_data_channel_message);
    rtcSetClosedCallback(dc, cm_rtc_data_channel_closed);
}

static void cm_rtc_video_frame(int track, const char *data, int size,
                               const rtcFrameInfo *info, void *context) {
    cm_orbis_webrtc_t *rtc = (cm_orbis_webrtc_t *)context;
    (void)track;
    (void)info;
    if (!rtc || !data || size <= 0 ||
        __sync_fetch_and_add(&rtc->closing, 0))
        return;
    if (cm_orbis_h264_submit((const uint8_t *)data, (size_t)size) < 0) {
        /* A damaged access unit is recoverable; next IDR can resynchronize. */
        return;
    }
}

static void cm_rtc_audio_frame(int track, const char *data, int size,
                               const rtcFrameInfo *info, void *context) {
    cm_orbis_webrtc_t *rtc = (cm_orbis_webrtc_t *)context;
    (void)track;
    (void)info;
    if (!rtc || !data || size <= 0 ||
        __sync_fetch_and_add(&rtc->closing, 0))
        return;
    (void)cm_orbis_audio_submit_opus((const uint8_t *)data, (size_t)size);
}

static void cm_rtc_track(int pc, int track, void *context) {
    cm_orbis_webrtc_t *rtc = (cm_orbis_webrtc_t *)context;
    char description[1024];
    int n;
    if (!rtc || pc != rtc->pc || track < 0 ||
        __sync_fetch_and_add(&rtc->closing, 0))
        return;

    n = rtcGetTrackDescription(track, description, (int)sizeof(description));
    if (n < 0 || n >= (int)sizeof(description))
        return;
    description[sizeof(description) - 1] = 0;
    rtcSetUserPointer(track, rtc);

    if (strstr(description, "m=video") != NULL) {
        if (rtcSetH264Depacketizer(track, RTC_NAL_SEPARATOR_START_SEQUENCE) < 0)
            return;
        if (rtcSetFrameCallback(track, cm_rtc_video_frame) == 0)
            __sync_lock_test_and_set(&rtc->video_track, track);
    } else if (strstr(description, "m=audio") != NULL) {
        if (rtcSetOpusDepacketizer(track) < 0)
            return;
        if (rtcSetFrameCallback(track, cm_rtc_audio_frame) == 0)
            __sync_lock_test_and_set(&rtc->audio_track, track);
    }
}

static void cm_pad_to_cloudmoon(const cm_orbis_gamepad_state_t *raw,
                               cm_pad_t *pad) {
    memset(pad, 0, sizeof(*pad));
    pad->lx = raw->lx;
    pad->ly = raw->ly;
    pad->rx = raw->rx;
    pad->ry = raw->ry;
    pad->cross = raw->cross;
    pad->circle = raw->circle;
    pad->square = raw->square;
    pad->triangle = raw->triangle;
    pad->l1 = raw->l1;
    pad->r1 = raw->r1;
    pad->l2 = raw->l2;
    pad->r2 = raw->r2;
    pad->up = raw->up;
    pad->down = raw->down;
    pad->left = raw->left;
    pad->right = raw->right;
    pad->start = raw->options;
    pad->select = raw->touchpad;
}

static void *cm_orbis_input_thread(void *argument) {
    cm_orbis_webrtc_t *rtc = (cm_orbis_webrtc_t *)argument;
    char messages[512];
    unsigned int stats_frame = 0;

    if (cm_orbis_input_init() != 0) {
        __sync_lock_test_and_set(&rtc->running, 0);
        cm_cloudmoon_client_request_stop();
        return NULL;
    }

    while (rtc && __sync_fetch_and_add(&rtc->running, 0) &&
           !__sync_fetch_and_add(&rtc->closing, 0)) {
        cm_orbis_gamepad_state_t raw;
        cm_pad_t pad;
        int dc, length;
        size_t offset = 0;

        if (cm_orbis_gamepad_read(&raw) == 0 && raw.connected) {
            if (raw.options && raw.circle) {
                __sync_lock_test_and_set(&rtc->running, 0);
                cm_cloudmoon_client_request_stop();
                break;
            }

            cm_pad_to_cloudmoon(&raw, &pad);
            length = cm_input_translate(&rtc->layout, &rtc->input_state,
                                        &pad, messages, sizeof(messages));
            dc = __sync_fetch_and_add(&rtc->data_channel, 0);
            if (length > 0 && dc >= 0 && rtcIsOpen(dc)) {
                while (offset < (size_t)length) {
                    size_t message_length = strlen(messages + offset);
                    if (message_length == 0)
                        break;
                    (void)rtcSendMessage(dc, messages + offset,
                                         (int)message_length);
                    offset += message_length + 1u;
                }
            } else if (dc < 0 || !rtcIsOpen(dc)) {
                cm_input_reset(&rtc->input_state);
            }

            if (++stats_frame >= 60u) {
                stats_frame = 0;
                if (dc >= 0 && rtcIsOpen(dc))
                    (void)rtcSendMessage(dc, "stats>", 6);
            }
        }
        usleep(16000);
    }

    cm_orbis_input_shutdown();
    return NULL;
}

int cm_orbis_webrtc_create(cm_orbis_webrtc_t **out,
                           cm_cloudmoon_client_t *client,
                           int width, int height) {
    static const char *ice_servers[] = {
        "stun:stun.ipfire.org:3478",
        "stun:global.stun.twilio.com:19302",
        "stun:stun.l.google.com:19302"
    };
    rtcConfiguration configuration;
    rtcTrackInit video_init, audio_init;
    cm_orbis_webrtc_t *rtc;

    if (!out || !client || width <= 0 || height <= 0)
        return -1;
    *out = NULL;

    rtc = (cm_orbis_webrtc_t *)calloc(1, sizeof(*rtc));
    if (!rtc)
        return -1;

    rtc->pc = -1;
    rtc->data_channel = -1;
    rtc->video_track = -1;
    rtc->audio_track = -1;
    rtc->client = client;
    rtc->width = width;
    rtc->height = height;
    cm_layout_default(&rtc->layout, width, height);
    cm_input_reset(&rtc->input_state);

    if (cm_orbis_h264_start(width, height) != 0)
        goto fail;
    if (cm_orbis_audio_start() != 0)
        goto fail;

    memset(&configuration, 0, sizeof(configuration));
    configuration.iceServers = ice_servers;
    configuration.iceServersCount = (int)(sizeof(ice_servers) / sizeof(ice_servers[0]));
    configuration.enableIceUdpMux = true;
    configuration.disableAutoNegotiation = true;
    configuration.forceMediaTransport = true;
    configuration.mtu = 1200;

    rtc->pc = rtcCreatePeerConnection(&configuration);
    if (rtc->pc < 0)
        goto fail;

    rtcSetUserPointer(rtc->pc, rtc);
    if (rtcSetLocalDescriptionCallback(rtc->pc, cm_rtc_local_description) < 0 ||
        rtcSetLocalCandidateCallback(rtc->pc, cm_rtc_local_candidate) < 0 ||
        rtcSetStateChangeCallback(rtc->pc, cm_rtc_state_changed) < 0 ||
        rtcSetDataChannelCallback(rtc->pc, cm_rtc_data_channel) < 0 ||
        rtcSetTrackCallback(rtc->pc, cm_rtc_track) < 0)
        goto fail;

    memset(&video_init, 0, sizeof(video_init));
    video_init.direction = RTC_DIRECTION_RECVONLY;
    video_init.codec = RTC_CODEC_H264;
    video_init.payloadType = 100;
    video_init.mid = "0";
    if (rtcAddTrackEx(rtc->pc, &video_init) < 0)
        goto fail;

    memset(&audio_init, 0, sizeof(audio_init));
    audio_init.direction = RTC_DIRECTION_RECVONLY;
    audio_init.codec = RTC_CODEC_OPUS;
    audio_init.payloadType = 111;
    audio_init.mid = "1";
    if (rtcAddTrackEx(rtc->pc, &audio_init) < 0)
        goto fail;

    rtc->data_channel = rtcCreateDataChannel(rtc->pc, "config");
    if (rtc->data_channel < 0)
        goto fail;
    rtcSetUserPointer(rtc->data_channel, rtc);
    rtcSetOpenCallback(rtc->data_channel, cm_rtc_data_channel_open);
    rtcSetMessageCallback(rtc->data_channel, cm_rtc_data_channel_message);
    rtcSetClosedCallback(rtc->data_channel, cm_rtc_data_channel_closed);

    __sync_lock_test_and_set(&rtc->running, 1);
    if (pthread_create(&rtc->input_thread, NULL,
                       cm_orbis_input_thread, rtc) != 0)
        goto fail;
    rtc->input_started = 1;
    *out = rtc;
    return 0;

fail:
    cm_orbis_webrtc_close(rtc);
    return -1;
}

void cm_orbis_webrtc_signal(void *context,
                            cm_cloudmoon_client_t *client,
                            const char *json, size_t length) {
    cm_orbis_webrtc_t *rtc = (cm_orbis_webrtc_t *)context;
    char type[32], sdp[CM_ORBIS_SDP_MAX], candidate[CM_ORBIS_CANDIDATE_MAX];
    char mid[64];
    if (!rtc || !client || !json || length == 0 ||
        __sync_fetch_and_add(&rtc->closing, 0))
        return;
    rtc->client = client;
    if (cm_json_get_string(json, length, "type", type, sizeof(type)) < 0)
        return;

    if (strcmp(type, "offer") == 0) {
        if (cm_json_get_string(json, length, "sdp", sdp, sizeof(sdp)) < 0)
            return;
        if (rtcSetRemoteDescription(rtc->pc, sdp, "offer") != RTC_ERR_SUCCESS) {
            cm_cloudmoon_client_request_stop();
            return;
        }
        if (rtcSetLocalDescription(rtc->pc, "answer") != RTC_ERR_SUCCESS)
            cm_cloudmoon_client_request_stop();
        return;
    }

    if (strcmp(type, "candidate") == 0) {
        if (cm_json_get_string(json, length, "candidate",
                               candidate, sizeof(candidate)) < 0)
            return;
        if (cm_json_get_string(json, length, "id", mid, sizeof(mid)) < 0 &&
            cm_json_get_string(json, length, "mid", mid, sizeof(mid)) < 0)
            (void)snprintf(mid, sizeof(mid), "0");
        (void)rtcAddRemoteCandidate(rtc->pc, candidate, mid);
    }
}

int cm_orbis_webrtc_send_input(cm_orbis_webrtc_t *rtc,
                               const char *message, size_t length) {
    int dc;
    if (!rtc || !message || length == 0 || length > 4096 ||
        __sync_fetch_and_add(&rtc->closing, 0))
        return -1;
    dc = __sync_fetch_and_add(&rtc->data_channel, 0);
    if (dc < 0 || !rtcIsOpen(dc))
        return -1;
    return rtcSendMessage(dc, message, (int)length);
}

int cm_orbis_webrtc_is_connected(const cm_orbis_webrtc_t *rtc) {
    return rtc ? __sync_fetch_and_add((volatile int *)&rtc->connected, 0) : 0;
}

void cm_orbis_webrtc_close(cm_orbis_webrtc_t *rtc) {
    int pc, dc, video_track, audio_track;
    if (!rtc)
        return;
    __sync_lock_test_and_set(&rtc->closing, 1);
    __sync_lock_test_and_set(&rtc->running, 0);
    if (rtc->input_started) {
        (void)pthread_join(rtc->input_thread, NULL);
        rtc->input_started = 0;
    }

    pc = rtc->pc;
    dc = rtc->data_channel;
    video_track = rtc->video_track;
    audio_track = rtc->audio_track;
    rtc->pc = -1;
    rtc->data_channel = -1;

    if (video_track >= 0)
        (void)rtcSetFrameCallback(video_track, NULL);
    if (audio_track >= 0)
        (void)rtcSetFrameCallback(audio_track, NULL);
    if (pc >= 0)
        (void)rtcClosePeerConnection(pc);
    if (dc >= 0) {
        (void)rtcClose(dc);
        (void)rtcDeleteDataChannel(dc);
    }
    if (pc >= 0)
        (void)rtcDeletePeerConnection(pc);

    cm_orbis_h264_stop();
    cm_orbis_audio_stop();
    free(rtc);
}
