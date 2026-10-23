#ifndef __MULTIMEDIA_SERVICE_DEFINE_H__
#define __MULTIMEDIA_SERVICE_DEFINE_H__

#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include "video.h"
#include "osal_mutex.h"
#include "service_base.h"
#include "video_interface.h"
#include <list>

#include "multimedia_service_exception.h"

namespace camsvc
{
typedef enum
{
    CHANNEL_IMPL_NOT_FIND = -100,
    CHANNEL_OPEN_FAILED,
    CHANNEL_CLOSE_FAILED,
    CHANNEL_START_FAILED,
    CHANNEL_STOP_FAILED,
    CHANNEL_READ_FAILED,
    CHANNEL_NOT_EXISTENT,
    CHANNEL_READ_NULL,

    CHANNEL_ALREADY_OPEN        = 1,
    CHANNEL_ALREADY_CLOSE       = 2,
    CHANNEL_ALREADY_START       = 3,
    CHANNEL_ALREADY_STOP        = 4,
    CHANNEL_ALREADY_SET         = 5,
    CHANNEL_READ_NOT_OPEN_START = 6,
    CHANNEL_OPEN_FD_CONTINUE    = 7,
    CHANNEL_START_FD_CONTINUE   = 8,
    CHANNEL_WRITE_FAILED,
    CHANNEL_PLAYER_FAILED,
    CHANNEL_PLAYER_STATE_FAILED,
    CHANNEL_OPEN_SUCCESS,
} ErrorStatus;

struct VideoChannelRunStatus
{
    int16_t status;  ///< 汇总运行状态（0=未初始化，1=正常运行，其它=异常）
    struct
    {
        /* 以下 5 个 bit 分别标记通道是否执行过对应操作 */
        uint16_t is_open  : 1,  ///< bit0: 通道是否已 open
                 is_close : 1,  ///< bit1: 通道是否已 close
                 is_start : 1,  ///< bit2: 通道是否已 start（开启流）
                 is_stop  : 1,  ///< bit3: 通道是否已 stop
                 is_set   : 1;  ///< bit4: 通道参数是否已 set
    };
};

struct VideoChannelErrorStatus
{
    int16_t status;  ///< 汇总错误状态
    struct
    {
        uint16_t open_error   : 1,  ///< bit0: open 操作出错
                 impl_not_find: 1,  ///< bit1: 底层实现未找到
                 start_error  : 1,  ///< bit2: start 操作出错
                 stop_error   : 1,  ///< bit3: stop 操作出错
                 close_error  : 1,  ///< bit4: close 操作出错
                 read_error   : 1;  ///< bit5: read 操作出错
    };
};

typedef struct
{
    char                    dev_name[256];     ///< 设备节点路径，如 "/dev/video0"、"/dev/video-one"
    char                    dev_impl[256];     ///< 底层实现类型，如 "v4l2-normal"、"rk-rv1106"、"rk-venc"
    VideoChannelRunStatus   run_status;         ///< 运行状态（open/start/stop等位域）
    VideoChannelErrorStatus error_status;       ///< 错误状态（各类错误位域）
    CamBuf              buf;                ///< 当前读到的一帧数据（指针+长度）
    CamContext          ctx;                ///< 底层实现上下文（priv_data 指向 V4L2 私有数据）
    int                     index;              ///< 通道索引号（0,1,2...），在 video_mp 中的 key
} VideoChannelInfo;

typedef struct
{
    CAM_FMT pix_format; ///< 源像素格式（H264/H265/YUYV/JPEG）
    uint32_t        width;      ///< 图像宽度，如 1920
    uint32_t        height;     ///< 图像高度，如 1080
    uint8_t         fps;        ///< 帧率，如 30
    char            rc_mode[8]; ///< 码率控制模式："cbr"(固定码率)/"vbr"(可变码率)/"avbr"(自适应可变码率)
    CAM_FMT dst_format; ///< 目标输出格式（硬件编码时的输出编码格式）
    int             bitrate;    ///< 目标码率（bps），如 2048000 表示 2Mbps
    int             index;      ///< 通道索引号
} VideoChannelArgs;

typedef enum
{
    SET_WIDTH = 0,     ///< 设置图像宽度
    SET_HEIGHT,        ///< 设置图像高度
    SET_FPS,           ///< 设置帧率
    SET_FORMAT,        ///< 设置源像素格式
    SET_DST_FORMAT,    ///< 设置目标输出格式
    SET_RC_MODE,       ///< 设置码率控制模式
    SET_BITRATE,       ///< 设置码率
    SET_INDEX,         ///< 设置通道索引号
    SET_ALL,           ///< 设置全部参数
    REQUEST_KEY_FRAME, ///< 请求关键帧（I帧）
} MultimediaVideoSetType;

typedef struct
{
    int                    index;        ///< 通道索引号（对应 video_mp 的 key）
    VideoChannelInfo       channelInfo;  ///< 通道信息（输入+输出，ctl 后回填状态和数据）
    VideoChannelArgs       channelArgs;  ///< 通道参数（set 命令的输入参数）
    MultimediaVideoSetType set_type;     ///< 设置类型（仅 SET 命令有效，指定改哪个参数）
} MultimediaVideoChannel;



/* ========================================================================
 * 6. VideoDeal - 视频异常恢复动作
 *
 * 由 error_deal() 方法返回，告诉上层应该执行哪种恢复操作。
 * 恢复策略：连续错误达到阈值 → 先 rebind（重新绑定驱动）→ 再 reset（复位USB/OTG/MIPI）→ 最后 REBOOT
 * ======================================================================== */
typedef enum
{
    USB_NORMAL,      ///< USB 正常，无需处理
    VIDEO_CONTINUE,  ///< 继续检测（错误次数未达阈值，或恢复中）
    OTG_RESET = -30, ///< OTG 接口复位（针对 /dev/video-two）
    USB_RESET,       ///< USB 接口复位（针对 /dev/video-one）
    MIPI_RESET,      ///< MIPI 接口复位（针对 rk-venc 硬编码通道）
    OTG_REBIND,      ///< OTG 重新绑定驱动（unbind + bind）
    USB_REBIND,      ///< USB 重新绑定驱动
    MIPI_REBIND,     ///< MIPI 重新绑定驱动
    REBOOT,          ///< 整机重启（最后手段）
} VideoDeal;

/* ========================================================================
 * 7. SetType - 状态设置/重置类型
 *
 * 用于 set_error_status() 和 set_run_status() 方法，
 * 决定是置位（SET）还是清零（RESET）对应的状态位。
 * ======================================================================== */
typedef enum
{
    V_CHANNEL_SET   = 1,  ///< 设置视频通道状态位（置 1）
    V_CHANNEL_RESET = 0,  ///< 重置视频通道状态位（清 0）

    A_CHANNEL_SET   = 1,  ///< 设置音频通道状态位
    A_CHANNEL_RESET = 0,  ///< 重置音频通道状态位
} SetType;





}



