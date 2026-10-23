#include "multimedia_service_video_base.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>
#include <unistd.h>

#include "message_define.h"
#include "cam_utils.h"

using namespace camsvc;

static const char *s_key = "video";
static const char *s_sub = "run";

MultimediaServiceVideoBase::MultimediaServiceVideoBase(const char *impl_name)
{
    statusChanged(0);
    readErrorCount = 0;
    readErrorCycle = 0;
    last_read      = 0;
    use_times      = 0;
    read_count     = 0;
    print_last     = 0;
    frame_count    = 0;
    implName       = impl_name ? impl_name : "";

    /* 自动恢复默认关闭：sc3336 重拉必失败且可能挂死板子，
     * 机制备好、开关留给调用方显式打开 */
    auto_recover_      = false;
    recover_threshold_ = 3;
    recover_max_       = 3;
    recover_interval_  = 1;
    abnormal_cnt_      = 0;
    recover_cnt_       = 0;
    stall_timeout_ms_  = 10000;
}

MultimediaServiceVideoBase::~MultimediaServiceVideoBase()
{
    exited = true;
    if (pid > 0)
    {
        pthread_join(pid, NULL);
        pid = 0;
    }
}

int MultimediaServiceVideoBase::readError()
{
    if (readErrorCount >= 65535)
    {
        readErrorCount = 0;
        readErrorCycle++;
    }
    return ++readErrorCount;
}

int MultimediaServiceVideoBase::reOpenOperation()
{
    switch (runStatus())
    {
    case 0:
    case CHANNEL_IMPL_NOT_FIND:
    case CHANNEL_OPEN_FAILED:
    case CHANNEL_START_FAILED:
        if (channel_info.run_status.is_open == 0)
        {
            int ret = open(channel_info.dev_name);
            if (ret < 0)
            {
                printf("[%s][%s][%s] open failed ret=[%d] errno=[%d]", s_key, s_sub, channel_info.dev_name, ret,
                       errno);
                return ret;
            }
        }

        return openInner();
    default:
        return YQSL_E_OK;
    }
}

void MultimediaServiceVideoBase::statusChanged(int st)
{
    printf("[%s][%s][%s] status changed [%d] => [%d]", s_key, s_sub, channel_info.dev_name,
           channel_info.run_status.status, st);
    channel_info.run_status.status = st;
}

void *thread_func(void *args)
{
    prctl(PR_SET_NAME, "v4l2Thread");
    MultimediaServiceVideoBase *videoBase = static_cast<MultimediaServiceVideoBase *>(args);
    printf("[%s][%s] thread start of dev=[%s]", s_key, s_sub, videoBase->channel_info.dev_name);
    videoBase->run();
    printf("[%s][%s][%s] thread stopped", s_key, s_sub, videoBase->channel_info.dev_name);
    return NULL;
}

void MultimediaServiceVideoBase::setAutoRecover(bool enable, int threshold, int max_times, int interval)
{
    auto_recover_      = enable;
    recover_threshold_ = threshold > 0 ? threshold : 3;
    recover_max_       = max_times >= 0 ? max_times : 3;
    recover_interval_  = interval > 0 ? interval : 1;

    printf("[%s][%s][%s] auto recover %s, threshold=[%d] max=[%d] interval=[%ds]", s_key, s_sub,
           channel_info.dev_name, enable ? "ENABLED" : "disabled", recover_threshold_, recover_max_,
           recover_interval_);
}

int MultimediaServiceVideoBase::doRecover()
{
    /* 先停干净再重建：reOpenOperation() 在 is_open==1 时会直接返回
     * CHANNEL_ALREADY_OPEN，不做任何事，所以必须由这里先停一遍 */
    stopInner();
    closeInner();

    int ret = open(channel_info.dev_name);
    if (ret < 0)
    {
        printf("[%s][%s][%s] recover: reopen failed ret=[%d]", s_key, s_sub, channel_info.dev_name, ret);
        return ret;
    }

    ret = openInner();
    printf("[%s][%s][%s] recover: rebuild ret=[%d] status=[%d]", s_key, s_sub, channel_info.dev_name, ret,
           runStatus());
    return ret;
}

