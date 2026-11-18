#include "cloudmoon_video_limits.h"

#include <stdint.h>

#define CM_VIDEO_MAX_DIMENSION 1920
#define CM_VIDEO_MIN_MOBILE_PIXELS (720ULL * 1280ULL)

int cm_cloudmoon_video_dimensions_supported(int requested_width,
                                            int requested_height,
                                            int actual_width,
                                            int actual_height) {
    uint64_t requested_pixels;
    uint64_t actual_pixels;
    int max_dimension;

    if (requested_width <= 0 || requested_height <= 0 ||
        actual_width <= 0 || actual_height <= 0) return 0;

    max_dimension = requested_width > requested_height ?
                    requested_width : requested_height;
    /* A 720p portrait session is 720x1280, the same pixel budget as 1280x720. */
    if (max_dimension < 1280) max_dimension = 1280;
    if (max_dimension > CM_VIDEO_MAX_DIMENSION ||
        actual_width > max_dimension || actual_height > max_dimension ||
        actual_width > CM_VIDEO_MAX_DIMENSION ||
        actual_height > CM_VIDEO_MAX_DIMENSION) return 0;

    requested_pixels = (uint64_t)(unsigned)requested_width *
                       (uint64_t)(unsigned)requested_height;
    if (requested_pixels < CM_VIDEO_MIN_MOBILE_PIXELS)
        requested_pixels = CM_VIDEO_MIN_MOBILE_PIXELS;

    actual_pixels = (uint64_t)(unsigned)actual_width *
                    (uint64_t)(unsigned)actual_height;
    return actual_pixels <= requested_pixels;
}
