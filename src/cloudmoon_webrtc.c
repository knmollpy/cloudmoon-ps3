/*
 * CloudMoon WebRTC adapter using libdatachannel's C API.
 * Requires ICE, DTLS, SRTP, SCTP and media support.
 */
#include "cloudmoon_webrtc.h"
#include "cloudmoon_session.h"
#include "cloudmoon_input.h"
#include "video.h"
#include "audio.h"
#include "input.h"
#include "ui.h"
#include <Limelight.h>
#include <rtc/rtc.h>
#include <sys/thread.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CM_RTC_SDP_MAX 16384
#define CM_RTC_MESSAGE_MAX 2048
#define CM_RTC_MAX_MIDS 8
#define CM_RTC_PENDING_CANDIDATES 16
#define CM_RTC_CANDIDATE_MAX 2048
#define CM_ATOMIC_LOAD(p) __sync_fetch_and_add((p), 0)
#define CM_ATOMIC_STORE(p, v) __sync_lock_test_and_set((p), (v))
#define CM_ATOMIC_CAS(p, oldv, newv) __sync_val_compare_and_swap((p), (oldv), (newv))

struct cm_cloudmoon_webrtc {
    int pc,video_track,audio_track;
    volatile int data_channel,video_started,audio_started,connected,closing;
    int width,height,video_width,video_height;
    volatile int media_callbacks,video_blocked,video_keyframe_requested,video_failure_logged;
    volatile int input_running;
    int input_started;
    sys_ppu_thread_t input_thread;
    cm_layout_t layout;
    cm_input_state_t input_state;
    cm_cloudmoon_client_t *client;
    volatile int signaling_detached, signaling_users;
    volatile int pending_lock;
    int remote_description_set, pending_count;
    struct { char candidate[CM_RTC_CANDIDATE_MAX]; char mid[32]; } pending[CM_RTC_PENDING_CANDIDATES];
    char remote_mids[CM_RTC_MAX_MIDS][32];
};

static int cm_json_quote(char*out,size_t cap,const char*s) {
    size_t n=0;
    if(!out||!cap||!s||n+1>=cap)return -1;
    out[n++]='"';
    while(*s) {
        unsigned char c=(unsigned char)*s++;
        if(c=='"'||c=='\\'){if(n+2>=cap)return -1;out[n++]='\\';out[n++]=(char)c;}
        else if(c<0x20){int z=snprintf(out+n,cap-n,"\\u%04x",c);if(z!=6)return -1;n+=6;}
        else{if(n+1>=cap)return -1;out[n++]=(char)c;}
    }
    if(n+2>cap)return -1;
    out[n++]='"';out[n]=0;return (int)n;
}

static int cm_rtc_send_signal(cm_cloudmoon_webrtc_t*rtc,const char*type,
                              const char*value,const char*mid,int label) {
    /*
     * SDP contains CR/LF control bytes. cm_json_quote expands each control
     * byte to six characters, so reserve the worst-case escaped size.
     * Keep these buffers off the small PS3 PPU callback/thread stacks.
     */
    const size_t qcap=(CM_RTC_SDP_MAX+256U)*6U+1U;
    const size_t jcap=qcap+512U;
    char qmid[128];
    char *storage,*qvalue,*json;
    int n,result=-1;
    cm_cloudmoon_client_t *client;
    if(!rtc||!type||!value)return -1;
    if(__sync_val_compare_and_swap(&rtc->signaling_detached,0,0))return -1;
    __sync_add_and_fetch(&rtc->signaling_users,1);
    if(__sync_val_compare_and_swap(&rtc->signaling_detached,0,0)) {
        __sync_sub_and_fetch(&rtc->signaling_users,1);
        return -1;
    }
    client=__sync_val_compare_and_swap(&rtc->client,(cm_cloudmoon_client_t*)0,(cm_cloudmoon_client_t*)0);
    if(!client)goto done;
    storage=(char*)malloc(qcap+jcap);
    if(!storage)goto done;
    qvalue=storage;
    json=storage+qcap;
    if(cm_json_quote(qvalue,qcap,value)<0)goto cleanup;
    if(mid) {
        if(cm_json_quote(qmid,sizeof(qmid),mid)<0)goto cleanup;
        n=snprintf(json,jcap,"{\"type\":\"%s\",\"candidate\":%s,\"label\":%d,\"id\":%s}",type,qvalue,label,qmid);
    } else {
        n=snprintf(json,jcap,"{\"type\":\"%s\",\"sdp\":%s}",type,qvalue);
    }
    if(n>=0&&(size_t)n<jcap)
        result=cm_cloudmoon_client_send_signal(client,json,(size_t)n);
cleanup:
    free(storage);
done:
    __sync_sub_and_fetch(&rtc->signaling_users,1);
    return result;
}

static void cm_rtc_local_description(int pc,const char*sdp,const char*type,void*ptr) {
    cm_cloudmoon_webrtc_t*rtc=(cm_cloudmoon_webrtc_t*)ptr;
    if(rtc&&pc==rtc->pc&&sdp&&type)(void)cm_rtc_send_signal(rtc,type,sdp,NULL,0);
}

