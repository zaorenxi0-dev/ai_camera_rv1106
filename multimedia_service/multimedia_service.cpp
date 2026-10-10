#include "multimedia_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "app_config.h"
#include "devices_capability.h"
#include "logger.h"
#include "message_define.h"
#include "multimedia_service_video.h"
#include "cam_utils.h"

using namespace camsvc;

#define MULTIMEDIA_SERVICE_JSON_CONFIG_PATH      APP_MULTIMEDIA_CFG_PATH
#define MULTIMEDIA_SERVICE_JSON_CONFIG_PATH_SAVE APP_MULTIMEDIA_CFG_PATH_SAVE

static char *multimedia_service_local_json(const char *name)
{
    if (!name)
    {
        return NULL;
    }

    char *buf = NULL;
    int   len = 0;
    if (cm_read_file_data(name, &buf, &len) < 0)
    {
        return NULL;
    }
    return buf;
}

MultimediaService::MultimediaService() = default;

MultimediaService::~MultimediaService()
{
    deinit();
}

int MultimediaService::init()
{
    deinit();

    memset(&config_json, 0, sizeof(config_json));
    config_json.enable                = 1;
    config_json.duration              = 30;
    config_json.interval_count        = 3;
    config_json.interval_time         = 3;
    config_json.cycle_count_max       = 3;
    config_json.reboot_enable         = 1;
    config_json.reboot_count_max      = 3;
    config_json.poweroff_enable       = 1;
    config_json.poweroff_count_max    = 3;
    config_json.error_action_duration = 43200;
    config_json.error_clear_duration  = 600;

    resume_detection    = 0;
    system_status       = 0;
    video_detect_status = 1;
    system_busy_flag    = 1;

    yqsl_dc_handler()->Init();
    multimedia_service_json_load(const_cast<char *>(MULTIMEDIA_SERVICE_JSON_CONFIG_PATH));
    multimedia_service_json_load(const_cast<char *>(MULTIMEDIA_SERVICE_JSON_CONFIG_PATH_SAVE));
    return camera_dev_detect();
}

int MultimediaService::deinit()
{
    for (auto &item : video_mp)
    {
        delete item.second;
    }
    video_mp.clear();
    videoErrNoNode.clear();
    videoErrOpenFailed.clear();
    videoErrReadFailed.clear();
    videoErrReadNull.clear();
    return 0;
}

int MultimediaService::start()
{
    if (video_mp.empty())
    {
        camera_dev_detect();
    }
    for (auto &item : video_mp)
    {
        item.second->start();
    }
    return 0;
}

int MultimediaService::stop()
{
    for (auto &item : video_mp)
    {
        item.second->stop();
    }
    return 0;
}

int MultimediaService::run()
{
    while (!this->getExited())
    {
        sleep(1);
    }
    return 0;
}

int MultimediaService::camera_dev_detect()
{
    SvcErrorRecord def_record;
    def_record.init(true, 0);

    int video_channel_num = yqsl_dc_handler()->GetCountCamera();
    for (int i = 0; i < video_channel_num; ++i)
    {
        YQSLMessageDeviceCapabilityItem item;
        memset(&item, 0, sizeof(item));

        int result = yqsl_dc_handler()->GetDeviceCamera(i, item);
        VideoBaseInterface *videoCreate = static_cast<VideoBaseInterface *>(new Video(item.impl));
        if (!videoCreate)
        {
            return YQSL_E_MALLOC_FAIL;
        }

        char name[32] = {0};
        snprintf(name, sizeof(name), "video-%d", i);
        videoCreate->getExceptionHandler().setName(name);
        videoCreate->setParent(this);
        videoCreate->set_error_deal_rule(config_json.interval_count);
        videoCreate->getExceptionHandler().setCheckDuration(config_json.error_action_duration * 1000,
                                                            config_json.error_clear_duration * 1000);

        video_mp[i]            = videoCreate;
        videoErrNoNode[i]      = def_record;
        videoErrOpenFailed[i]  = def_record;
        videoErrReadFailed[i]  = def_record;
        videoErrReadNull[i]    = def_record;
        videoErrNoNode[i].setName("noNode", i);
        videoErrOpenFailed[i].setName("openFailed", i);
        videoErrReadFailed[i].setName("readFailed", i);
        videoErrReadNull[i].setName("readNull", i);

        if (result < 0)
        {
            videoCreate->set_error_status(&videoCreate->channel_info.error_status, V_CHANNEL_SET,
                                          CHANNEL_IMPL_NOT_FIND);
            continue;
        }

        videoCreate->channel_info.index = i;
        strncpy(videoCreate->channel_info.dev_name, item.device, sizeof(videoCreate->channel_info.dev_name) - 1);
        strncpy(videoCreate->channel_info.dev_impl, item.impl, sizeof(videoCreate->channel_info.dev_impl) - 1);
        videoCreate->set_error_status(&videoCreate->channel_info.error_status, V_CHANNEL_RESET,
                                      CHANNEL_IMPL_NOT_FIND);
    }

    return 0;
}

