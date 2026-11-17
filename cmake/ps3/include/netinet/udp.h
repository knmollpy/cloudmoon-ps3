#ifndef CLOUDMOON_PS3_NETINET_UDP_H
#define CLOUDMOON_PS3_NETINET_UDP_H

#include <stdint.h>
#include <netinet/in.h>

/* usrsctp only needs the standard 8-byte UDP header layout. */
struct udphdr {
    uint16_t uh_sport;
    uint16_t uh_dport;
    uint16_t uh_ulen;
    uint16_t uh_sum;
};

#endif
