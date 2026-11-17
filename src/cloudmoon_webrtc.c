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

struct cm_cloudmoon_webrtc {
    int pc,data_channel,video_started,audio_started,connected;
    int width,height,input_running,input_started;
    sys_ppu_thread_t input_thread;
    cm_layout_t layout;
    cm_input_state_t input_state;
    cm_cloudmoon_client_t *client;
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
    char qvalue[CM_RTC_SDP_MAX+256],qmid[128],json[CM_RTC_SDP_MAX+512];
    int n;
    if(!rtc||!rtc->client||!type||!value||cm_json_quote(qvalue,sizeof(qvalue),value)<0)return -1;
    if(mid) {
        if(cm_json_quote(qmid,sizeof(qmid),mid)<0)return -1;
        n=snprintf(json,sizeof(json),"{\"type\":\"%s\",\"candidate\":%s,\"mid\":%s,\"label\":%d,\"sdpMid\":%s,\"sdpMLineIndex\":%d}",type,qvalue,qmid,label,qmid,label);
    } else n=snprintf(json,sizeof(json),"{\"type\":\"%s\",\"sdp\":%s}",type,qvalue);
    if(n<0||(size_t)n>=sizeof(json))return -1;
    return cm_cloudmoon_client_send_signal(rtc->client,json,(size_t)n);
}

static void cm_rtc_local_description(int pc,const char*sdp,const char*type,void*ptr) {
    cm_cloudmoon_webrtc_t*rtc=(cm_cloudmoon_webrtc_t*)ptr;
    if(rtc&&pc==rtc->pc&&sdp&&type)(void)cm_rtc_send_signal(rtc,type,sdp,NULL,0);
}

static void cm_rtc_local_candidate(int pc,const char*candidate,const char*mid,void*ptr) {
    cm_cloudmoon_webrtc_t*rtc=(cm_cloudmoon_webrtc_t*)ptr;
    int label=0;
    if(!rtc||pc!=rtc->pc||!candidate)return;
    if(mid&&mid[0]>='0'&&mid[0]<='9')label=atoi(mid);
    (void)cm_rtc_send_signal(rtc,"candidate",candidate,mid?mid:"0",label);
}

static void cm_rtc_state(int pc,rtcState state,void*ptr) {
    cm_cloudmoon_webrtc_t*rtc=(cm_cloudmoon_webrtc_t*)ptr;
    if(rtc&&pc==rtc->pc){rtc->connected=(state==RTC_CONNECTED);if(state==RTC_CONNECTED)ui_set_state(UI_STATE_STREAMING);else if(state==RTC_DISCONNECTED||state==RTC_FAILED||state==RTC_CLOSED)ui_set_state(UI_STATE_ERROR);}
}

static void cm_rtc_channel_message(int id,const char*message,int size,void*ptr) {
    (void)id;(void)message;(void)size;(void)ptr;
}

static void cm_rtc_channel_open(int id,void*ptr) {
    cm_cloudmoon_webrtc_t*rtc=(cm_cloudmoon_webrtc_t*)ptr;
    char initial[64];int n;
    if(!rtc)return;
    rtc->data_channel=id;
    n=snprintf(initial,sizeof(initial),"res>%dx%d",rtc->width,rtc->height);
    if(n>0&&(size_t)n<sizeof(initial))(void)rtcSendMessage(id,initial,n);
    (void)rtcSendMessage(id,"adBlock>true",12);
}

static void cm_rtc_data_channel(int pc,int dc,void*ptr) {
    cm_cloudmoon_webrtc_t*rtc=(cm_cloudmoon_webrtc_t*)ptr;
    (void)pc;
    if(!rtc||dc<0)return;
    rtc->data_channel=dc;
    rtcSetUserPointer(dc,rtc);
    rtcSetOpenCallback(dc,cm_rtc_channel_open);
    rtcSetMessageCallback(dc,cm_rtc_channel_message);
}

static void cm_rtc_video_frame(int track,const char*data,int size,
                               const rtcFrameInfo*info,void*ptr) {
    cm_cloudmoon_webrtc_t*rtc=(cm_cloudmoon_webrtc_t*)ptr;
    (void)track;
    if(!rtc||!data||size<=0||!info)return;
    if(!rtc->video_started) {
        if(ps3video_cloudmoon_start(rtc->width,rtc->height)!=0)return;
        rtc->video_started=1;
    }
    (void)ps3video_submit_h264((const uint8_t*)data,(size_t)size,info->timestamp);
}

