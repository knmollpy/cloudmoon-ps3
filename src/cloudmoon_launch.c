#include "cloudmoon_launch.h"
#include "cloudmoon_client.h"
#include "cloudmoon_discovery.h"
#include "cloudmoon_ps3_random.h"
#include "cloudmoon_webrtc.h"
#include "ui.h"
#include "net_logger.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#define CM_CONFIG_PATH "/dev_hdd0/game/MNLT00001/USRDIR/cloudmoon.ini"
#define CM_CA_PATH "/dev_hdd0/game/MNLT00001/USRDIR/cacert.pem"
typedef struct { cm_cloudmoon_webrtc_t *rtc; } cm_launch_rtc_context;
static void cm_launch_signal(void *context,cm_cloudmoon_client_t *client,const char *json,size_t length){
 cm_launch_rtc_context *state=(cm_launch_rtc_context*)context;
 if(!state||!client||!json||!length)return;
 if(!state->rtc){if(cm_cloudmoon_webrtc_create(&state->rtc,client,1280,720)!=0){ui_push_log("CloudMoon: WebRTC peer initialization failed.");return;}}
 cm_cloudmoon_webrtc_signal(state->rtc,json,length);
}
static void cm_trim(char*s){size_t n;while(*s&&isspace((unsigned char)*s))memmove(s,s+1,strlen(s));n=strlen(s);while(n&&isspace((unsigned char)s[n-1]))s[--n]=0;}
static int cm_read_config(char*game,size_t gc,char*user,size_t uc,char*instance,size_t ic){
 FILE*f=fopen(CM_CONFIG_PATH,"r");char line[4096];
 if(!f){f=fopen(CM_CONFIG_PATH,"w");if(f){fputs("# Copy these values from your active CloudMoon browser session URL.\n",f);fputs("game=\nuser_id=\nandroid_instance_id=\n",f);fclose(f);}return 0;}
 while(fgets(line,sizeof(line),f)){char*eq;cm_trim(line);if(!line[0]||line[0]=='#'||line[0]==';')continue;eq=strchr(line,'=');if(!eq)continue;*eq++=0;cm_trim(line);cm_trim(eq);
  if(!strcmp(line,"game")){if(strlen(eq)>=gc){fclose(f);return -1;}strcpy(game,eq);}
  else if(!strcmp(line,"user_id")){if(strlen(eq)>=uc){fclose(f);return -1;}strcpy(user,eq);}
  else if(!strcmp(line,"android_instance_id")){if(strlen(eq)>=ic){fclose(f);return -1;}strcpy(instance,eq);}
 }
 fclose(f);return game[0]&&user[0]&&instance[0]?1:-1;
}
int cm_cloudmoon_autostart(void){
 char game[192]={0},user[192]={0},instance[3072]={0},normalized[3072]={0};cm_cloudmoon_registration_t registration;cm_launch_rtc_context rtc_state={0};
 int rc=cm_read_config(game,sizeof(game),user,sizeof(user),instance,sizeof(instance));
 if(rc==0){ui_push_log("CloudMoon: edit USRDIR/cloudmoon.ini and restart.");return 0;}
 if(rc<0){ui_push_log("CloudMoon: cloudmoon.ini is incomplete or invalid.");return -1;}
 if(cm_cloudmoon_decode_instance_id(instance,normalized,sizeof(normalized))<0){ui_push_log("CloudMoon: invalid android_instance_id encoding.");return -1;}
 registration.width=1280;registration.height=720;registration.game=game;registration.user_id=user;registration.instance_id=normalized;
 ui_push_log("CloudMoon: connecting directly to coordinator...");NLOG("CloudMoon: coordinator session starting for package %s",game);
 rc=cm_cloudmoon_client_run_from_instance_id(normalized,CM_CA_PATH,20,&registration,cm_ps3_random_source,NULL,cm_launch_signal,&rtc_state);
 if(rtc_state.rtc){cm_cloudmoon_webrtc_close(rtc_state.rtc);rtc_state.rtc=NULL;}
 NLOG("CloudMoon: session ended with status %d",rc);
 if(rc!=0)ui_push_log("CloudMoon: connection failed. Check session data and CA bundle.");else ui_push_log("CloudMoon: stream disconnected.");
 return rc;
}
