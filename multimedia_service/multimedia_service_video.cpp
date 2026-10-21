#include "multimedia_service_video.h"
#include <cstring>

Video::Video(const char *impl_name)
    : MultimediaServiceVideoBase(impl_name)
{
}

Video::~Video() = default;

CamVideoImpl *Video::getVideoImpl()
{
    /* 单摄像头方案：只保留 RV1106 MIPI (rk-venc/rk-rv1106) 实现，
     * UVC/USB 的 v4l2 实现已随 ch1 一并移除 */
    return const_cast<CamVideoImpl *>(&video_impl_rk_rv1106);
}