static void cm_rtc_local_candidate(int pc,const char*candidate,const char*mid,void*ptr) {
    cm_cloudmoon_webrtc_t*rtc=(cm_cloudmoon_webrtc_t*)ptr;
    int label=0;
    if(!rtc||pc!=rtc->pc||!candidate)return;
    /* The browser client deliberately excludes host ICE candidates. */
    if(strstr(candidate," typ host"))return;
    if(mid&&mid[0]>='0'&&mid[0]<='9')label=atoi(mid);
    (void)cm_rtc_send_signal(rtc,"candidate",candidate,mid?mid:"0",label);
}

static void cm_rtc_state(int pc,rtcState state,void*ptr) {
    cm_cloudmoon_webrtc_t*rtc=(cm_cloudmoon_webrtc_t*)ptr;
    if(rtc&&!CM_ATOMIC_LOAD(&rtc->closing)&&pc==rtc->pc){CM_ATOMIC_STORE(&rtc->connected,(state==RTC_CONNECTED));if(state==RTC_CONNECTED)ui_set_state(UI_STATE_STREAMING);else if(state==RTC_DISCONNECTED||state==RTC_FAILED||state==RTC_CLOSED)ui_set_state(UI_STATE_ERROR);}
}

static void cm_rtc_channel_message(int id,const char*message,int size,void*ptr) {
    cm_cloudmoon_webrtc_t*rtc=(cm_cloudmoon_webrtc_t*)ptr;
    (void)id;
    if(!rtc||CM_ATOMIC_LOAD(&rtc->closing)||!message||size<=0)return;
    if(size>=9&&!memcmp(message,"rotation>",9)) {
        ui_set_state(UI_STATE_STREAMING);
        return;
    }
    if(size>=8&&!memcmp(message,"kickout>",8)){
        CM_ATOMIC_STORE(&rtc->connected,0);
        ui_set_state(UI_STATE_ERROR);
    }
}
static void cm_rtc_channel_closed(int id,void*ptr) {
    cm_cloudmoon_webrtc_t*rtc=(cm_cloudmoon_webrtc_t*)ptr;
    if(rtc&&CM_ATOMIC_LOAD(&rtc->data_channel)==id)CM_ATOMIC_STORE(&rtc->data_channel,-1);
}

static void cm_rtc_channel_open(int id,void*ptr) {
    cm_cloudmoon_webrtc_t*rtc=(cm_cloudmoon_webrtc_t*)ptr;
    char initial[64];int n;
    if(!rtc||CM_ATOMIC_LOAD(&rtc->closing))return;
    CM_ATOMIC_STORE(&rtc->data_channel,id);
    n=snprintf(initial,sizeof(initial),"res>%dx%d",rtc->width,rtc->height);
    if(n>0&&(size_t)n<sizeof(initial))(void)rtcSendMessage(id,initial,n);
    /* Ad-block status is not known on PS3; do not falsely report it as enabled. */
}

static void cm_rtc_data_channel(int pc,int dc,void*ptr) {
    cm_cloudmoon_webrtc_t*rtc=(cm_cloudmoon_webrtc_t*)ptr;
    (void)pc;
    if(!rtc||CM_ATOMIC_LOAD(&rtc->closing)||dc<0)return;
    CM_ATOMIC_STORE(&rtc->data_channel,dc);
    rtcSetUserPointer(dc,rtc);
    rtcSetOpenCallback(dc,cm_rtc_channel_open);
    rtcSetMessageCallback(dc,cm_rtc_channel_message);
    rtcSetClosedCallback(dc,cm_rtc_channel_closed);
}


/* Minimal bounded H.264 SPS reader. Access units arrive Annex-B after the
   libdatachannel depacketizer. The parser only needs the SPS fields up to crop. */
typedef struct {
    const uint8_t *data;
    size_t bit_pos, byte_len;
} cm_h264_bit_reader;

