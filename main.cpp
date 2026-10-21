#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#include "app_config.h"
#include "cm_common.h"
#include "message_define.h"
#include "multimedia_service.h"
#include "video.h"
#include "ipc_dbus_client.h"

using camsvc::MultimediaVideoChannel;
using camsvc::SET_ALL;

static int save_file(const char *path, const void *data, size_t len)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        std::perror("fopen");
        return -1;
    }

    size_t written = fwrite(data, 1, len, fp);
    fclose(fp);
    if (written != len) {
        std::cerr << "write file failed, written=" << written << " len=" << len << std::endl;
        return -1;
    }
    return 0;
}

static int set_channel_args(MultimediaService &service, int index, uint32_t width, uint32_t height,
                            uint8_t fps, CAM_FMT format)
{
    MultimediaVideoChannel channel;
    std::memset(&channel, 0, sizeof(channel));
    channel.index                   = index;
    channel.set_type                = SET_ALL;
    channel.channelArgs.index       = index;
    channel.channelArgs.width       = width;
    channel.channelArgs.height      = height;
    channel.channelArgs.fps         = fps;
    channel.channelArgs.pix_format  = format;
    channel.channelArgs.dst_format  = format;
    channel.channelArgs.bitrate     = 2 * 1024 * 1024;
    std::snprintf(channel.channelArgs.rc_mode, sizeof(channel.channelArgs.rc_mode), "cbr");
    return service.ctl(CMD_MULTIMEDIA_VIDEO_SET, &channel);
}

static int capture_jpeg(MultimediaService &service, int index, const char *output, int warmup_frames)
{
    MultimediaVideoChannel channel;
    std::memset(&channel, 0, sizeof(channel));
    channel.index = index;

    int ret = service.ctl(CMD_MULTIMEDIA_VIDEO_START, &channel);
    if (ret < 0) {
        std::cerr << "channel " << index << " start failed: " << ret << std::endl;
        return ret;
    }

    if (index == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    for (int i = 0; i < warmup_frames; ++i) {
        std::memset(&channel.channelInfo.buf, 0, sizeof(channel.channelInfo.buf));
        ret = service.ctl(CMD_MULTIMEDIA_VIDEO_READ, &channel);
        std::cout << "warmup channel[" << index << "] frame[" << i << "] ret=" << ret
                  << " len=" << channel.channelInfo.buf.length << std::endl;
    }

    bool saved = false;
    for (int retry = 0; retry < 20; ++retry) {
        std::memset(&channel.channelInfo.buf, 0, sizeof(channel.channelInfo.buf));
        ret = service.ctl(CMD_MULTIMEDIA_VIDEO_READ, &channel);
        if ((ret == YQ_ERROR_DONE) && channel.channelInfo.buf.start && channel.channelInfo.buf.length > 0) {
            ret = save_file(output, channel.channelInfo.buf.start, channel.channelInfo.buf.length);
            if (ret == 0) {
                std::cout << "saved channel[" << index << "] => " << output
                          << " len=" << channel.channelInfo.buf.length << std::endl;
                saved = true;
            }
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    int stop_ret = service.ctl(CMD_MULTIMEDIA_VIDEO_STOP, &channel);
    if (stop_ret < 0) {
        std::cerr << "channel " << index << " stop failed: " << stop_ret << std::endl;
    }

    if (!saved) {
        return -1;
    }
    return 0;
}

static int detect_with_yolo(const char *img_path)
{
    char *result = NULL;
    int ret = ipc_dbus_client_call(AI_DETECT_DBUS_NAME, AI_DETECT_DBUS_PATH,
                                   AI_DETECT_DBUS_IFACE, AI_DETECT_DBUS_METHOD,
                                   img_path, &result);
    if (ret < 0) {
        std::cerr << "yolo detect failed: " << ret << std::endl;
        return ret;
    }

    std::cout << "yolo detect result [" << img_path << "]: "
              << (result ? result : "(empty)") << std::endl;
    if (result) {
        free(result);
    }
    return 0;
}

int main()
{
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    std::cout.setf(std::ios::unitbuf);
    std::cerr.setf(std::ios::unitbuf);

    MultimediaService service;

    int ret = service.init();
    if (ret < 0) {
        std::cerr << "multimedia_service init failed: " << ret << std::endl;
        return 1;
    }

    ret = set_channel_args(service, 0, 1920, 1080, 15, CAM_FMT_JPEG);
    if (ret < 0) {
        std::cerr << "set sc3336 args failed: " << ret << std::endl;
        return 1;
    }

    ret = capture_jpeg(service, 0, "snapshot_sc3336.jpg", 4);
    if (ret < 0) {
        std::cerr << "capture sc3336 failed: " << ret << std::endl;
        return 1;
    }
    detect_with_yolo("snapshot_sc3336.jpg");

    service.deinit();
    return 0;
}
