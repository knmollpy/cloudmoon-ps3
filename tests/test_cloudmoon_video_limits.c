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


    {
        int iw = 0, ih = 0;
        CHECK("rotation portrait swaps landscape frame",
              cm_cloudmoon_input_dimensions(1280, 720, 0, &iw, &ih), 0);
        CHECK("portrait width", iw, 720);
        CHECK("portrait height", ih, 1280);
        CHECK("rotation landscape swaps portrait frame",
              cm_cloudmoon_input_dimensions(720, 1280, 1, &iw, &ih), 0);
        CHECK("landscape width", iw, 1280);
        CHECK("landscape height", ih, 720);
        CHECK("already portrait remains portrait",
              cm_cloudmoon_input_dimensions(720, 1280, 0, &iw, &ih), 0);
        CHECK("already landscape remains landscape",
              cm_cloudmoon_input_dimensions(1280, 720, 1, &iw, &ih), 0);
        CHECK("unknown rotation preserves video aspect",
              cm_cloudmoon_input_dimensions(1280, 720, -1, &iw, &ih), 0);
        CHECK("invalid rotation rejected",
              cm_cloudmoon_input_dimensions(1280, 720, 2, &iw, &ih), -1);
    }

    if (failures) {
        fprintf(stderr, "%d CloudMoon video dimension test(s) failed.\\n", failures);
        return 1;
    }
    puts("CloudMoon video orientation and memory-limit tests passed.");
    return 0;
}