static int cm_h264_read_bit(cm_h264_bit_reader *br, uint32_t *value) {
    size_t byte_pos;
    unsigned shift;
    if (!br || !value || br->bit_pos >= br->byte_len * 8U) return -1;
    byte_pos = br->bit_pos >> 3;
    shift = 7U - (unsigned)(br->bit_pos & 7U);
    *value = (br->data[byte_pos] >> shift) & 1U;
    br->bit_pos++;
    return 0;
}
static int cm_h264_read_bits(cm_h264_bit_reader *br, unsigned count, uint32_t *value) {
    uint32_t out = 0, bit;
    unsigned i;
    if (!value || count > 32) return -1;
    for (i = 0; i < count; ++i) {
        if (cm_h264_read_bit(br, &bit) != 0) return -1;
        out = (out << 1) | bit;
    }
    *value = out;
    return 0;
}
static int cm_h264_read_ue(cm_h264_bit_reader *br, uint32_t *value) {
    unsigned zeros = 0;
    uint32_t bit, suffix = 0;
    uint64_t code;
    do {
        if (cm_h264_read_bit(br, &bit) != 0) return -1;
        if (bit) break;
        if (++zeros > 31) return -1;
    } while (1);
    if (zeros && cm_h264_read_bits(br, zeros, &suffix) != 0) return -1;
    code = (((uint64_t)1 << zeros) - 1U) + suffix;
    if (code > 0xffffffffULL) return -1;
    *value = (uint32_t)code;
    return 0;
}
static int cm_h264_read_se(cm_h264_bit_reader *br, int32_t *value) {
    uint32_t code;
    int64_t signed_value;
    if (!value || cm_h264_read_ue(br, &code) != 0) return -1;
    signed_value = (code & 1U) ? (int64_t)((code + 1ULL) / 2ULL) : -(int64_t)(code / 2U);
    if (signed_value < -2147483647LL || signed_value > 2147483647LL) return -1;
    *value = (int32_t)signed_value;
    return 0;
}
static int cm_h264_skip_scaling_list(cm_h264_bit_reader *br, unsigned count) {
    int last_scale = 8, next_scale = 8;
    unsigned j;
    for (j = 0; j < count; ++j) {
        if (next_scale != 0) {
            int32_t delta_scale;
            if (cm_h264_read_se(br, &delta_scale) != 0) return -1;
            next_scale = (last_scale + delta_scale + 256) & 255;
        }
        if (next_scale != 0) last_scale = next_scale;
    }
    return 0;
}
static int cm_h264_parse_sps(const uint8_t *nal, size_t nal_len,
                             int *out_width, int *out_height) {
    uint8_t rbsp[1024];
    size_t rbsp_len = 0, i;
    unsigned zero_count = 0;
    uint32_t profile, ignored, chroma_format = 1, separate_colour_plane = 0;
    uint32_t width_mbs_minus1, height_map_units_minus1, frame_mbs_only;
    uint32_t pic_order_cnt_type, crop_flag = 0, crop_left = 0, crop_right = 0;
    uint32_t crop_top = 0, crop_bottom = 0, bit;
    uint64_t coded_width, coded_height, crop_x = 1, crop_y = 1;
    cm_h264_bit_reader br;
    if (!nal || nal_len < 4 || !out_width || !out_height ||
        (nal[0] & 0x1fU) != 7U) return -1;
    for (i = 1; i < nal_len && rbsp_len < sizeof(rbsp); ++i) {
        uint8_t b = nal[i];
        if (zero_count >= 2 && b == 3) {
            zero_count = 0;
            continue;
        }
        rbsp[rbsp_len++] = b;
        if (b == 0) zero_count++; else zero_count = 0;
    }
    if (i < nal_len || rbsp_len < 3) return -1;
    br.data = rbsp; br.byte_len = rbsp_len; br.bit_pos = 0;
    if (cm_h264_read_bits(&br, 8, &profile) != 0 ||
        cm_h264_read_bits(&br, 8, &ignored) != 0 ||
        cm_h264_read_bits(&br, 8, &ignored) != 0 ||
        cm_h264_read_ue(&br, &ignored) != 0) return -1;

    if (profile == 100 || profile == 110 || profile == 122 || profile == 244 ||
        profile == 44 || profile == 83 || profile == 86 || profile == 118 ||
        profile == 128 || profile == 138 || profile == 139 || profile == 134 ||
        profile == 135 || profile == 144) {
        uint32_t scaling_matrix_present;
        if (cm_h264_read_ue(&br, &chroma_format) != 0 || chroma_format > 3) return -1;
        if (chroma_format == 3 &&
            cm_h264_read_bit(&br, &separate_colour_plane) != 0) return -1;
        if (cm_h264_read_ue(&br, &ignored) != 0 ||
            cm_h264_read_ue(&br, &ignored) != 0 ||
            cm_h264_read_bit(&br, &bit) != 0 ||
            cm_h264_read_bit(&br, &scaling_matrix_present) != 0) return -1;
        if (scaling_matrix_present) {
            unsigned count = chroma_format == 3 ? 12U : 8U;
            unsigned j;
            for (j = 0; j < count; ++j) {
                uint32_t present;
                if (cm_h264_read_bit(&br, &present) != 0) return -1;
                if (present && cm_h264_skip_scaling_list(&br, j < 6 ? 16U : 64U) != 0)
                    return -1;
            }
        }
    }
    if (cm_h264_read_ue(&br, &ignored) != 0 ||
        cm_h264_read_ue(&br, &pic_order_cnt_type) != 0) return -1;
    if (pic_order_cnt_type == 0) {
        if (cm_h264_read_ue(&br, &ignored) != 0) return -1;
    } else if (pic_order_cnt_type == 1) {
        uint32_t cycle, j;
        int32_t signed_ignored;
        if (cm_h264_read_bit(&br, &bit) != 0 ||
            cm_h264_read_se(&br, &signed_ignored) != 0 ||
            cm_h264_read_se(&br, &signed_ignored) != 0 ||
            cm_h264_read_ue(&br, &cycle) != 0 || cycle > 255) return -1;
        for (j = 0; j < cycle; ++j)
            if (cm_h264_read_se(&br, &signed_ignored) != 0) return -1;
    } else if (pic_order_cnt_type != 2) {
        return -1;
    }
    if (cm_h264_read_ue(&br, &ignored) != 0 ||
        cm_h264_read_bit(&br, &bit) != 0 ||
        cm_h264_read_ue(&br, &width_mbs_minus1) != 0 ||
        cm_h264_read_ue(&br, &height_map_units_minus1) != 0 ||
        cm_h264_read_bit(&br, &frame_mbs_only) != 0) return -1;
    if (!frame_mbs_only && cm_h264_read_bit(&br, &bit) != 0) return -1;
    if (cm_h264_read_bit(&br, &bit) != 0 ||
        cm_h264_read_bit(&br, &crop_flag) != 0) return -1;
    if (crop_flag &&
        (cm_h264_read_ue(&br, &crop_left) != 0 ||
         cm_h264_read_ue(&br, &crop_right) != 0 ||
         cm_h264_read_ue(&br, &crop_top) != 0 ||
         cm_h264_read_ue(&br, &crop_bottom) != 0)) return -1;

    coded_width = ((uint64_t)width_mbs_minus1 + 1U) * 16U;
    coded_height = ((uint64_t)height_map_units_minus1 + 1U) * 16U *
                   (2U - frame_mbs_only);
    if (coded_width > 8192U || coded_height > 8192U) return -1;
    if (separate_colour_plane) chroma_format = 0;
    if (chroma_format == 1) { crop_x = 2; crop_y = 2U * (2U - frame_mbs_only); }
    else if (chroma_format == 2) { crop_x = 2; crop_y = 1U * (2U - frame_mbs_only); }
    else { crop_x = 1; crop_y = 1U * (2U - frame_mbs_only); }
    if (((uint64_t)crop_left + crop_right) * crop_x >= coded_width ||
        ((uint64_t)crop_top + crop_bottom) * crop_y >= coded_height) return -1;
    coded_width -= ((uint64_t)crop_left + crop_right) * crop_x;
    coded_height -= ((uint64_t)crop_top + crop_bottom) * crop_y;
    if (!coded_width || !coded_height || coded_width > 8192U || coded_height > 8192U)
        return -1;
    *out_width = (int)coded_width;
    *out_height = (int)coded_height;
    return 0;
}
static size_t cm_h264_find_start_code(const uint8_t *data, size_t length,
                                      size_t from, size_t *prefix_len) {
    size_t i;
    for (i = from; i + 3 <= length; ++i) {
        if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) {
            *prefix_len = 3;
            return i;
        }
        if (i + 4 <= length && data[i] == 0 && data[i + 1] == 0 &&
            data[i + 2] == 0 && data[i + 3] == 1) {
            *prefix_len = 4;
            return i;
        }
    }
    return length;
}
static int cm_h264_find_sps(const uint8_t *data, size_t length,
                            int *width, int *height) {
    size_t prefix = 0, start, next_prefix, end;
    if (!data || !length || !width || !height) return -1;
    if ((data[0] & 0x1fU) == 7U)
        return cm_h264_parse_sps(data, length, width, height) == 0 ? 1 : -1;
    start = cm_h264_find_start_code(data, length, 0, &prefix);
    while (start < length) {
        size_t nal_start = start + prefix;
        end = cm_h264_find_start_code(data, length, nal_start, &next_prefix);
        if (nal_start < end && (data[nal_start] & 0x1fU) == 7U)
            return cm_h264_parse_sps(data + nal_start, end - nal_start,
                                     width, height) == 0 ? 1 : -1;
        start = end;
        if (start < length) prefix = next_prefix;
    }
    return 0;
}

