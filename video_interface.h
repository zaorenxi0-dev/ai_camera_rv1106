#ifndef __VIDEO_INTERFACE_H__
#define __VIDEO_INTERFACE_H__

typedef struct VideoConfig {
    unsigned int width;
    unsigned int height;
    unsigned char fps;
} VideoConfig;

int video_open(const VideoConfig *config);
void video_close(void);


#endif
