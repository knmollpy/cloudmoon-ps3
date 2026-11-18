#include "qrcodegen.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    static const char payload[] = "http://192.168.1.42:18765/?k=0123456789abcdef0123456789abcdef";
    uint8_t temp[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
    uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
    int size;
    if (!qrcodegen_encodeText(payload, temp, qr, qrcodegen_Ecc_MEDIUM,
                              1, 10, qrcodegen_Mask_AUTO, true)) {
        fputs("QR encoding failed.\n", stderr);
        return 1;
    }
    size = qrcodegen_getSize(qr);
    if (size < 21 || size > 57 || (size - 17) % 4 != 0) {
        fprintf(stderr, "Unexpected QR size: %d\n", size);
        return 1;
    }
    if (!qrcodegen_getModule(qr, 0, 0) ||
        !qrcodegen_getModule(qr, 6, 0) ||
        !qrcodegen_getModule(qr, 0, 6) ||
        !qrcodegen_getModule(qr, 6, 6) ||
        !qrcodegen_getModule(qr, 3, 3)) {
        fputs("QR finder pattern validation failed.\n", stderr);
        return 1;
    }
    puts("CloudMoon LAN pairing QR tests passed.");
    return 0;
}