static void cm_rtc_request_keyframe(cm_cloudmoon_webrtc_t *rtc, int track) {
    int video_track = track >= 0 ? track : (rtc ? rtc->video_track : -1);
    if (video_track >= 0) (void)rtcRequestKeyframe(video_track);
}
static void cm_rtc_request_keyframe_once(cm_cloudmoon_webrtc_t *rtc, int track) {
    if (rtc && CM_ATOMIC_CAS(&rtc->video_keyframe_requested, 0, 1) == 0)
        cm_rtc_request_keyframe(rtc, track);
}
static void cm_rtc_video_fail(cm_cloudmoon_webrtc_t *rtc, const char *message) {
    if (!rtc) return;
    CM_ATOMIC_STORE(&rtc->video_blocked, 1);
    CM_ATOMIC_STORE(&rtc->connected, 0);
    ui_set_state(UI_STATE_ERROR);
    if (CM_ATOMIC_CAS(&rtc->video_failure_logged, 0, 1) == 0)
        ui_push_log(message);
}

static void cm_rtc_video_frame(int track,const char*data,int size,
                               const rtcFrameInfo*info,void*ptr) {
    cm_cloudmoon_webrtc_t*rtc=(cm_cloudmoon_webrtc_t*)ptr;
    int actual_width=0, actual_height=0, sps_result, submit_result;
    if(!rtc||!data||size<=0||!info)return;
    __sync_add_and_fetch(&rtc->media_callbacks,1);
    if(CM_ATOMIC_LOAD(&rtc->closing))goto done;
    if(CM_ATOMIC_LOAD(&rtc->video_blocked))goto done;

    sps_result=cm_h264_find_sps((const uint8_t*)data,(size_t)size,
                                &actual_width,&actual_height);
    if(sps_result<0) {
        cm_rtc_request_keyframe_once(rtc,track);
        goto done;
    }
    if(sps_result>0) {
        if(actual_width>rtc->width||actual_height>rtc->height) {
            cm_rtc_video_fail(rtc,"CloudMoon: stream resolution exceeds negotiated buffer limit.");
            cm_rtc_request_keyframe_once(rtc,track);
            goto done;
        }
        if(CM_ATOMIC_LOAD(&rtc->video_started)==0) {
            if(CM_ATOMIC_CAS(&rtc->video_started,0,1)==0) {
                rtc->video_width=actual_width;
                rtc->video_height=actual_height;
                if(ps3video_cloudmoon_start(actual_width,actual_height)==0) {
                    CM_ATOMIC_STORE(&rtc->video_started,2);
                    CM_ATOMIC_STORE(&rtc->video_keyframe_requested,0);
                } else {
                    cm_rtc_video_fail(rtc,"CloudMoon: PS3 H.264 decoder initialization failed.");
                }
            }
        } else if(CM_ATOMIC_LOAD(&rtc->video_started)==2 &&
                  (actual_width!=rtc->video_width||actual_height!=rtc->video_height)) {
            cm_rtc_video_fail(rtc,"CloudMoon: stream resolution changed; reconnect at the negotiated size.");
            cm_rtc_request_keyframe_once(rtc,track);
            goto done;
        }
    }
    if(CM_ATOMIC_LOAD(&rtc->video_blocked))goto done;
    if(CM_ATOMIC_LOAD(&rtc->video_started)!=2) {
        cm_rtc_request_keyframe_once(rtc,track);
        goto done;
    }
    submit_result=ps3video_submit_h264((const uint8_t*)data,(size_t)size,info->timestamp);
    if(submit_result!=DR_OK) {
        cm_rtc_request_keyframe_once(rtc,track);
    } else {
        CM_ATOMIC_STORE(&rtc->video_keyframe_requested,0);
    }
done:
    __sync_sub_and_fetch(&rtc->media_callbacks,1);
}

