#ifndef CLOUDMOON_CLOCK_COMPAT_H
#define CLOUDMOON_CLOCK_COMPAT_H
#include <time.h>
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif

/* POSIX/BSD constants missing from the PSL1GHT socket headers. */
#ifndef SOCK_SEQPACKET
#define SOCK_SEQPACKET 5
#endif
#ifndef IPPORT_RESERVED
#define IPPORT_RESERVED 1024
#endif
#ifdef __cplusplus
extern "C" {
#endif
int clock_gettime(int clock_id, struct timespec *value);
#ifdef __cplusplus
}
#endif
#endif
