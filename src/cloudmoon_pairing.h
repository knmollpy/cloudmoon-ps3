#ifndef CLOUDMOON_PAIRING_H
#define CLOUDMOON_PAIRING_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CM_PAIR_QR_MAX_SIZE 57
#define CM_PAIR_QR_MAX_MODULES (CM_PAIR_QR_MAX_SIZE * CM_PAIR_QR_MAX_SIZE)
#define CM_PAIR_URL_CAP 160

/* Starts a one-time, LAN-only pairing page and returns its URL plus QR matrix. */
int cm_cloudmoon_pair_start(const char *selected_game, char *url, size_t url_capacity,
                            uint8_t *modules, size_t modules_capacity,
                            int *module_size);
void cm_cloudmoon_pair_stop(void);
int cm_cloudmoon_pair_is_running(void);

#ifdef __cplusplus
}
#endif

#endif
