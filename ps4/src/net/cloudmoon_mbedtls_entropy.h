#ifndef CLOUDMOON_MBEDTLS_ENTROPY_H
#define CLOUDMOON_MBEDTLS_ENTROPY_H

#include <mbedtls/entropy.h>

/* Strong entropy obtained from the Orbis system random device. */
int cm_orbis_entropy_poll(void *context, unsigned char *output,
                          size_t length, size_t *output_length);
int cm_orbis_entropy_add(mbedtls_entropy_context *entropy);

#endif