int MultimediaServiceVideoBase::run()
{
    while (!exited)
    {
        /* 状态由主线程在持锁路径下通过 statusChanged() 改写，
         * 读侧必须同样持锁，否则会读到不一致的中间状态 */
        int                status    = CHANNEL_OPEN_SUCCESS;
        unsigned long long lastRead  = 0;
        OSAL_MutexLock(get_mutexobj());
        status   = runStatus();
        lastRead = last_read;
        OSAL_MutexUnlock(get_mutexobj());

        /* 两类异常：① 状态不是 OPEN_SUCCESS；② 状态正常但长时间没有新帧（读帧停滞） */
        if (status != CHANNEL_OPEN_SUCCESS)
        {
            abnormal_cnt_++;
            printf("[%s][%s][%s] current status = [%d] abnormal=%d/%d", s_key, s_sub, channel_info.dev_name,
                   status, abnormal_cnt_, recover_threshold_);
        }
        else if (lastRead > 0 && (cm_tick_milli() - lastRead) > stall_timeout_ms_)
        {
            abnormal_cnt_++;
            printf("[%s][%s][%s] no frame for %llums, abnormal=%d/%d", s_key, s_sub, channel_info.dev_name,
                   cm_tick_milli() - lastRead, abnormal_cnt_, recover_threshold_);
        }
        else
        {
            /* 一切正常，异常计数清零；恢复次数也随稳定时长缓慢回退，
             * 避免一次偶发故障把重试预算永久耗光 */
            if (abnormal_cnt_ > 0)
            {
                abnormal_cnt_ = 0;
            }
            if (recover_cnt_ > 0)
            {
                recover_cnt_--;
            }
        }

        if (abnormal_cnt_ >= recover_threshold_)
        {
            if (!auto_recover_)
            {
                /* 只观测、不动作：驱动侧修好 sensor 复位后打开开关即可 */
                printf("[%s][%s][%s] channel abnormal but auto recover disabled, keep watching", s_key, s_sub,
                       channel_info.dev_name);
            }
            else if (recover_cnt_ >= recover_max_)
            {
                printf("[%s][%s][%s] recover reach max [%d], give up", s_key, s_sub, channel_info.dev_name,
                       recover_max_);
            }
            else
            {
                recover_cnt_++;
                printf("[%s][%s][%s] recover start, times=%d/%d", s_key, s_sub, channel_info.dev_name,
                       recover_cnt_, recover_max_);
                OSAL_MutexLock(get_mutexobj());
                doRecover();
                OSAL_MutexUnlock(get_mutexobj());
                abnormal_cnt_ = 0;
            }
        }

        sleep(recover_interval_);
    }
    return YQSL_E_OK;
}

int MultimediaServiceVideoBase::open(const char *dev)
{
    if (!dev)
    {
        return YQSL_E_ARGS;
    }

    printf("[%s][%s][%s] open dev [%s]", s_key, s_sub, channel_info.dev_name, dev);
    memset(&channel_info.run_status, 0, sizeof(channel_info.run_status));
    memset(&channel_info.error_status, 0, sizeof(channel_info.error_status));
    memset(&channel_info.buf, 0, sizeof(channel_info.buf));
    channel_info.ctx.priv_data = NULL;
    strncpy(channel_info.dev_name, dev, sizeof(channel_info.dev_name) - 1);
    channel_info.dev_name[sizeof(channel_info.dev_name) - 1] = '\0';
    return 0;
}

int MultimediaServiceVideoBase::openInner()
{
    int result = 0;
    printf("[%s][%s] open dev [%s] opened fd = [%d]", s_key, s_sub, channel_info.dev_name, video_open_fd);

    if (videoImpl == NULL)
    {
        videoImpl = getVideoImpl();
    }
    if (!videoImpl)
    {
        printf("[%s][%s][%s] video impl not find", s_key, s_sub, channel_info.dev_name);
        statusChanged(CHANNEL_IMPL_NOT_FIND);
        return CHANNEL_IMPL_NOT_FIND;
    }

    if (channel_info.run_status.is_open)
    {
        return CHANNEL_ALREADY_OPEN;
    }

    /* 计数必须在「确认要打开」之后加，否则重复 open 会白增一次 */
    video_open_fd++;

    result = videoImpl->open(&channel_info.ctx, channel_info.dev_name);
    if (result >= 0)
    {
        channel_info.run_status.is_open  = 1;
        channel_info.run_status.is_close = 0;
        result                           = setArgs(&channelArgs);
        if (result >= 0)
        {
            result = startInner();
        }
    }

    if (result >= 0)
    {
        statusChanged(CHANNEL_OPEN_SUCCESS);
        last_read  = cm_tick_milli();
        use_times  = 0;
        read_count = 0;
        print_last = cm_tick_milli();
        frame_count = 0;
        startThread();
    }
    else
    {
        /* 把内部真实错误码打出来：CHANNEL_OPEN_FAILED 只是聚合状态，
         * 直接返回它会让 -3/-4/-5/-6/-7 这类可实现定位的码全部丢失 */
        printf("[%s][%s][%s] open failed, inner result = [%d]", s_key, s_sub,
               channel_info.dev_name, result);
        statusChanged(CHANNEL_OPEN_FAILED);
        stopInner();
        closeInner();
        /* 打开失败也要把守护线程拉起来：否则这类故障永远没人重试，
         * 看门狗会按 threshold 累计异常并在允许时执行重建 */
        startThread();
    }
    return runStatus();
}

