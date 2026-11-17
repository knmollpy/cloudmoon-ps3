#ifndef CLOUDMOON_CLOCK_COMPAT_H
#define CLOUDMOON_CLOCK_COMPAT_H
#include <time.h>
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif
#ifdef __cplusplus
extern "C" {
#endif
int clock_gettime(int clock_id, struct timespec *value);
#ifdef __cplusplus
}
#endif
#endif
