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


/* 多张连拍：START 一次，循环读帧存盘，最后 STOP 一次。
 * ★ 实测（2026-10-22）sc3336 + RV1106 SDK 只有开机后第一次 ISP 拉流能出图，
 * 之后 STOP/START 重新拉流会一直 GetStream 失败(0xa004800e)。
 * 因此多张拍摄绝不能中途复位通道 */
static int capture_jpeg_batch(MultimediaService &service, int index,
                              int count, const char *name_prefix)
{
    MultimediaVideoChannel channel;
    std::memset(&channel, 0, sizeof(channel));
    channel.index = index;

    int ret = service.ctl(CMD_MULTIMEDIA_VIDEO_START, &channel);
    if (ret < 0) {
        std::cerr << "channel " << index << " start failed: " << ret << std::endl;
        return ret;
    }

    /* ISP 冷启动收敛，等待要足够 */
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));

    for (int shot = 0; shot < count; ++shot) {
        /* 第一张多丢几帧让曝光/白平衡稳定，后续张只做少量 settles */
        int warmups = (shot == 0) ? 4 : 2;
        for (int i = 0; i < warmups; ++i) {
            std::memset(&channel.channelInfo.buf, 0, sizeof(channel.channelInfo.buf));
            ret = service.ctl(CMD_MULTIMEDIA_VIDEO_READ, &channel);
            std::cout << "warmup shot[" << shot << "] frame[" << i << "] ret=" << ret << std::endl;
        }

        char output[64];
        if (count == 1) {
            std::snprintf(output, sizeof(output), "%s.jpg", name_prefix);
        } else {
            std::snprintf(output, sizeof(output), "%s_%d.jpg", name_prefix, shot + 1);
        }

        bool saved = false;
        for (int retry = 0; retry < 20; ++retry) {
            std::memset(&channel.channelInfo.buf, 0, sizeof(channel.channelInfo.buf));
            ret = service.ctl(CMD_MULTIMEDIA_VIDEO_READ, &channel);
            if ((ret == YQ_ERROR_DONE) && channel.channelInfo.buf.start &&
                channel.channelInfo.buf.length > 0) {
                ret = save_file(output, channel.channelInfo.buf.start, channel.channelInfo.buf.length);
                if (ret == 0) {
                    std::cout << "saved shot[" << shot << "] => " << output
                              << " len=" << channel.channelInfo.buf.length << std::endl;
                    saved = true;
                }
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (!saved) {
            std::cerr << "capture shot " << shot << " failed" << std::endl;
            service.ctl(CMD_MULTIMEDIA_VIDEO_STOP, &channel);
            return -1;
        }

        if (shot + 1 < count) {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
        }
    }

    int stop_ret = service.ctl(CMD_MULTIMEDIA_VIDEO_STOP, &channel);
    if (stop_ret < 0) {
        std::cerr << "channel " << index << " stop failed: " << stop_ret << std::endl;
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

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    std::cout.setf(std::ios::unitbuf);
    std::cerr.setf(std::ios::unitbuf);

    /* 用法：./ai_camera_rv1106 [拍照次数]
     * 默认 1 次，输出 snapshot_sc3336.jpg（与旧版一致）；
     * 多次时输出 snapshot_sc3336_1.jpg / _2.jpg ...，
     * 且整个会话只做一次 START/STOP（见 capture_jpeg_batch 注释） */
    int captures = 1;
    if (argc >= 2) {
        captures = std::atoi(argv[1]);
        if (captures < 1) captures = 1;
        if (captures > 10) captures = 10;
    }

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

    char last_output[64] = "snapshot_sc3336.jpg";
    ret = capture_jpeg_batch(service, 0, captures, "snapshot_sc3336");
    if (ret < 0) {
        std::cerr << "capture sc3336 failed: " << ret << std::endl;
        service.deinit();
        return 1;
    }
    if (captures == 1) {
        std::snprintf(last_output, sizeof(last_output), "snapshot_sc3336.jpg");
    } else {
        std::snprintf(last_output, sizeof(last_output), "snapshot_sc3336_%d.jpg", captures);
    }
    detect_with_yolo(last_output);

    service.deinit();
    return 0;
}