int MultimediaServiceVideoBase::startThread()
{
    if (pid <= 0)
    {
        exited = false;
        int ret = pthread_create(&pid, NULL, thread_func, this);
        if (ret != 0)
        {
            printf("[%s][%s][%s] thread create failed ret=[%d]", s_key, s_sub, channel_info.dev_name, ret);
            return YQSL_E_RUN_FAIL;
        }
    }
    return 0;
}

int MultimediaServiceVideoBase::stopThread()
{
    if (pid <= 0)
    {
        return 0;
    }

    /* 先置退出标志，再 join：线程循环条件是 !exited，
     * 每次循环最多阻塞 400ms（或状态异常时 1s），join 不会久等 */
    exited = true;
    int ret = pthread_join(pid, NULL);
    if (ret != 0)
    {
        printf("[%s][%s][%s] thread join failed ret=[%d]", s_key, s_sub, channel_info.dev_name, ret);
        return YQSL_E_RUN_FAIL;
    }
    pid = 0;
    return 0;
}

int MultimediaServiceVideoBase::setArgs(VideoChannelArgs *args)
{
    if (!args)
    {
        return YQSL_E_ARGS;
    }

    channelArgs = *args;
    if (!channel_info.run_status.is_open || !videoImpl)
    {
        return YQSL_E_NOT_INIT;
    }

    int ret = videoImpl->ctrl(&channel_info.ctx, CAM_CMD_SET_FPS, &args->fps);
    if (ret < 0)
    {
        return YQSL_E_RUN_FAIL;
    }
    ret = videoImpl->ctrl(&channel_info.ctx, CAM_CMD_SET_WIDTH, &args->width);
    if (ret < 0)
    {
        return YQSL_E_RUN_FAIL - 1;
    }
    ret = videoImpl->ctrl(&channel_info.ctx, CAM_CMD_SET_HEIGHT, &args->height);
    if (ret < 0)
    {
        return YQSL_E_RUN_FAIL - 2;
    }
    ret = videoImpl->ctrl(&channel_info.ctx, CAM_CMD_SET_PIX_FORMAT, &args->pix_format);
    if (ret < 0)
    {
        return YQSL_E_RUN_FAIL - 3;
    }
    ret = videoImpl->ctrl(&channel_info.ctx, CAM_CMD_SET_DST_PIX_FORMAT, &args->dst_format);
    if (ret < 0)
    {
        return YQSL_E_RUN_FAIL - 4;
    }
    ret = videoImpl->ctrl(&channel_info.ctx, CAM_CMD_SET_DST_BITRATE, &args->bitrate);
    if (ret < 0)
    {
        return YQSL_E_RUN_FAIL - 5;
    }
    ret = videoImpl->ctrl(&channel_info.ctx, CAM_CMD_SET_DST_RC_MODE, args->rc_mode);
    if (ret < 0)
    {
        return YQSL_E_RUN_FAIL - 6;
    }

    return videoImpl->ctrl(&channel_info.ctx, CAM_CMD_SET_CHANNEL, &args->index);
}

int MultimediaServiceVideoBase::set(int cmd, void *args)
{
    if ((channel_info.run_status.is_start) || (channel_info.run_status.is_set))
    {
        return CHANNEL_ALREADY_SET;
    }
    if (runStatus() != CHANNEL_OPEN_SUCCESS || !videoImpl)
    {
        return YQSL_E_RUN_FAIL;
    }

    int ret = videoImpl->ctrl(&channel_info.ctx, cmd, args);
    if (ret >= 0)
    {
        channel_info.run_status.is_set = 1;
    }
    return ret;
}

