#ifndef CLOUDMOON_TIME_COMPAT_H
#define CLOUDMOON_TIME_COMPAT_H
#include <sys/time.h>
#ifdef __cplusplus
extern "C" {
#endif
int gettimeofday(struct timeval *tv, void *tz);
#ifdef __cplusplus
}
#endif
#endif
