#ifndef __VIDEO_RV1106_H__
#define __VIDEO_RV1106_H__

#include "video_interface.h"
#include "video.h"
#ifdef __cplusplus
extern "C" {
#endif

int video_rv1106_open(const VideoConfig *config);
void video_rv1106_close(void);

#ifdef __cplusplus
}
#endif

#endif
