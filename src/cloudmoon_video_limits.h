#ifndef CLOUDMOON_VIDEO_LIMITS_H
#define CLOUDMOON_VIDEO_LIMITS_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Accepts portrait/landscape orientation swaps while keeping the decoded
 * frame within the memory budget negotiated by the selected stream preset.
 */
int cm_cloudmoon_video_dimensions_supported(int requested_width,
                                            int requested_height,
                                            int actual_width,
                                            int actual_height);

/*
 * Maps the decoded video dimensions to the Android input coordinate space.
 * rotation: -1 = not supplied, 0 = portrait, 1 = landscape.
 */
int cm_cloudmoon_input_dimensions(int video_width, int video_height,
                                  int rotation, int *input_width,
                                  int *input_height);

#ifdef __cplusplus
}
#endif

#endif
