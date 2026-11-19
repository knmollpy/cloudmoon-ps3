#ifndef CLOUDMOON_QR_H
#define CLOUDMOON_QR_H

#include <stdint.h>

#define CM_QR_MAX_SIZE 177

typedef struct {
    int size;
    uint8_t modules[CM_QR_MAX_SIZE * CM_QR_MAX_SIZE];
} cm_qr_t;

/* Build a standards-compliant QR matrix from UTF-8 text. */
int cm_qr_encode(const char *text, cm_qr_t *out);

#endif
