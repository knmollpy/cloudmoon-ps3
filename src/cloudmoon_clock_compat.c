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

/* PSL1GHT's newlib declares nanosleep but does not provide it. */
#include <errno.h>
int nanosleep(const struct timespec *request, struct timespec *remaining) {
    u64 usec;
    if (!request || request->tv_sec < 0 ||
        request->tv_nsec < 0 || request->tv_nsec >= 1000000000L) {
        errno = EINVAL;
        return -1;
    }
    usec = (u64)request->tv_sec * 1000000ULL +
           (u64)request->tv_nsec / 1000ULL;
    if (remaining) {
        remaining->tv_sec = 0;
        remaining->tv_nsec = 0;
    }
    while (usec > 0xFFFFFFFFULL) {
        sysUsleep(0xFFFFFFFFU);
        usec -= 0xFFFFFFFFULL;
    }
    if (usec)
        sysUsleep((u32)usec);
    return 0;
}
