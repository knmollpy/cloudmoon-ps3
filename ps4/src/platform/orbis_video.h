#ifndef CLOUDMOON_ORBIS_VIDEO_H
#define CLOUDMOON_ORBIS_VIDEO_H

#include "../ui/cloudmoon_ui.h"

int cm_orbis_video_init(int width, int height);
cm_ui_surface_t *cm_orbis_video_surface(void);
int cm_orbis_video_present(void);
void cm_orbis_video_shutdown(void);

#endif
