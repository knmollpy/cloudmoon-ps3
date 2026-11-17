#include "cloudmoon_random.h"
int cm_random_bytes(cm_random_source_fn source,void*context,uint8_t*output,size_t length){if(!source||(!output&&length))return -1;if(!length)return 0;return source(context,output,length)==0?0:-1;}