class VideoBaseInterface
{
public:
    VideoBaseInterface()
    {
        OSAL_MutexInit(&video_mutex_obj);
        memset(&channel_info, 0, sizeof(camsvc::VideoChannelInfo));  ///< 清零通道信息
        video_open_fd   = 0;     ///< open 操作的 fd 计数（用于判断是否已打开）
        video_start_fd  = 0;     ///< start 操作的 fd 计数
        error_check_cnt = 0;     ///< 连续错误计数（达到 interval_count 触发 error_deal 恢复）
        rebind_flag     = 0;     ///< rebind 阶段标志（1=已执行过 rebind）
        reset_flag      = 0;     ///< reset 阶段标志（1=已执行过 reset）
        interval_count  = 0;     ///< 错误恢复阈值（连续错误多少次后触发恢复）
        service_parent_ = nullptr;
        videoImpl       = nullptr;
        pid             = 0;
        exited          = false;
    }

    /* ── 析构函数：停止守护线程 + 销毁互斥锁 ── */
    virtual ~VideoBaseInterface()
    {
        /* ★ 必须先让守护线程退出，再销毁锁和对象。
         * 原实现只销毁锁：若调用方没走 stop() 就 delete（如 deinit 直接析构），
         * 线程下一轮仍会访问已释放的对象与已销毁的锁（use-after-free） */
        exited = true;
        if (pid > 0)
        {
            pthread_join(pid, NULL);
            pid = 0;
        }
        OSAL_MutexDestroy(&video_mutex_obj);
    };

