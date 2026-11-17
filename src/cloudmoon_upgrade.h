#ifndef CLOUDMOON_UPGRADE_H
#define CLOUDMOON_UPGRADE_H
#include <stddef.h>
#include <stdint.h>
#include "cloudmoon_random.h"
#ifdef __cplusplus
extern "C" {
#endif
int cm_ws_generate_client_key(cm_random_source_fn random_source,void *random_context,char client_key_out[25]);
int cm_ws_build_upgrade_request(char*out,size_t out_cap,const char*host,const char*path,const uint8_t nonce16[16],char client_key_out[25]);
int cm_ws_validate_upgrade_response(const char*response,size_t response_len,const char client_key[25]);
#ifdef __cplusplus
}
#endif
#endif