static void cm_rtc_audio_frame(int track,const char*data,int size,
                               const rtcFrameInfo*info,void*ptr) {
    cm_cloudmoon_webrtc_t*rtc=(cm_cloudmoon_webrtc_t*)ptr;
    (void)track;(void)info;
    if(!rtc||!data||size<=0)return;
    __sync_add_and_fetch(&rtc->media_callbacks,1);
    if(CM_ATOMIC_LOAD(&rtc->closing))goto done;
    if(CM_ATOMIC_LOAD(&rtc->audio_started)!=2) {
        if(CM_ATOMIC_CAS(&rtc->audio_started,0,1)==0) {
            if(ps3audio_cloudmoon_start()==0)
                CM_ATOMIC_STORE(&rtc->audio_started,2);
            else
                CM_ATOMIC_STORE(&rtc->audio_started,0);
        }
        if(CM_ATOMIC_LOAD(&rtc->audio_started)!=2)goto done;
    }
    ps3audio_submit_opus((const uint8_t*)data,(size_t)size);
done:
    __sync_sub_and_fetch(&rtc->media_callbacks,1);
}

static void cm_rtc_track(int pc,int track,void*ptr) {
    cm_cloudmoon_webrtc_t*rtc=(cm_cloudmoon_webrtc_t*)ptr;
    char description[2048];int n;
    if(!rtc||pc!=rtc->pc||track<0)return;
    n=rtcGetTrackDescription(track,description,sizeof(description));
    if(n<=0)return;
    rtcSetUserPointer(track,rtc);
    if(strstr(description,"m=video")&&
       (strstr(description,"H264")||strstr(description,"h264"))) {
        if(rtcSetH264Depacketizer(track,RTC_NAL_SEPARATOR_START_SEQUENCE)<0)return;
        if(rtcSetFrameCallback(track,cm_rtc_video_frame)==0)rtc->video_track=track;
    } else if(strstr(description,"m=audio")&&strstr(description,"opus")) {
        if(rtcSetOpusDepacketizer(track)<0)return;
        if(rtcSetFrameCallback(track,cm_rtc_audio_frame)==0)rtc->audio_track=track;
    }
}

static void cm_rtc_parse_mids(cm_cloudmoon_webrtc_t*rtc,const char*sdp) {
    const char*p=sdp;int mline=-1;
    memset(rtc->remote_mids,0,sizeof(rtc->remote_mids));
    while(p&&*p) {
        const char*e=strstr(p,"\n");size_t n=e?(size_t)(e-p):strlen(p);if(n&&p[n-1]=='\r')n--;
        if(n>=2&&p[0]=='m'&&p[1]=='='){if(mline<CM_RTC_MAX_MIDS-1)mline++;}
        else if(mline>=0&&mline<CM_RTC_MAX_MIDS&&n>6&&!memcmp(p,"a=mid:",6)) {
            size_t z=n-6;if(z>=sizeof(rtc->remote_mids[mline]))z=sizeof(rtc->remote_mids[mline])-1;
            memcpy(rtc->remote_mids[mline],p+6,z);rtc->remote_mids[mline][z]=0;
        }
        if(!e)break;p=e+1;
    }
    if(!rtc->remote_mids[0][0])strcpy(rtc->remote_mids[0],"0");
}

