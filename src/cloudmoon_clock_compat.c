#include "cloudmoon_clock_compat.h"
#include <sys/systime.h>
int clock_gettime(int clock_id, struct timespec *value) {
    u64 seconds = 0, nanoseconds = 0;
    (void)clock_id;
    if (!value || sysGetCurrentTime(&seconds, &nanoseconds) != 0) return -1;
    value->tv_sec = (time_t)seconds;
    value->tv_nsec = (long)nanoseconds;
    return 0;
}
