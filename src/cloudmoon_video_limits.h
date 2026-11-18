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

#ifdef __cplusplus
}
#endif

#endif
