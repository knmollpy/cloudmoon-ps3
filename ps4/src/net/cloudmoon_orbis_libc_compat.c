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

/*
 * OpenOrbis libc++ expects the FreeBSD __cxa_thread_atexit_impl hook.
 * Provide the same per-thread destructor semantics using POSIX pthread keys.
 * A destructor registered by another destructor is picked up by the next
 * POSIX key-destructor iteration, as required for thread-local teardown.
 */
#include <pthread.h>

typedef struct cm_thread_dtor_entry {
    void (*destructor)(void *);
    void *object;
    struct cm_thread_dtor_entry *next;
} cm_thread_dtor_entry_t;

static pthread_key_t s_thread_dtor_key;
static pthread_once_t s_thread_dtor_once = PTHREAD_ONCE_INIT;
static int s_thread_dtor_key_status = -1;

static void cm_run_thread_destructors(void *value) {
    cm_thread_dtor_entry_t *entry = (cm_thread_dtor_entry_t *)value;
    while (entry) {
        cm_thread_dtor_entry_t *next = entry->next;
        if (entry->destructor)
            entry->destructor(entry->object);
        free(entry);
        entry = next;
    }
}

static void cm_create_thread_dtor_key(void) {
    s_thread_dtor_key_status =
        pthread_key_create(&s_thread_dtor_key, cm_run_thread_destructors);
}

/* dso_handle is unused: CloudMoon is a single executable and does not unload
 * C++ shared objects. This implementation still runs each destructor when
 * its owning pthread exits rather than postponing cleanup until app shutdown.
 */
int __cxa_thread_atexit_impl(void (*destructor)(void *),
                             void *object,
                             void *dso_handle) {
    cm_thread_dtor_entry_t *entry;
    cm_thread_dtor_entry_t *head;
    (void)dso_handle;

    if (!destructor)
        return 0;
    if (pthread_once(&s_thread_dtor_once, cm_create_thread_dtor_key) != 0 ||
        s_thread_dtor_key_status != 0)
        return -1;

    entry = (cm_thread_dtor_entry_t *)malloc(sizeof(*entry));
    if (!entry)
        return -1;
    entry->destructor = destructor;
    entry->object = object;
    entry->next = NULL;

    head = (cm_thread_dtor_entry_t *)pthread_getspecific(s_thread_dtor_key);
    entry->next = head;
    if (pthread_setspecific(s_thread_dtor_key, entry) != 0) {
        free(entry);
        return -1;
    }
    return 0;
}
