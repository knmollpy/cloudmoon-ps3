#ifndef CLOUDMOON_ORBIS_INPUT_H
#define CLOUDMOON_ORBIS_INPUT_H

#include "../ui/cloudmoon_ui.h"

typedef struct {
    int16_t lx, ly, rx, ry;
    uint8_t cross, circle, square, triangle;
    uint8_t l1, r1, l2, r2;
    uint8_t up, down, left, right;
    uint8_t options, touchpad, connected;
} cm_orbis_gamepad_state_t;

int cm_orbis_input_init(void);
void cm_orbis_input_shutdown(void);
/* Returns 0 on success and writes only newly pressed/repeated UI keys. */
int cm_orbis_input_poll(unsigned int *pressed);
int cm_orbis_gamepad_read(cm_orbis_gamepad_state_t *state);

#endif
