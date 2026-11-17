#pragma once
/*
 * PSL1GHT/newlib does not provide the POSIX getaddrinfo family. Keep the
 * IPv4-only resolver adapter shared by libjuice C and libdatachannel C++.
 */
#include_next <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef AI_PASSIVE
#define AI_PASSIVE 0x0001
#endif
#ifndef AI_CANONNAME
#define AI_CANONNAME 0x0002
#endif
#ifndef AI_NUMERICHOST
#define AI_NUMERICHOST 0x0004
#endif
#ifndef AI_NUMERICSERV
#define AI_NUMERICSERV 0x0400
#endif
#ifndef NI_NUMERICHOST
#define NI_NUMERICHOST 0x0001
#endif
#ifndef NI_NUMERICSERV
#define NI_NUMERICSERV 0x0002
#endif
#ifndef NI_DGRAM
#define NI_DGRAM 0x0010
#endif
#ifndef EAI_FAIL
#define EAI_FAIL (-4)
#endif
#ifndef EAI_NONAME
#define EAI_NONAME (-2)
#endif
#ifndef EAI_SERVICE
#define EAI_SERVICE (-8)
#endif
#ifndef EAI_FAMILY
#define EAI_FAMILY (-6)
#endif

static inline int cm_ps3_getaddrinfo(const char *node, const char *service,
                                     const struct addrinfo *hints,
                                     struct addrinfo **result) {
    unsigned long port = 0;
    struct in_addr address;
    struct addrinfo *ai;
    struct sockaddr_in *sin;
    if (!result)
        return EAI_FAIL;
    *result = NULL;
    if (hints && hints->ai_family != AF_UNSPEC && hints->ai_family != AF_INET)
        return EAI_FAMILY;
    if (service && *service) {
        char *end = NULL;
        port = strtoul(service, &end, 10);
        if (end == service || *end != '\0' || port > 65535)
            return EAI_SERVICE;
    }
    if (!node || !*node) {
        address.s_addr = (hints && (hints->ai_flags & AI_PASSIVE))
                             ? htonl(INADDR_ANY) : htonl(INADDR_LOOPBACK);
    } else if (inet_pton(AF_INET, node, &address) != 1) {
        struct hostent *host;
        if (hints && (hints->ai_flags & AI_NUMERICHOST))
            return EAI_NONAME;
        host = gethostbyname(node);
        if (!host || host->h_addrtype != AF_INET ||
            host->h_length != (int)sizeof(address) ||
            !host->h_addr_list || !host->h_addr_list[0])
            return EAI_NONAME;
        memcpy(&address, host->h_addr_list[0], sizeof(address));
    }
    ai = (struct addrinfo *)calloc(1, sizeof(*ai));
    sin = (struct sockaddr_in *)calloc(1, sizeof(*sin));
    if (!ai || !sin) {
        free(ai);
        free(sin);
        return EAI_FAIL;
    }
    sin->sin_family = AF_INET;
    sin->sin_port = htons((uint16_t)port);
    sin->sin_addr = address;
    ai->ai_family = AF_INET;
    ai->ai_socktype = hints ? hints->ai_socktype : 0;
    ai->ai_protocol = hints ? hints->ai_protocol : 0;
    ai->ai_addrlen = (socklen_t)sizeof(*sin);
    ai->ai_addr = (struct sockaddr *)sin;
    *result = ai;
    return 0;
}

static inline int cm_ps3_getnameinfo(const struct sockaddr *address,
                                     socklen_t address_len,
                                     char *host, socklen_t host_len,
                                     char *service, socklen_t service_len,
                                     int flags) {
    const struct sockaddr_in *sin;
    (void)flags;
    if (!address || address->sa_family != AF_INET ||
        address_len < (socklen_t)sizeof(struct sockaddr_in))
        return EAI_FAMILY;
    sin = (const struct sockaddr_in *)address;
    if (host && !inet_ntop(AF_INET, &sin->sin_addr, host, host_len))
        return EAI_FAIL;
    if (service) {
        int n = snprintf(service, service_len, "%u",
                         (unsigned)ntohs(sin->sin_port));
        if (n < 0 || (socklen_t)n >= service_len)
            return EAI_FAIL;
    }
    return 0;
}

static inline void cm_ps3_freeaddrinfo(struct addrinfo *ai) {
    while (ai) {
        struct addrinfo *next = ai->ai_next;
        free(ai->ai_addr);
        free(ai->ai_canonname);
        free(ai);
        ai = next;
    }
}

#define getaddrinfo cm_ps3_getaddrinfo
#define getnameinfo cm_ps3_getnameinfo
#define freeaddrinfo cm_ps3_freeaddrinfo
