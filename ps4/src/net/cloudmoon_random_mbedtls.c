#include "cloudmoon_random_mbedtls.h"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>

#include <pthread.h>
#include <string.h>

static mbedtls_entropy_context s_entropy;
static mbedtls_ctr_drbg_context s_drbg;
static pthread_once_t s_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static int s_init_result = -1;
static int s_initialized;

static void cm_orbis_random_once(void) {
    static const unsigned char personalization[] = "CloudMoon-PS4-WebSocket";
    int rc;

    mbedtls_entropy_init(&s_entropy);
    mbedtls_ctr_drbg_init(&s_drbg);

    rc = mbedtls_ctr_drbg_seed(&s_drbg, mbedtls_entropy_func, &s_entropy,
                               personalization,
                               sizeof(personalization) - 1u);
    if (rc == 0) {
        s_initialized = 1;
        s_init_result = 0;
    } else {
        mbedtls_ctr_drbg_free(&s_drbg);
        mbedtls_entropy_free(&s_entropy);
        s_init_result = rc;
    }
}

int cm_orbis_random_init(void) {
    if (pthread_once(&s_once, cm_orbis_random_once) != 0)
        return -1;
    return s_init_result;
}

int cm_orbis_random_source(void *context, uint8_t *output, size_t length) {
    int rc;
    (void)context;

    if ((!output && length != 0) || cm_orbis_random_init() != 0)
        return -1;
    if (length == 0)
        return 0;

    if (pthread_mutex_lock(&s_lock) != 0)
        return -1;
    rc = mbedtls_ctr_drbg_random(&s_drbg, output, length);
    (void)pthread_mutex_unlock(&s_lock);
    return rc;
}

void cm_orbis_random_shutdown(void) {
    if (!s_initialized)
        return;
    if (pthread_mutex_lock(&s_lock) != 0)
        return;
    mbedtls_ctr_drbg_free(&s_drbg);
    mbedtls_entropy_free(&s_entropy);
    memset(&s_drbg, 0, sizeof(s_drbg));
    memset(&s_entropy, 0, sizeof(s_entropy));
    s_initialized = 0;
    s_init_result = -1;
    (void)pthread_mutex_unlock(&s_lock);
}