    /**
     * @brief 打开视频设备
     * @param dev 设备节点路径，如 "/dev/video0"
     * @return 0=成功，<0=失败（错误码见 ErrorStatus）
     */
    virtual int open(const char *dev) = 0;

    /**
     * @brief 关闭视频设备
     * @return 0=成功，<0=失败（错误码见 ErrorStatus）
     */
    virtual int close(void) = 0;

    /**
     * @brief 启动视频流
     * @return 0=成功，<0=失败（错误码见 ErrorStatus）
     */
    virtual int start(void) = 0;

    /**
     * @brief 停止视频流
     * @return 0=成功，<0=失败（错误码见 ErrorStatus）
     */
    virtual int stop(void) = 0;

    /**
     * @brief 设置通道参数
     * @param cmd  设置命令（见 CAM_CMD 或 MultimediaVideoSetType）
     * @param args 参数指针
     * @return 0=成功，<0=失败
     */
    virtual int set(int cmd, void *args) = 0;


    /**
     * @brief 读取一帧视频数据
     * @param buf 输出缓冲区（填充数据指针和长度）
     * @return 1=读到数据，0=无数据（超时），<0=出错
     */
    virtual int read(CamBuf *buf) = 0;

    /**
     * @brief 请求关键帧（I帧）
     * @return 0=成功，-1=不支持（默认实现返回-1）
     *
     * 用于硬编码通道，强制编码器输出一个 I 帧，
     * 确保远程拍照/录像的第一帧是关键帧。
     */
    virtual int requestKeyFrame()
    {
        return -1;
    };

    /**
     * @brief 设置父服务指针
     * @param _parent 指向 SvcBase 的指针
     *
     * 通道对象可以通过 parent 回调服务层的方法。
     */
    void setParent(ServiceBaseInterface *_service_base_interface)
    {
        service_parent_ = _service_base_interface;
    }


    /**
     * @brief 状态变化回调（纯虚函数）
     * @param st 新状态
     *
     * 当通道运行状态发生变化时调用，子类实现具体逻辑。
     */
    virtual void statusChanged(int st) = 0;

    /** @brief 获取通道运行状态（汇总值）*/
    int runStatus()
    {
        return channel_info.run_status.status;
    }


    /**
     * @brief 获取通道运行状态（汇总值）
     * @return run_status.status
     *
     * 与 runStatus() 功能相同，保留两个方法是历史兼容。
     */
    int get_run_status(void)
    {
        return channel_info.run_status.status;
    };

