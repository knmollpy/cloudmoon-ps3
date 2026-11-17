#include <ifaddrs.h>
#include <net/if.h>
int getifaddrs(struct ifaddrs **ifap){if(!ifap)return -1;*ifap=0;return 0;}
void freeifaddrs(struct ifaddrs *ifa){(void)ifa;}
unsigned int if_nametoindex(const char *ifname){(void)ifname;return 1;}

#include <sys/time.h>
#include <sys/systime.h>
int gettimeofday(struct timeval *tv,struct timezone *tz){u64 sec=0,nsec=0;(void)tz;if(!tv||sysGetCurrentTime(&sec,&nsec)!=0)return -1;tv->tv_sec=(long)sec;tv->tv_usec=(long)(nsec/1000ULL);return 0;}
