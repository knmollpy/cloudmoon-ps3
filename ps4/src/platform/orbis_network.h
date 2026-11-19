#ifndef CLOUDMOON_ORBIS_NETWORK_H
#define CLOUDMOON_ORBIS_NETWORK_H

#include <stddef.h>

int cm_orbis_network_init(void);
void cm_orbis_network_shutdown(void);
int cm_orbis_network_ipv4(char *out, size_t capacity);

#endif