 /**
     * @brief 设置或清除通道错误状态位
     * @param status     要修改的错误状态结构体
     * @param type       V_CHANNEL_SET=置位，V_CHANNEL_RESET=清零
     * @param dev_status 要操作的错误类型（见 ErrorStatus）
     * @return 始终返回 0
     *
     * 根据 dev_status 找到对应的位域字段，然后置 1 或清 0。
     * 例如：set_error_status(&status, V_CHANNEL_SET, CHANNEL_OPEN_FAILED)
     *       会把 status->open_error 设为 1。
     */
    int set_error_status(camsvc::VideoChannelErrorStatus *status,
                         camsvc::SetType type, camsvc::ErrorStatus dev_status)
    {
        if (type == camsvc::V_CHANNEL_SET) // 设置运行状态信息（置 1）
        {
            switch (dev_status)
            {
            case camsvc::CHANNEL_IMPL_NOT_FIND:
                status->impl_not_find = 1;  ///< 标记"实现未找到"错误
                break;
            case camsvc::CHANNEL_OPEN_FAILED:
                status->open_error = 1;     ///< 标记"打开失败"错误
                break;
            case camsvc::CHANNEL_CLOSE_FAILED:
                status->close_error = 1;    ///< 标记"关闭失败"错误
                break;
            case camsvc::CHANNEL_START_FAILED:
                status->start_error = 1;    ///< 标记"启动失败"错误
                break;
            case camsvc::CHANNEL_STOP_FAILED:
                status->stop_error = 1;     ///< 标记"停止失败"错误
                break;
            case camsvc::CHANNEL_READ_FAILED:
                status->read_error = 1;     ///< 标记"读帧失败"错误
                break;

            default:
                break;
            }
        }
        else if (type == camsvc::V_CHANNEL_RESET) // reset（清 0）
        {
            switch (dev_status)
            {
            case camsvc::CHANNEL_IMPL_NOT_FIND:
                status->impl_not_find = 0;  ///< 清除"实现未找到"错误
                break;
            case camsvc::CHANNEL_OPEN_FAILED:
                status->open_error = 0;     ///< 清除"打开失败"错误
                break;
            case camsvc::CHANNEL_CLOSE_FAILED:
                status->close_error = 0;    ///< 清除"关闭失败"错误
                break;
            case camsvc::CHANNEL_START_FAILED:
                status->start_error = 0;    ///< 清除"启动失败"错误
                break;
            case camsvc::CHANNEL_STOP_FAILED:
                status->stop_error = 0;     ///< 清除"停止失败"错误
                break;
            case camsvc::CHANNEL_READ_FAILED:
                status->read_error = 0;     ///< 清除"读帧失败"错误
                break;

            default:
                break;
            }
        }
        return 0;
    }


    /** @brief 错误计数 +1（每次读帧失败等错误时调用）*/
    void ticks_error_count()
    {
        error_check_cnt++;
    }

    /** @brief 清零错误计数（读取成功时调用）*/
    void clear_error_count()
    {
        error_check_cnt = 0;
    }

    /*
     * @brief 设置错误恢复的阈值次数
     * @param count 连续错误达到多少次后触发恢复
     */
    void set_error_deal_rule(int count)
    {
        interval_count = count;
    }

    /**
     * @brief 错误恢复决策（核心方法）
     * @return VideoDeal 枚举值，告诉上层应该执行什么恢复动作
     *
     * 【恢复策略】三级递进：
     *   第1级：连续错误达到 interval_count → 执行 REBIND（重新绑定驱动）
     *   第2级：REBIND 后仍然错误 → 执行 RESET（复位 USB/OTG/MIPI 接口）
     *   第3级：RESET 后仍然错误 → 执行 REBOOT（整机重启，最后手段）
     *
     * 根据 dev_name 区分不同硬件的复位方式：
     *   /dev/video-one → USB_RESET / USB_REBIND
     *   /dev/video-two → OTG_RESET / OTG_REBIND
     *   rk-venc        → MIPI_RESET / MIPI_REBIND
     *
     * 每次执行恢复动作后清零 error_check_cnt，重新计数。
     */
    int error_deal()
    {
        if (error_check_cnt > interval_count) // 错误次数达到阈值
        {
            if (reset_flag)  // 已经 reset 过仍然出错 → 整机重启
            {
                reset_flag      = 0;
                error_check_cnt = 0;
                return camsvc::REBOOT;  ///< 最后手段：重启整机
            }

            if (rebind_flag)  // 已经 rebind 过仍然出错 → 执行 reset
            {
                rebind_flag     = 0;
                error_check_cnt = 0;

                reset_flag = 1;  ///< 标记已进入 reset 阶段
                if (strcmp(channel_info.dev_name, "/dev/video-one") == 0)
                {
                    return camsvc::USB_RESET;  ///< 复位 USB 接口
                }

                if (strcmp(channel_info.dev_name, "/dev/video-two") == 0)
                {
                    return camsvc::OTG_RESET;  ///< 复位 OTG 接口
                }

                if (strcmp(channel_info.dev_name, "rk-venc") == 0)
                {
                    return camsvc::MIPI_RESET;  ///< 复位 MIPI 接口
                }

                return camsvc::VIDEO_CONTINUE;  ///< 未知设备，继续检测
            }

            // 第一次达到阈值 → 执行 rebind
            rebind_flag     = 1; // 标记已进入 rebind 阶段
            error_check_cnt = 0;

            if (strcmp(channel_info.dev_name, "/dev/video-one") == 0)
            {
                return camsvc::USB_REBIND;  ///< USB 重新绑定驱动
            }

            if (strcmp(channel_info.dev_name, "/dev/video-two") == 0)
            {
                return camsvc::OTG_REBIND;  ///< OTG 重新绑定驱动
            }

            if (strcmp(channel_info.dev_name, "rk-venc") == 0)
            {
                return camsvc::MIPI_REBIND;  ///< MIPI 重新绑定驱动
            }
        }

                return camsvc::VIDEO_CONTINUE; // 错误次数未达阈值，继续检测
    }


