#include "multimedia_service_video.h"
#include <cstring>

Video::Video(const char *impl_name)
    : MultimediaServiceVideoBase(impl_name)
{
}

Video::~Video() = default;

CamVideoImpl *Video::getVideoImpl()
{
    if ((implName == "v4l2-normal") || (implName == "uvc") || (implName == "usb-v4l2"))
    {
        return const_cast<CamVideoImpl *>(&cam_video_impl_v4l2);
    }
    if ((implName == "rk-rv1106") || (implName == "rk-venc"))
    {
        return const_cast<CamVideoImpl *>(&video_impl_rk_rv1106);
    }
    return const_cast<CamVideoImpl *>(&video_impl_rk_rv1106);
}