int MultimediaService::multimedia_service_parse_load(char *data, multimedia_service_json_config *config)
{
    if (!data || !config)
    {
        return YQ_ERROR_ARGS;
    }
    return multimedia_service_json_config_confirm(config);
}

int MultimediaService::multimedia_service_json_load(char *cfg_path)
{
    if (!cfg_path || access(cfg_path, F_OK) != 0)
    {
        return YQ_ERROR_NOT_EXIST;
    }

    char *json_str = multimedia_service_local_json(cfg_path);
    if (!json_str)
    {
        return YQ_ERROR_NOT_EXIST;
    }

    int ret = multimedia_service_parse_load(json_str, &config_json);
    free(json_str);
    return ret;
}

int MultimediaService::multimedia_service_json_config_confirm(multimedia_service_json_config *json_config)
{
    if (!json_config)
    {
        return YQ_ERROR_ARGS;
    }

    if (json_config->interval_count <= 0)
    {
        json_config->interval_count = 3;
    }
    if (json_config->error_action_duration <= 0)
    {
        json_config->error_action_duration = 43200;
    }
    if (json_config->error_clear_duration <= 0)
    {
        json_config->error_clear_duration = 600;
    }
    return 0;
}

int MultimediaService::ctl(int type, void *args)
{
    switch (type)
    {
    case CMD_MULTIMEDIA_VIDEO_CHANNEL_INFO:
    {
        auto *channel = static_cast<MultimediaVideoChannel *>(args);
        if (!channel)
        {
            return YQ_ERROR_ARGS;
        }
        auto iter = video_mp.find(channel->index);
        if (iter == video_mp.end())
        {
            return YQ_ERROR_NOT_EXIST;
        }
        channel->channelInfo = iter->second->channel_info;
        channel->channelArgs = iter->second->getChannelArgs();
        return 0;
    }
    case CMD_MULTIMEDIA_VIDEO_SET:
    {
        auto *channel = static_cast<MultimediaVideoChannel *>(args);
        if (!channel)
        {
            return YQ_ERROR_ARGS;
        }
        auto iter = video_mp.find(channel->index);
        if (iter == video_mp.end())
        {
            return YQ_ERROR_NOT_EXIST;
        }
        VideoBaseInterface *video = iter->second;
        OSAL_MutexLock(video->get_mutexobj());
        int result = 0;
        switch (channel->set_type)
        {
        case SET_WIDTH:
            result = video->set(CAM_CMD_SET_WIDTH, &channel->channelArgs.width);
            break;
        case SET_HEIGHT:
            result = video->set(CAM_CMD_SET_HEIGHT, &channel->channelArgs.height);
            break;
        case SET_FPS:
            result = video->set(CAM_CMD_SET_FPS, &channel->channelArgs.fps);
            break;
        case SET_FORMAT:
            result = video->set(CAM_CMD_SET_PIX_FORMAT, &channel->channelArgs.pix_format);
            break;
        case SET_DST_FORMAT:
            result = video->set(CAM_CMD_SET_DST_PIX_FORMAT, &channel->channelArgs.dst_format);
            break;
        case SET_RC_MODE:
            result = video->set(CAM_CMD_SET_DST_RC_MODE, channel->channelArgs.rc_mode);
            break;
        case SET_BITRATE:
            result = video->set(CAM_CMD_SET_DST_BITRATE, &channel->channelArgs.bitrate);
            break;
        case SET_INDEX:
            result = video->set(CAM_CMD_SET_CHANNEL, &channel->channelArgs.index);
            break;
        case SET_ALL:
            video->setChannelArgs(&channel->channelArgs);
            result = 0;
            break;
        case REQUEST_KEY_FRAME:
            result = video->requestKeyFrame();
            break;
        default:
            result = YQ_ERROR_ARGS;
            break;
        }
        if (result >= 0)
        {
            video->setChannelArgs(&channel->channelArgs);
        }
        OSAL_MutexUnlock(video->get_mutexobj());
        return result;
    }
    case CMD_MULTIMEDIA_VIDEO_OPEN:
    case CMD_MULTIMEDIA_VIDEO_START:
    case CMD_MULTIMEDIA_VIDEO_CLOSE:
    case CMD_MULTIMEDIA_VIDEO_STOP:
    case CMD_MULTIMEDIA_VIDEO_READ:
    {
        auto *channel = static_cast<MultimediaVideoChannel *>(args);
        if (!channel)
        {
            return YQ_ERROR_ARGS;
        }
        auto iter = video_mp.find(channel->index);
        if (iter == video_mp.end())
        {
            return YQ_ERROR_NOT_EXIST;
        }
        VideoBaseInterface *video = iter->second;
        int result = 0;
        OSAL_MutexLock(video->get_mutexobj());
        switch (type)
        {
        case CMD_MULTIMEDIA_VIDEO_OPEN:
            result = video->open(video->channel_info.dev_name);
            break;
        case CMD_MULTIMEDIA_VIDEO_START:
            result = video->start();
            break;
        case CMD_MULTIMEDIA_VIDEO_CLOSE:
            result = video->close();
            break;
        case CMD_MULTIMEDIA_VIDEO_STOP:
            result = video->stop();
            break;
        case CMD_MULTIMEDIA_VIDEO_READ:
            result = video->read(&channel->channelInfo.buf);
            if (result >= 0)
            {
                video->channel_info.buf = channel->channelInfo.buf;
            }
            break;
        default:
            result = YQ_ERROR_ARGS;
            break;
        }
        channel->channelInfo = video->channel_info;
        OSAL_MutexUnlock(video->get_mutexobj());
        return result;
    }
    case CMD_MULTIMEDIA_SET_SYS_STATUS:
        system_status = args ? *static_cast<int *>(args) : system_status;
        return 0;
    case CMD_MULTIMEDIA_SET_SYS_BUSY_STATUS:
        system_busy_flag = args ? *static_cast<int *>(args) : system_busy_flag;
        return 0;
    default:
        return YQ_ERROR_NOT_EXIST;
    }
}

std::vector<MultimediaVideoChannel> MultimediaService::getVideoChannels()
{
    std::vector<MultimediaVideoChannel> channels;
    for (auto &item : video_mp)
    {
        MultimediaVideoChannel channel;
        memset(&channel, 0, sizeof(channel));
        channel.index       = item.first;
        channel.channelInfo = item.second->channel_info;
        channel.channelArgs = item.second->getChannelArgs();
        channels.push_back(channel);
    }
    return channels;
}

int MultimediaService::videoStatus(int index)
{
    auto iter = video_mp.find(index);
    if (iter == video_mp.end())
    {
        return YQ_ERROR_NOT_EXIST;
    }
    return iter->second->runStatus();
}

int MultimediaService::audioStart()
{
    return 0;
}

int MultimediaService::audioStop()
{
    return 0;
}

void MultimediaService::privNotifyError(int channel, int error, bool isHappen, char *desc)
{
    (void)channel;
    (void)error;
    (void)isHappen;
    (void)desc;
}

void MultimediaService::checkVideoError(int channel)
{
    (void)channel;
}

int MultimediaService::exceptionErrorCodeConverter(int innerCode)
{
    return innerCode;
}
