#ifndef CLOUDMOON_RANDOM_H
#define CLOUDMOON_RANDOM_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef int (*cm_random_source_fn)(void *context,uint8_t *output,size_t length);
int cm_random_bytes(cm_random_source_fn source,void *context,uint8_t *output,size_t length);
#ifdef __cplusplus
}
#endif
#endif
