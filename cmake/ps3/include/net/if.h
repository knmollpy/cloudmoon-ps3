#ifndef CLOUDMOON_PS3_NET_IF_H
#define CLOUDMOON_PS3_NET_IF_H
#define IFNAMSIZ 16
#define IF_NAMESIZE IFNAMSIZ
unsigned int if_nametoindex(const char *ifname);
#endif
