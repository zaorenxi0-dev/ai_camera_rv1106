#ifndef __MULTIMEDIA_SERVICE_VIDEO_H__
#define __MULTIMEDIA_SERVICE_VIDEO_H__

#include "multimedia_service_video_base.h"

class Video : public MultimediaServiceVideoBase
{
  public:
    Video(const char *impl_name = "rk-venc");
    virtual ~Video();

  protected:
    CamVideoImpl *getVideoImpl() override;
};



#endif
