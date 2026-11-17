#include "cloudmoon_ps3_random.h"
#include <lv2/system.h>
#define CM_PS3_RANDOM_MAX_CHUNK 64U
int cm_ps3_random_source(void*context,uint8_t*output,size_t length){size_t off=0;(void)context;if(!output&&length)return -1;while(off<length){size_t n=length-off;if(n>CM_PS3_RANDOM_MAX_CHUNK)n=CM_PS3_RANDOM_MAX_CHUNK;if(sysGetRandomNumber(output+off,(u32)n)!=0)return -1;off+=n;}return 0;}