static int cm_rtc_prefer_video_payload(char *line) {
    char payloads[512], rebuilt[2048];
    char *tokens[64], *cursor;
    char *sp1=strchr(line,' '), *sp2, *sp3;
    size_t prefix, used;
    int count=0, preferred=-1, i, n;
    if(!sp1 || !(sp2=strchr(sp1+1,' ')) || !(sp3=strchr(sp2+1,' ')))
        return 0;
    if(strlen(sp3+1)>=sizeof(payloads))return -1;
    strcpy(payloads,sp3+1);
    cursor=payloads;
    while(*cursor) {
        while(*cursor==' ')cursor++;
        if(!*cursor)break;
        if(count>=64)return -1;
        tokens[count]=cursor;
        while(*cursor && *cursor!=' ')cursor++;
        if(*cursor)*cursor++=0;
        if(!strcmp(tokens[count],"100"))preferred=count;
        count++;
    }
    if(preferred<=0)return 0;
    prefix=(size_t)(sp3-line+1);
    if(prefix>=sizeof(rebuilt))return -1;
    memcpy(rebuilt,line,prefix);used=prefix;rebuilt[used]=0;
    n=snprintf(rebuilt+used,sizeof(rebuilt)-used,"%s",tokens[preferred]);
    if(n<0||(size_t)n>=sizeof(rebuilt)-used)return -1;
    used+=(size_t)n;
    for(i=0;i<count;i++) {
        if(i==preferred)continue;
        n=snprintf(rebuilt+used,sizeof(rebuilt)-used," %s",tokens[i]);
        if(n<0||(size_t)n>=sizeof(rebuilt)-used)return -1;
        used+=(size_t)n;
    }
    if(used>=2048)return -1;
    memcpy(line,rebuilt,used+1);
    return 0;
}

static int cm_rtc_prepare_offer(char *sdp,size_t cap) {
    char output[CM_RTC_SDP_MAX+256];
    const char *p=sdp;
    size_t used=0;
    while(p && *p) {
        const char *end=strchr(p,'\n');
        size_t raw_len=end?(size_t)(end-p):strlen(p);
        size_t line_len=raw_len;
        char line[2048];
        int media=0;
        if(line_len>=sizeof(line))return -1;
        memcpy(line,p,line_len);line[line_len]=0;
        if(line_len && line[line_len-1]=='\r')line[--line_len]=0;
        if(!strncmp(line,"m=video ",8)) {
            media=1;
            if(cm_rtc_prefer_video_payload(line)<0)return -1;
        } else if(!strncmp(line,"m=audio ",8)) {
            media=1;
        }
        if(used+line_len+2+(media?16:0)+1>sizeof(output))return -1;
        memcpy(output+used,line,line_len);used+=line_len;
        output[used++]='\r';output[used++]='\n';
        if(media) {
            memcpy(output+used,"a=rtcp-xr:rrtr\r\n",16);
            used+=16;
        }
        p=end?end+1:NULL;
    }
    if(used+1>cap)return -1;
    output[used]=0;
    memcpy(sdp,output,used+1);
    return 0;
}

static void cm_rtc_input_loop(void*arg) {
    cm_cloudmoon_webrtc_t*rtc=(cm_cloudmoon_webrtc_t*)arg;
    char messages[512];
    cm_pad_t pad;
    ps3_pad_state_t ps3;
    while(rtc&&CM_ATOMIC_LOAD(&rtc->input_running)) {
        ps3input_get_data(&ps3);
        memset(&pad,0,sizeof(pad));
        pad.lx=ps3.lx;pad.ly=ps3.ly;pad.rx=ps3.rx;pad.ry=ps3.ry;
        pad.cross=!!(ps3.buttons_down&A_FLAG);
        pad.circle=!!(ps3.buttons_down&B_FLAG);
        pad.square=!!(ps3.buttons_down&Y_FLAG);
        pad.triangle=!!(ps3.buttons_down&X_FLAG);
        pad.l1=!!(ps3.buttons_down&LB_FLAG);pad.r1=!!(ps3.buttons_down&RB_FLAG); 
        pad.l2=ps3.l2;pad.r2=ps3.r2;
        pad.up=!!(ps3.buttons_down&UP_FLAG);pad.down=!!(ps3.buttons_down&DOWN_FLAG);
        pad.left=!!(ps3.buttons_down&LEFT_FLAG);pad.right=!!(ps3.buttons_down&RIGHT_FLAG);
        pad.start=!!(ps3.buttons_down&PLAY_FLAG);pad.select=!!(ps3.buttons_down&BACK_FLAG);
        {
            int dc=CM_ATOMIC_LOAD(&rtc->data_channel);
            if(dc>=0&&rtcIsOpen(dc)) {
                int n=cm_input_translate(&rtc->layout,&rtc->input_state,&pad,messages,sizeof(messages));
                size_t off=0;
                while(n>0&&off<(size_t)n) {
                    size_t z=strlen(messages+off);
                    if(z==0)break;
                    (void)rtcSendMessage(dc,messages+off,(int)z);
                    off+=z+1;
                }
                if(rtc->input_state.frame%60==0)
                    (void)rtcSendMessage(dc,"stats>",6);
            } else {
                cm_input_reset(&rtc->input_state);
            }
        }
        usleep(16000);
    }
    sysThreadExit(0);
}

