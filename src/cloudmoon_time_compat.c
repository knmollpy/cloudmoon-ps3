#include "cloudmoon_time_compat.h"
#include <sys/time.h>
#include <sys/systime.h>

int gettimeofday(struct timeval *tv, void *tz) {
    u64 seconds = 0, nanoseconds = 0;
    (void)tz;
    if (!tv || sysGetCurrentTime(&seconds, &nanoseconds) != 0) return -1;
    tv->tv_sec = (long)seconds;
    tv->tv_usec = (long)(nanoseconds / 1000ULL);
    return 0;
}
