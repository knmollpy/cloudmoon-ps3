#include "cloudmoon_ps3_random.h"
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/platform_time.h>
#include <sys/systime.h>
#include <stddef.h>
#include <stdint.h>
int mbedtls_hardware_poll(void *data,unsigned char *output,size_t len,size_t *olen) {
    (void)data;
    if(!output||!olen)return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    if(cm_ps3_random_source(NULL,(uint8_t*)output,len)!=0)return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    *olen=len;return 0;
}
mbedtls_ms_time_t mbedtls_ms_time(void) {
    u64 seconds=0,nanoseconds=0;
    if(sysGetCurrentTime(&seconds,&nanoseconds)!=0)return 0;
    return (mbedtls_ms_time_t)(seconds*1000ULL+nanoseconds/1000000ULL);
}
