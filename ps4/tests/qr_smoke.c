#include "../src/qr/cloudmoon_qr.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    cm_qr_t qr;
    int dark = 0;
    int light = 0;
    int i;

    assert(cm_qr_encode("http://192.168.1.25:8787/pair/0123456789ABCDEF", &qr) >= 21);
    assert(qr.size >= 21 && qr.size <= CM_QR_MAX_SIZE);
    for (i = 0; i < qr.size * qr.size; ++i) {
        if (qr.modules[i]) ++dark;
        else ++light;
    }
    assert(dark > 0 && light > 0);
    assert(cm_qr_encode("", &qr) < 0);
    puts("CloudMoon QR smoke tests passed");
    return 0;
}
