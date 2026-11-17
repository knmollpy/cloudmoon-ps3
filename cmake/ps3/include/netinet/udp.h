#ifndef CLOUDMOON_PS3_NETINET_UDP_H
#define CLOUDMOON_PS3_NETINET_UDP_H

#include <sys/types.h>
#include <netinet/in.h>

/* usrsctp only uses sizeof(struct udphdr) to account for UDP encapsulation.
 * PSL1GHT has no BSD netinet/udp.h, so provide the standard 8-byte header. */
struct udphdr {
    u_int16_t uh_sport;
    u_int16_t uh_dport;
    u_int16_t uh_ulen;
    u_int16_t uh_sum;
};

#endif
