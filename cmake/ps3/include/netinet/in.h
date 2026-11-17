#ifndef CLOUDMOON_PS3_NETINET_IN_H
#define CLOUDMOON_PS3_NETINET_IN_H
#include_next <netinet/in.h>
/* PSL1GHT exposes IPv4 sockets only; libusrsctp still embeds these
 * address storage types in public structs even with SCTP IPv6 disabled. */
#ifndef CLOUDMOON_PS3_IPV6_STORAGE_TYPES
#define CLOUDMOON_PS3_IPV6_STORAGE_TYPES
struct in6_addr { u8 s6_addr[16]; };
struct sockaddr_in6 {
    u8 sin6_len;
    sa_family_t sin6_family;
    in_port_t sin6_port;
    u32 sin6_flowinfo;
    struct in6_addr sin6_addr;
    u32 sin6_scope_id;
};
struct sockaddr_storage {
    u8 ss_len;
    sa_family_t ss_family;
    u8 __ss_pad1[6];
    u64 __ss_align;
    u8 __ss_pad2[112];
};
#endif
#endif
