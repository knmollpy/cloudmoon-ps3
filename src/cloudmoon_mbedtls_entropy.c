#include "cloudmoon_ps3_random.h"
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <stddef.h>
#include <stdint.h>

/* Mbed TLS entropy hook for the PS3: use the console's hardware RNG only. */
int mbedtls_hardware_poll(void *data,unsigned char *output,size_t len,size_t *olen) {
    (void)data;
    if(!output||!olen)return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    if(cm_ps3_random_source(NULL,(uint8_t*)output,len)!=0)return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    *olen=len;
    return 0;
}
