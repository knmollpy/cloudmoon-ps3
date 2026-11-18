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
    {
        int x, y, dark = 0;
        for (y = 0; y < size; ++y)
            for (x = 0; x < size; ++x)
                dark += qrcodegen_getModule(qr, x, y) ? 1 : 0;
        if (dark < (size * size) / 4 || dark > (size * size * 3) / 4) {
            fprintf(stderr, "Unexpected QR module density: %d/%d\\n", dark, size * size);
            return 1;
        }
    }
    puts("CloudMoon LAN pairing QR tests passed.");
    return 0;
}
