#ifndef CLOUDMOON_ORBIS_INPUT_H
#define CLOUDMOON_ORBIS_INPUT_H

#include "../ui/cloudmoon_ui.h"

int cm_orbis_input_init(void);
void cm_orbis_input_shutdown(void);
/* Returns 0 on success and writes only newly pressed/repeated UI keys. */
int cm_orbis_input_poll(unsigned int *pressed);

#endif
