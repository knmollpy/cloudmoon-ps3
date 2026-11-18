#ifndef CLOUDMOON_RANDOM_MBEDTLS_H
#define CLOUDMOON_RANDOM_MBEDTLS_H

#include <stddef.h>
#include <stdint.h>

/* Mbed TLS CTR-DRBG backed by the platform entropy source. */
int cm_orbis_random_init(void);
int cm_orbis_random_source(void *context, uint8_t *output, size_t length);
void cm_orbis_random_shutdown(void);

#endif
