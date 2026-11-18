#include "cloudmoon_video_limits.h"

#include <stdio.h>

static int failures;

#define CHECK(name, expression, expected) do { \
    int actual = (expression); \
    if (actual != (expected)) { \
        fprintf(stderr, "%s: expected %d, got %d\\n", (name), (expected), actual); \
        failures++; \
    } \
} while (0)

int main(void) {
    CHECK("landscape 720p", cm_cloudmoon_video_dimensions_supported(1280, 720, 1280, 720), 1);
    CHECK("portrait 720p", cm_cloudmoon_video_dimensions_supported(1280, 720, 720, 1280), 1);
    CHECK("portrait 4:3 target", cm_cloudmoon_video_dimensions_supported(1024, 768, 720, 1280), 1);
    CHECK("portrait 1080p", cm_cloudmoon_video_dimensions_supported(1920, 1080, 1080, 1920), 1);
    CHECK("landscape 1080p", cm_cloudmoon_video_dimensions_supported(1920, 1080, 1920, 1080), 1);
    CHECK("720p rejects 1080p pixels", cm_cloudmoon_video_dimensions_supported(1280, 720, 1920, 1080), 0);
    CHECK("4:3 rejects oversized landscape", cm_cloudmoon_video_dimensions_supported(1024, 768, 1440, 640), 0);
    CHECK("reject width over Cell HD limit", cm_cloudmoon_video_dimensions_supported(1920, 1080, 2048, 960), 0);
    CHECK("reject height over Cell HD limit", cm_cloudmoon_video_dimensions_supported(1920, 1080, 960, 2048), 0);
    CHECK("reject zero request", cm_cloudmoon_video_dimensions_supported(0, 720, 720, 1280), 0);
    CHECK("reject zero frame", cm_cloudmoon_video_dimensions_supported(1280, 720, 0, 1280), 0);

    if (failures) {
        fprintf(stderr, "%d CloudMoon video dimension test(s) failed.\\n", failures);
        return 1;
    }
    puts("CloudMoon video orientation and memory-limit tests passed.");
    return 0;
}
