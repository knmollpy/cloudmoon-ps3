#include "cloudmoon_clock_compat.h"
#include <sys/time.h>
#include <sys/systime.h>
#include <stdint.h>

int clock_gettime(int clock_id, struct timespec *value) {
    if (!value) return -1;
    if (clock_id == CLOCK_REALTIME) {
        u64 seconds = 0, nanoseconds = 0;
        if (sysGetCurrentTime(&seconds, &nanoseconds) != 0) return -1;
        value->tv_sec = (time_t)seconds;
        value->tv_nsec = (long)nanoseconds;
    } else {
        u64 microseconds = sysGetSystemTime();
        value->tv_sec = (time_t)(microseconds / 1000000ULL);
        value->tv_nsec = (long)((microseconds % 1000000ULL) * 1000ULL);
    }
    return 0;
}