int MultimediaServiceVideoBase::startInner()
{
    if (!videoImpl)
    {
        return CHANNEL_IMPL_NOT_FIND;
    }
    if (channel_info.run_status.is_start || channel_info.run_status.is_open == 0)
    {
        return CHANNEL_ALREADY_START;
    }

    int result = videoImpl->start(&channel_info.ctx);
    if (result >= 0)
    {
        channel_info.run_status.is_start = 1;
        channel_info.run_status.is_stop  = 0;
    }
    else
    {
        printf("[%s][%s][%s] start fail [%d]", s_key, s_sub, channel_info.dev_name, result);
    }
    exited = false;
    return result;
}

int MultimediaServiceVideoBase::stopInner()
{
    if (channel_info.run_status.is_stop)
    {
        return CHANNEL_ALREADY_STOP;
    }

    channel_info.run_status.is_stop  = 1;
    channel_info.run_status.is_start = 0;
    return 0;
}

int MultimediaServiceVideoBase::closeInner()
{
    if (!videoImpl)
    {
        return CHANNEL_IMPL_NOT_FIND;
    }

    int result = 0;
    video_open_fd--;
    if (video_open_fd < 0)
    {
        video_open_fd = 0;
    }
    if (video_open_fd)
    {
        return CHANNEL_OPEN_FD_CONTINUE;
    }

    if (channel_info.run_status.is_close || (channel_info.run_status.is_open == 0))
    {
        return CHANNEL_ALREADY_CLOSE;
    }

    if (channel_info.run_status.is_start == 1 || channel_info.run_status.is_stop == 1)
    {
        /* 必须同时看 is_stop：stop() 会先调 stopInner() 把 is_start 清 0，
         * 只看 is_start 会让 impl->stop() 永远不被调用，VI/VENC/ISP 全部泄漏 */
        videoImpl->stop(&channel_info.ctx);
    }
    result = videoImpl->close(&channel_info.ctx);
    if (result >= 0)
    {
        channel_info.run_status.is_close = 1;
        channel_info.run_status.is_start = 0;
        channel_info.run_status.is_stop  = 0;
        channel_info.run_status.is_set   = 0;
        channel_info.run_status.is_open  = 0;
    }
    return result;
}

int MultimediaServiceVideoBase::close()
{
    return closeInner();
}

int MultimediaServiceVideoBase::start()
{
    if (channel_info.run_status.is_open == 0)
    {
        return openInner();
    }
    return startInner();
}

int MultimediaServiceVideoBase::stop()
{
    int ret = stopInner();
    int close_ret = closeInner();
    /* 通道已停，守护线程没有继续存在的意义，这里就把它收掉；
     * 万一将来 stop 后还要再 start，startThread() 会重新拉起（pid 已归零） */
    stopThread();
    return ret < 0 ? ret : close_ret;
}

int MultimediaServiceVideoBase::requestKeyFrame()
{
    if (!videoImpl)
    {
        return CHANNEL_IMPL_NOT_FIND;
    }
    return videoImpl->ctrl(&channel_info.ctx, CAM_CMD_SET_KEY_FRAME, NULL);
}

int MultimediaServiceVideoBase::read(CamBuf *buf)
{
    if (!buf)
    {
        return YQSL_E_ARGS;
    }
    if ((channel_info.run_status.is_open == 0) || (channel_info.run_status.is_start == 0))
    {
        return CHANNEL_READ_NOT_OPEN_START;
    }
    if (runStatus() != CHANNEL_OPEN_SUCCESS || !videoImpl)
    {
        return YQSL_E_RUN_FAIL;
    }

    unsigned long long read_before = cm_tick_milli();
    int ret = videoImpl->read(&channel_info.ctx, buf);
    unsigned long long read_after = cm_tick_milli();
    use_times += (read_after - read_before);

    if (read_count++ >= 100)
    {
        if (readAvg.size() >= 100)
        {
            readAvg.pop_front();
        }
        readAvg.push_back(use_times / 100);
        use_times  = 0;
        read_count = 0;
    }

    if (++frame_count >= 65535)
    {
        frame_count = 0;
    }

    if (ret < 0)
    {
        unsigned long long now = cm_tick_milli();
        readError();
        if (now - last_read > 5 * 1000)
        {
            statusChanged(CHANNEL_READ_FAILED);
            sleep(1);
        }
        return ret;
    }

    last_read = cm_tick_milli();
    if (last_read - print_last > 60 * 1000 && !readAvg.empty())
    {
        printf("[%s][%s][%s] read result use time avg=[%llu]ms frame_count=[%d]", s_key, s_sub,
               channel_info.dev_name, readAvg.back(), frame_count);
        frame_count = 0;
        print_last  = last_read;
    }
    return ret;
}