    /** @brief 获取通道互斥锁指针（用于外部加锁，如 ctl 命令中）*/
    T_MutexObj *get_mutexobj()
    {
        return &video_mutex_obj;
    }


    /* ===== 通道参数管理 ===== */

    /**
     * @brief 设置通道参数
     * @param _args 新的通道参数
     */
    void setChannelArgs(camsvc::VideoChannelArgs *_args)
    {
        channelArgs = *_args;
    }


    /**
     * @brief 获取通道参数
     * @return 当前通道参数的拷贝
     */
    camsvc::VideoChannelArgs getChannelArgs()
    {
        return channelArgs;
    }


    /* ===== 帧缓冲区队列 ===== */

    /**
     * @brief 将一帧数据压入缓冲区队列
     * @param buf 要缓存的帧数据
     * @return 当前队列长度
     *
     * 队列最多保留 5 帧，超过时丢弃最旧的一帧（pop_front）。
     * 用于远程拍照等场景：采集线程持续压帧，远程拍照请求时取最新帧。
     */
    int pushBuf(CamBuf *buf)
    {
        if (videoBufList.size() >= 5)
        {
            videoBufList.pop_front();  ///< 队列满，丢弃最旧帧
        }
        videoBufList.push_back(*buf);   ///< 新帧加到队尾
        return videoBufList.size();
    }

    /**
     * @brief 从队列头部取出一帧（最旧的帧）
     * @return 取出的帧数据
     */
    CamBuf popFirst()
    {
        CamBuf *buf = &videoBufList.front();
        videoBufList.pop_front();
        return *buf;
    }

    /**
     * @brief 从队列尾部取出一帧（最新的帧）
     * @return 取出的帧数据
     *
     * 远程拍照时用这个方法获取最新采集的一帧。
     */
    CamBuf popLast()
    {
        CamBuf *buf = &videoBufList.back();
        videoBufList.pop_back();
        return *buf;
    }

    /*
     * @brief 获取异常处理器引用
     * @return SvcException 引用
     *
     * 异常处理器用于记录和管理通道的各类异常（设备丢失、读帧失败等），
     * 支持异常计数、状态反转检测、时间戳记录等功能。
     */
    SvcException &getExceptionHandler()
    {
        return multimediaServiceException;
    }







    camsvc::ErrorStatus      error_status;   
    camsvc::VideoChannelInfo channel_info; ///< 通道信息
    T_MutexObj  video_mutex_obj; 
    int   video_open_fd;      ///< open 操作计数（>0 表示已打开）
    int   video_start_fd;     ///< start 操作计数（>0 表示已启动流）

    int error_check_cnt; ///< 连续错误计数（达到 interval_count 触发 error_deal 恢复）
    int rebind_flag;     ///< rebind 阶段标志（1=已执行过 rebind）
    int reset_flag;      ///< reset 阶段标志（1=已执行过 reset）
    int interval_count;  ///< 错误恢复阈值（连续错误多少次后触发恢复）




    

protected:

    ServiceBaseInterface* service_parent_;
    camsvc::VideoChannelArgs channelArgs;  ///< 通道参数（分辨率/帧率/码率等）
    CamVideoImpl *videoImpl;
     SvcException multimediaServiceException;  ///< 异常处理器

    pthread_t              pid;          ///< 采集线程 ID
    bool                   exited;       ///< 线程退出标志
      std::list<CamBuf> videoBufList; ///< 帧缓冲区队列（最多5帧，用于远程拍照）


};





#endif
