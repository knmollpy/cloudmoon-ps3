#include "orbis_network.h"

#include <orbis/Net.h>
#include <orbis/NetCtl.h>
#include <orbis/Sysmodule.h>

#include <stdio.h>
#include <string.h>

#define CM_NET_POOL_SIZE (2 * 1024 * 1024)

static int s_net_started;
static int s_netctl_started;
static int s_net_pool = -1;

int cm_orbis_network_init(void) {
    int rc;

    if (s_net_started)
        return 0;

    rc = sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET);
    if (rc < 0 && rc != (int)0x809A0000)
        return -1;

    rc = sceNetInit();
    if (rc < 0 && (uint32_t)rc != 0x80410101u)
        return -2;

    s_net_started = 1;

    s_net_pool = sceNetPoolCreate("cloudmoon-ps4", CM_NET_POOL_SIZE, 0);
    if (s_net_pool < 0)
        return -3;

    rc = sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NETCTL);
    if (rc < 0)
        return -4;
    rc = sceNetCtlInit();
    if (rc < 0 && (uint32_t)rc != 0x80410101u)
        return -5;

    s_netctl_started = 1;
    return 0;
}

int cm_orbis_network_ipv4(char *out, size_t capacity) {
    OrbisNetCtlInfo info;
    int rc;
    size_t n;

    if (!out || capacity < 8 || !s_net_started)
        return -1;
    out[0] = 0;
    memset(&info, 0, sizeof(info));

    rc = sceNetCtlGetInfo(ORBIS_NET_CTL_INFO_IP_ADDRESS, &info);
    if (rc < 0 || !info.ip_address[0])
        return -1;

    n = strlen(info.ip_address);
    if (n + 1 > capacity)
        return -1;
    memcpy(out, info.ip_address, n + 1);
    return 0;
}

void cm_orbis_network_shutdown(void) {
    if (s_netctl_started) {
        sceNetCtlTerm();
        s_netctl_started = 0;
    }
    if (s_net_pool >= 0) {
        sceNetPoolDestroy(s_net_pool);
        s_net_pool = -1;
    }
    if (s_net_started) {
        sceNetTerm();
        s_net_started = 0;
    }
}
