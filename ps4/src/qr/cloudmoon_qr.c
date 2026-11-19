#include "cloudmoon_qr.h"

#include "qrcodegen.h"

#include <string.h>

int cm_qr_encode(const char *text, cm_qr_t *out) {
    uint8_t temporary[qrcodegen_BUFFER_LEN_MAX];
    uint8_t symbol[qrcodegen_BUFFER_LEN_MAX];
    int size, x, y;

    if (!text || !text[0] || !out)
        return -1;

    memset(out, 0, sizeof(*out));
    if (!qrcodegen_encodeText(text, temporary, symbol,
                              qrcodegen_Ecc_MEDIUM,
                              qrcodegen_VERSION_MIN,
                              qrcodegen_VERSION_MAX,
                              qrcodegen_Mask_AUTO, true))
        return -1;

    size = qrcodegen_getSize(symbol);
    if (size < 21 || size > CM_QR_MAX_SIZE)
        return -1;

    out->size = size;
    for (y = 0; y < size; ++y)
        for (x = 0; x < size; ++x)
            out->modules[(size_t)y * (size_t)size + (size_t)x] =
                (uint8_t)qrcodegen_getModule(symbol, x, y);

    return size;
}
