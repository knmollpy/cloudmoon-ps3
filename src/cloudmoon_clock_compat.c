#include "cloudmoon_clock_compat.h"
#include <sys/time.h>
#include <stdint.h>

/* PSL1GHT exposes a monotonic LV2 system clock in microseconds. */
int clock_gettime(int clock_id, struct timespec *value) {
    u64 microseconds;
    (void)clock_id;
    if (!value) return -1;
    microseconds = sysGetSystemTime();
    value->tv_sec = (time_t)(microseconds / 1000000ULL);
    value->tv_nsec = (long)((microseconds % 1000000ULL) * 1000ULL);
    return 0;
}
