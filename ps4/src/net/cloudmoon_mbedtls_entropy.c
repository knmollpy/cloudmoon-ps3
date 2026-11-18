#include "cloudmoon_mbedtls_entropy.h"

#include <mbedtls/error.h>

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

int cm_orbis_entropy_poll(void *context, unsigned char *output,
                          size_t length, size_t *output_length) {
    int fd;
    size_t offset = 0;
    (void)context;

    if (!output_length || (!output && length != 0))
        return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    *output_length = 0;
    if (length == 0)
        return 0;

    fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0)
        return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;

    while (offset < length) {
        ssize_t n = read(fd, output + offset, length - offset);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            (void)close(fd);
            return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
        }
        offset += (size_t)n;
    }

    (void)close(fd);
    *output_length = offset;
    return 0;
}

int cm_orbis_entropy_add(mbedtls_entropy_context *entropy) {
    if (!entropy)
        return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    return mbedtls_entropy_add_source(entropy, cm_orbis_entropy_poll, NULL,
                                      32, MBEDTLS_ENTROPY_SOURCE_STRONG);
}
