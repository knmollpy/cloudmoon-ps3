#include <ifaddrs.h>
#include <net/if.h>
int getifaddrs(struct ifaddrs **ifap){if(!ifap)return -1;*ifap=0;return 0;}
void freeifaddrs(struct ifaddrs *ifa){(void)ifa;}
unsigned int if_nametoindex(const char *ifname){(void)ifname;return 1;}
