#ifndef CLOUDMOON_PS3_NETINET_IP_H
#define CLOUDMOON_PS3_NETINET_IP_H
#include <stdint.h>
#include <netinet/in.h>
struct ip {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint8_t ip_hl:4, ip_v:4;
#else
    uint8_t ip_v:4, ip_hl:4;
#endif
    uint8_t ip_tos;
    uint16_t ip_len,ip_id,ip_off;
    uint8_t ip_ttl,ip_p;
    uint16_t ip_sum;
    struct in_addr ip_src,ip_dst;
};
#define IP_RP 0x8000
#define IP_DF 0x4000
#define IP_MF 0x2000
#define IP_OFFMASK 0x1fff
#endif