int cm_cloudmoon_webrtc_create(cm_cloudmoon_webrtc_t**out,
                               cm_cloudmoon_client_t*client,int width,int height) {
    static const char*ice_servers[]={"stun:stun.ipfire.org:3478","stun:global.stun.twilio.com:3478","stun:stun.l.google.com:19302"};
    rtcConfiguration config;
    cm_cloudmoon_webrtc_t*rtc;
    if(!out||!client||width<=0||height<=0)return -1;
    *out=NULL;
    rtc=(cm_cloudmoon_webrtc_t*)calloc(1,sizeof(*rtc));
    if(!rtc)return -1;
    rtc->pc=-1;rtc->data_channel=-1;rtc->video_track=-1;rtc->audio_track=-1;rtc->client=client;rtc->width=width;rtc->height=height;
    cm_layout_default(&rtc->layout,width,height);cm_input_reset(&rtc->input_state);
    memset(&config,0,sizeof(config));
    config.iceServers=ice_servers;config.iceServersCount=3;
    config.enableIceUdpMux=true;config.disableAutoNegotiation=true;config.forceMediaTransport=true;
    rtc->pc=rtcCreatePeerConnection(&config);
    if(rtc->pc<0)goto fail;
    rtcSetUserPointer(rtc->pc,rtc);
    rtcSetLocalDescriptionCallback(rtc->pc,cm_rtc_local_description);
    rtcSetLocalCandidateCallback(rtc->pc,cm_rtc_local_candidate);
    rtcSetStateChangeCallback(rtc->pc,cm_rtc_state);
    rtcSetDataChannelCallback(rtc->pc,cm_rtc_data_channel);
    rtcSetTrackCallback(rtc->pc,cm_rtc_track);

    /* Match the browser player: recv-only H.264 + Opus transceivers and a
       locally-created ordered "config" data channel before the server offer. */
    {
        rtcTrackInit video_init, audio_init;
        memset(&video_init,0,sizeof(video_init));
        video_init.direction=RTC_DIRECTION_RECVONLY;
        video_init.codec=RTC_CODEC_H264;
        video_init.payloadType=100;
        video_init.mid="0";
        if(rtcAddTrackEx(rtc->pc,&video_init)<0)goto fail;

        memset(&audio_init,0,sizeof(audio_init));
        audio_init.direction=RTC_DIRECTION_RECVONLY;
        audio_init.codec=RTC_CODEC_OPUS;
        audio_init.payloadType=111;
        audio_init.mid="1";
        if(rtcAddTrackEx(rtc->pc,&audio_init)<0)goto fail;
    }
    rtc->data_channel=rtcCreateDataChannel(rtc->pc,"config");
    if(rtc->data_channel<0)goto fail;
    rtcSetUserPointer(rtc->data_channel,rtc);
    rtcSetOpenCallback(rtc->data_channel,cm_rtc_channel_open);
    rtcSetMessageCallback(rtc->data_channel,cm_rtc_channel_message);
    rtcSetClosedCallback(rtc->data_channel,cm_rtc_channel_closed);

    CM_ATOMIC_STORE(&rtc->input_running,1);
    if(sysThreadCreate(&rtc->input_thread,cm_rtc_input_loop,rtc,150,0x40000,
                       THREAD_JOINABLE,"CloudMoonInput")!=0)goto fail;
    rtc->input_started=1;
    *out=rtc;return 0;
fail:
    if(rtc->input_started){u64 thread_result;CM_ATOMIC_STORE(&rtc->input_running,0);sysThreadJoin(rtc->input_thread,&thread_result);}
    if(rtc->pc>=0){rtcClosePeerConnection(rtc->pc);rtcDeletePeerConnection(rtc->pc);}
    free(rtc);return -1;
}

