#ifndef CLOUDMOON_DISCOVERY_H
#define CLOUDMOON_DISCOVERY_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int cm_base64_decode(const char *input,uint8_t *output,size_t output_capacity,size_t *output_length);
int cm_cloudmoon_parse_https_url(const char *url,char *hostname,size_t hostname_capacity,uint16_t *port,char *path,size_t path_capacity);
int cm_cloudmoon_get_coordinator(const char *android_instance_id,char *hostname,size_t hostname_capacity,uint16_t *port,char *path,size_t path_capacity);
#ifdef __cplusplus
}
#endif
#endif
