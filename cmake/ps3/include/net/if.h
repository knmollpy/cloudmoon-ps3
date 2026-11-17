#ifndef CLOUDMOON_PS3_NET_IF_H
#define CLOUDMOON_PS3_NET_IF_H
#define IFNAMSIZ 16
#define IF_NAMESIZE IFNAMSIZ
#define IFF_UP 0x1
#define IFF_LOOPBACK 0x8
unsigned int if_nametoindex(const char *ifname);
#endif