void cm_cloudmoon_webrtc_signal(void*context,const char*json,size_t length) {
    cm_cloudmoon_webrtc_t*rtc=(cm_cloudmoon_webrtc_t*)context;
    char type[32],sdp[CM_RTC_SDP_MAX+256],candidate[2048],candidate_mid[64];
    int label=0;
    if(!rtc||!json||!length||cm_json_get_string(json,length,"type",type,sizeof(type))<0)return;
    if(!strcmp(type,"offer")) {
        if(cm_json_get_string(json,length,"sdp",sdp,sizeof(sdp))<0)return;
        if(cm_rtc_prepare_offer(sdp,sizeof(sdp))<0)return;
        cm_rtc_parse_mids(rtc,sdp);
        if(rtcSetRemoteDescription(rtc->pc,sdp,"offer")!=RTC_ERR_SUCCESS)return;
        if(rtcSetLocalDescription(rtc->pc,"answer")!=RTC_ERR_SUCCESS)return;
        __sync_lock_test_and_set(&rtc->pending_lock,1);
        rtc->remote_description_set=1;
        __sync_lock_release(&rtc->pending_lock);
        for(;;) {
            char queued_candidate[CM_RTC_CANDIDATE_MAX],queued_mid[32];
            __sync_lock_test_and_set(&rtc->pending_lock,1);
            if(rtc->pending_count<=0) {
                __sync_lock_release(&rtc->pending_lock);
                break;
            }
            memcpy(queued_candidate,rtc->pending[0].candidate,sizeof(queued_candidate));
            memcpy(queued_mid,rtc->pending[0].mid,sizeof(queued_mid));
            if(--rtc->pending_count>0)
                memmove(&rtc->pending[0],&rtc->pending[1],
                        (size_t)rtc->pending_count*sizeof(rtc->pending[0]));
            __sync_lock_release(&rtc->pending_lock);
            (void)rtcAddRemoteCandidate(rtc->pc,queued_candidate,queued_mid);
        }
    } else if(!strcmp(type,"candidate")) {
        const char*mid=NULL;
        if(cm_json_get_string(json,length,"candidate",candidate,sizeof(candidate))<0)return;
        if(cm_json_get_integer(json,length,"label",&label)<0)
            (void)cm_json_get_integer(json,length,"sdpMLineIndex",&label);
        if(cm_json_get_string(json,length,"id",candidate_mid,sizeof(candidate_mid))==0)
            mid=candidate_mid;
        else if(cm_json_get_string(json,length,"sdpMid",candidate_mid,sizeof(candidate_mid))==0)
            mid=candidate_mid;
        else if(cm_json_get_string(json,length,"mid",candidate_mid,sizeof(candidate_mid))==0)
            mid=candidate_mid;
        else if(label>=0&&label<CM_RTC_MAX_MIDS&&rtc->remote_mids[label][0])
            mid=rtc->remote_mids[label];
        else mid=rtc->remote_mids[0][0]?rtc->remote_mids[0]:"0";
        __sync_lock_test_and_set(&rtc->pending_lock,1);
        if(!rtc->remote_description_set) {
            if(rtc->pending_count<CM_RTC_PENDING_CANDIDATES) {
                size_t candidate_len=strlen(candidate),mid_len=strlen(mid);
                if(candidate_len<sizeof(rtc->pending[0].candidate)&&
                   mid_len<sizeof(rtc->pending[0].mid)) {
                    memcpy(rtc->pending[rtc->pending_count].candidate,candidate,candidate_len+1);
                    memcpy(rtc->pending[rtc->pending_count].mid,mid,mid_len+1);
                    rtc->pending_count++;
                }
            }
            __sync_lock_release(&rtc->pending_lock);
            return;
        }
        __sync_lock_release(&rtc->pending_lock);
        (void)rtcAddRemoteCandidate(rtc->pc,candidate,mid);
    }
}

int cm_cloudmoon_webrtc_send_input(cm_cloudmoon_webrtc_t*rtc,const char*message,size_t length) {
    if(!rtc||!message||!length||length>0x7fffffffU)return -1;
    { int dc=CM_ATOMIC_LOAD(&rtc->data_channel); if(dc<0||!rtcIsOpen(dc))return -1;
      return rtcSendMessage(dc,message,(int)length); }
}
void cm_cloudmoon_webrtc_detach_signaling(cm_cloudmoon_webrtc_t*rtc){
    if(!rtc)return;
    __sync_lock_test_and_set(&rtc->signaling_detached,1);
    while(__sync_val_compare_and_swap(&rtc->signaling_users,0,0)!=0)usleep(1000);
    __sync_lock_test_and_set(&rtc->client,(cm_cloudmoon_client_t*)0);
}
int cm_cloudmoon_webrtc_is_connected(const cm_cloudmoon_webrtc_t*rtc){return rtc?CM_ATOMIC_LOAD((volatile int*)&rtc->connected):0;}
void cm_cloudmoon_webrtc_close(cm_cloudmoon_webrtc_t*rtc) {
    int pc,dc,video_track,audio_track;
    if(!rtc)return;
    CM_ATOMIC_STORE(&rtc->closing,1);
    CM_ATOMIC_STORE(&rtc->input_running,0);
    if(rtc->input_started){u64 result;sysThreadJoin(rtc->input_thread,&result);rtc->input_started=0;}
    pc=rtc->pc;dc=CM_ATOMIC_LOAD(&rtc->data_channel);
    video_track=rtc->video_track;audio_track=rtc->audio_track;
    rtc->pc=-1;
    CM_ATOMIC_STORE(&rtc->data_channel,-1);
    /* Stop new media callbacks before releasing the buffers they may touch. */
    if(video_track>=0)(void)rtcSetFrameCallback(video_track,NULL);
    if(audio_track>=0)(void)rtcSetFrameCallback(audio_track,NULL);
    if(pc>=0)rtcClosePeerConnection(pc);
    if(dc>=0){rtcClose(dc);rtcDelete(dc);}
    while(CM_ATOMIC_LOAD(&rtc->media_callbacks)>0)usleep(1000);
    if(pc>=0)rtcDeletePeerConnection(pc);
    if(CM_ATOMIC_LOAD(&rtc->video_started)==2)ps3video_cloudmoon_stop();
    if(CM_ATOMIC_LOAD(&rtc->audio_started)==2)ps3audio_cloudmoon_stop();
    free(rtc);
}
