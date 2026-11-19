#include "cloudmoon_random_mbedtls.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

int timingsafe_bcmp(const void *left, const void *right, size_t length) {
    const volatile uint8_t *a = (const volatile uint8_t *)left;
    const volatile uint8_t *b = (const volatile uint8_t *)right;
    uint8_t difference = 0;
    size_t i;

    if ((!a || !b) && length != 0)
        return 1;
    for (i = 0; i < length; ++i)
        difference |= (uint8_t)(a[i] ^ b[i]);
    return difference != 0;
}

void arc4random_buf(void *buffer, size_t length) {
    if ((!buffer && length != 0) ||
        cm_orbis_random_source(NULL, (uint8_t *)buffer, length) != 0) {
        /* Fail closed: never supply predictable bytes to SCTP/DTLS. */
        abort();
    }
}
