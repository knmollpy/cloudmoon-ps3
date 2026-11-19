#ifndef CLOUDMOON_PAIRING_H
#define CLOUDMOON_PAIRING_H

#include <stddef.h>

#include "cloudmoon_session_link.h"
#include "../qr/cloudmoon_qr.h"

typedef struct cm_pairing_service cm_pairing_service_t;

int cm_pairing_start(cm_pairing_service_t **out,
                     const char *ca_bundle_path,
                     unsigned timeout_seconds);
void cm_pairing_stop(cm_pairing_service_t *service);
int cm_pairing_take_account(cm_pairing_service_t *service,
                            cm_account_session_t *account);
const char *cm_pairing_status(const cm_pairing_service_t *service);
const char *cm_pairing_url(const cm_pairing_service_t *service);
const cm_qr_t *cm_pairing_qr(const cm_pairing_service_t *service);

#endif
