#ifndef CLOUDMOON_TIME_COMPAT_H
#define CLOUDMOON_TIME_COMPAT_H
#ifdef __cplusplus
extern "C" {
#endif
#include <sys/time.h>
struct timezone;
int gettimeofday(struct timeval *tv, struct timezone *tz);
#ifdef __cplusplus
}
#endif
#endif