static void cm_rtc_audio_frame(int track,const char*data,int size,
                               const rtcFrameInfo*info,void*ptr) {
    cm_cloudmoon_webrtc_t*rtc=(cm_cloudmoon_webrtc_t*)ptr;
    (void)track;(void)info;
    if(!rtc||!data||size<=0)return;
    if(!rtc->audio_started) {
        if(ps3audio_cloudmoon_start()!=0)return;
        rtc->audio_started=1;
    }
    ps3audio_submit_opus((const uint8_t*)data,(size_t)size);
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
        (void)rtcSetH264Depacketizer(track,RTC_NAL_SEPARATOR_START_SEQUENCE);
        (void)rtcSetFrameCallback(track,cm_rtc_video_frame);
    } else if(strstr(description,"m=audio")&&strstr(description,"opus")) {
        (void)rtcSetOpusDepacketizer(track);
        (void)rtcSetFrameCallback(track,cm_rtc_audio_frame);
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

static void cm_rtc_input_loop(void*arg) {
    cm_cloudmoon_webrtc_t*rtc=(cm_cloudmoon_webrtc_t*)arg;
    char messages[512];
    cm_pad_t pad;
    ps3_pad_state_t ps3;
    while(rtc&&rtc->input_running) {
        ps3input_get_data(&ps3);
        memset(&pad,0,sizeof(pad));
        pad.lx=ps3.lx;pad.ly=ps3.ly;pad.rx=ps3.rx;pad.ry=ps3.ry;
        pad.cross=!!(ps3.buttons_down&A_FLAG);
        pad.circle=!!(ps3.buttons_down&B_FLAG);
        pad.square=!!(ps3.buttons_down&Y_FLAG);
        pad.triangle=!!(ps3.buttons_down&X_FLAG);
        pad.l1=!!(ps3.buttons_down&LB_FLAG);pad.r1=!!(ps3.buttons_down&RB_FLAG);\n        pad.l2=ps3.l2;pad.r2=ps3.r2;\n        pad.up=!!(ps3.buttons_down&UP_FLAG);pad.down=!!(ps3.buttons_down&DOWN_FLAG);\n        pad.left=!!(ps3.buttons_down&LEFT_FLAG);pad.right=!!(ps3.buttons_down&RIGHT_FLAG);
        pad.start=!!(ps3.buttons_down&PLAY_FLAG);pad.select=!!(ps3.buttons_down&BACK_FLAG);
        if(rtc->data_channel>=0&&rtcIsOpen(rtc->data_channel)) {
            int n=cm_input_translate(&rtc->layout,&rtc->input_state,&pad,messages,sizeof(messages));
            size_t off=0;
            while(n>0&&off<(size_t)n) {
                size_t z=strlen(messages+off);
                if(z==0)break;
                (void)rtcSendMessage(rtc->data_channel,messages+off,(int)z);
                off+=z+1;
            }
        } else {
            cm_input_reset(&rtc->input_state);
        }
        usleep(16000);
    }
    sysThreadExit(0);
}

int cm_cloudmoon_webrtc_create(cm_cloudmoon_webrtc_t**out,
                               cm_cloudmoon_client_t*client,int width,int height) {
    static const char*ice_servers[]={"stun:stun.nenly.cn:53006","stun:stun.l.google.com:19302"};
    rtcConfiguration config;
    cm_cloudmoon_webrtc_t*rtc;
    int dc;
    if(!out||!client||width<=0||height<=0)return -1;
    *out=NULL;
    rtc=(cm_cloudmoon_webrtc_t*)calloc(1,sizeof(*rtc));
    if(!rtc)return -1;
    rtc->pc=-1;rtc->data_channel=-1;rtc->client=client;rtc->width=width;rtc->height=height;
    cm_layout_default(&rtc->layout,width,height);cm_input_reset(&rtc->input_state);
    memset(&config,0,sizeof(config));
    config.iceServers=ice_servers;config.iceServersCount=2;
    config.enableIceUdpMux=true;config.disableAutoNegotiation=true;config.forceMediaTransport=true;
    rtc->pc=rtcCreatePeerConnection(&config);
    if(rtc->pc<0)goto fail;
    rtcSetUserPointer(rtc->pc,rtc);
    rtcSetLocalDescriptionCallback(rtc->pc,cm_rtc_local_description);
    rtcSetLocalCandidateCallback(rtc->pc,cm_rtc_local_candidate);
    rtcSetStateChangeCallback(rtc->pc,cm_rtc_state);
    rtcSetDataChannelCallback(rtc->pc,cm_rtc_data_channel);
    rtcSetTrackCallback(rtc->pc,cm_rtc_track);
    dc=rtcCreateDataChannel(rtc->pc,"config");
    if(dc<0)goto fail;
    rtc->data_channel=dc;rtcSetUserPointer(dc,rtc);
    rtcSetOpenCallback(dc,cm_rtc_channel_open);rtcSetMessageCallback(dc,cm_rtc_channel_message);
    rtc->input_running=1;
    if(sysThreadCreate(&rtc->input_thread,cm_rtc_input_loop,rtc,150,0x4000,
                       THREAD_JOINABLE,"CloudMoonInput")!=0)goto fail;
    rtc->input_started=1;
    *out=rtc;return 0;
fail:
    if(rtc->input_started){u64 thread_result;rtc->input_running=0;sysThreadJoin(rtc->input_thread,&thread_result);}
    if(rtc->pc>=0){rtcClosePeerConnection(rtc->pc);rtcDeletePeerConnection(rtc->pc);}
    free(rtc);return -1;
}

void cm_cloudmoon_webrtc_signal(void*context,const char*json,size_t length) {
    cm_cloudmoon_webrtc_t*rtc=(cm_cloudmoon_webrtc_t*)context;
    char type[32],sdp[CM_RTC_SDP_MAX],candidate[2048],candidate_mid[64];
    int label=0;
    if(!rtc||!json||!length||cm_json_get_string(json,length,"type",type,sizeof(type))<0)return;
    if(!strcmp(type,"offer")) {
        if(cm_json_get_string(json,length,"sdp",sdp,sizeof(sdp))<0)return;
        cm_rtc_parse_mids(rtc,sdp);
        if(rtcSetRemoteDescription(rtc->pc,sdp,"offer")!=RTC_ERR_SUCCESS)return;
        (void)rtcSetLocalDescription(rtc->pc,"answer");
    } else if(!strcmp(type,"candidate")) {
        const char*mid=NULL;
        if(cm_json_get_string(json,length,"candidate",candidate,sizeof(candidate))<0)return;
        if(cm_json_get_integer(json,length,"label",&label)<0)
            (void)cm_json_get_integer(json,length,"sdpMLineIndex",&label);
        if(cm_json_get_string(json,length,"sdpMid",candidate_mid,sizeof(candidate_mid))==0)
            mid=candidate_mid;
        else if(cm_json_get_string(json,length,"mid",candidate_mid,sizeof(candidate_mid))==0)
            mid=candidate_mid;
        else if(label>=0&&label<CM_RTC_MAX_MIDS&&rtc->remote_mids[label][0])
            mid=rtc->remote_mids[label];
        else mid=rtc->remote_mids[0][0]?rtc->remote_mids[0]:"0";
        (void)rtcAddRemoteCandidate(rtc->pc,candidate,mid);
    }
}

int cm_cloudmoon_webrtc_send_input(cm_cloudmoon_webrtc_t*rtc,const char*message,size_t length) {
    if(!rtc||rtc->data_channel<0||!message||!length||length>0x7fffffffU||!rtcIsOpen(rtc->data_channel))return -1;
    return rtcSendMessage(rtc->data_channel,message,(int)length);
}
int cm_cloudmoon_webrtc_is_connected(const cm_cloudmoon_webrtc_t*rtc){return rtc?rtc->connected:0;}
void cm_cloudmoon_webrtc_close(cm_cloudmoon_webrtc_t*rtc) {
    if(!rtc)return;
    rtc->input_running=0;
    if(rtc->input_started){u64 result;sysThreadJoin(rtc->input_thread,&result);rtc->input_started=0;}
    if(rtc->video_started)ps3video_cloudmoon_stop();
    if(rtc->audio_started)ps3audio_cloudmoon_stop();
    if(rtc->data_channel>=0){rtcClose(rtc->data_channel);rtcDeleteDataChannel(rtc->data_channel);}
    if(rtc->pc>=0){rtcClosePeerConnection(rtc->pc);rtcDeletePeerConnection(rtc->pc);}
    free(rtc);
}
