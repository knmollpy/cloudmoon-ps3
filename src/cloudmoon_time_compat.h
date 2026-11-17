#ifndef CLOUDMOON_TIME_COMPAT_H
#define CLOUDMOON_TIME_COMPAT_H
#include <sys/time.h>
#ifdef __cplusplus
extern "C" {
#endif
struct timezone;
int gettimeofday(struct timeval *tv, struct timezone *tz);
#ifdef __cplusplus
}
#endif
#endif
