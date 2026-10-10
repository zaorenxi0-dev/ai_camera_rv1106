#ifndef MULTIMEDIA_SERVICE_VIDEO_BASE_H
#define MULTIMEDIA_SERVICE_VIDEO_BASE_H


#include <list>
#include <string>
#include <vector>
#include "multimedia_service_define.h"
#include "jemalloc/jemalloc.h"


class MultimediaServiceVideoBase : public VideoBaseInterface
{
public:
    MultimediaServiceVideoBase(const char* impl_name);
    virtual ~MultimediaServiceVideoBase();
     /* ===== 对外接口（重写自 VideoBaseInterface）===== */

    /**
     * @brief 打开视频设备（仅初始化状态，不打开底层驱动）
     * @param dev 设备节点路径（如 /dev/video0）
     *
     * 实际打开底层驱动在 openInner() 中执行。
     */
    int open(const char *dev) override;

    /**
     * @brief 关闭视频设备（空实现，实际关闭在 closeInner()）
     */
    int close() override;

    /**
     * @brief 启动视频流（调用 openInner 完成打开+启动）
     */
    int start() override;

    /**
     * @brief 停止视频流（调用 stopInner + closeInner）
     */
    int stop() override;        

    /**
     * @brief 设置通道参数（当前空实现，实际参数在 openInner 时通过 setArgs 统一设置）
     */
    int  set(int cmd, void *args) override;

    /**
     * @brief 读取一帧视频数据
     * @param buf 输出缓冲区（填充帧数据）
     * @return 0=成功，负数=错误码
     *
     * 核心方法：调用底层 videoImpl->read()，统计耗时，检测连续读帧错误。
     */
    int  read(CamBuf *buf) override;

    /**
     * @brief 状态变更通知
     * @param st 新状态（见 ChannelStatus 枚举）
     *
     * 更新内部状态并回调 parent->privNotify() 通知上层。
     */
    void statusChanged(int st) override;

    /**
     * @brief 请求关键帧（I帧）
     */
    int  requestKeyFrame() override;

  protected:
    /* ===== 钩子方法（子类可重写扩展）===== */

    /**
     * @brief 后台线程主循环（子类可重写实现自定义逻辑）
     *
     * 当前基类实现：检测状态，非 OPEN_SUCCESS 则 sleep(30) 重试。
     */
    virtual int          run();

    /**
     * @brief 获取底层视频驱动实现（纯虚函数，必须由子类实现）
     * @return CamVideoImpl* 驱动函数指针表（V4L2 / MIPI 等）
     *
     * 子类返回对应的静态驱动实例，如：
     *   - V4L2: &cam_video_impl_v4l2
     *   - MIPI: &cam_video_impl_mipi
     */
    virtual CamVideoImpl *getVideoImpl() = 0;

    /**
     * @brief 创建后台线程
     * @return 0=成功，负数=失败
     */
    int                  startThread();

    /**
     * @brief 设置通道参数到底层驱动
     * @paramargs 视频通道参数（fps/width/height/pix_format/bitrate/rc_mode 等）
     * @return 0=成功，负数=错误码
     *
     * 通过 videoImpl->ctrl() 逐项设置，任一参数失败立即返回错误码。
     */
    int                  setArgs(camsvc::VideoChannelArgs *args);

    friend void         *thread_func(void *args);

    /**
     * @brief 重新打开操作（状态机自动恢复）
     *
     * 根据当前状态决定执行：
     *   - READ_FAILED: stop→close→重新 open→openInner→startInner
     *   - OPEN_FAILED / START_FAILED / IMPL_NOT_FIND: 重新打开流程
     */
    int                  reOpenOperation();

    /* ===== 内层操作（模板方法，子类可重写）===== */

    /**
     * @brief 内层启动（调用 videoImpl->start() 开始取流）
     */
    int startInner();

    /**
     * @brief 内层停止（标记停止状态）
     */
    int stopInner();

    /**
     * @brief 内层打开（获取 videoImpl → 引用计数++ → videoImpl->open() → setArgs() → startInner() → startThread()）
     */
    int openInner();

    /**
     * @brief 内层关闭（引用计数-- → videoImpl->stop() → videoImpl->close() → 重置状态）
     */
    int closeInner();

    /**
     * @brief 读帧错误计数
     * @return 当前错误计数（0-65535 后自动进位 readErrorCycle）
     */
    int readError();

    /** 实现名称（如 "v4l2" / "mipi"），用于日志 */
    std::string implName;

  private:
    /* ===== 统计与监控 ===== */

    /** 最近 100 次读帧平均耗时队列（毫秒），用于性能监控 */
    std::list<unsigned long long> readAvg;

    /** 当前读帧错误计数（0-65535） */
    int                           readErrorCount;

    /** 读帧错误周期数（readErrorCount 溢出后 +1） */
    int                           readErrorCycle;

    /** 上次成功读帧时间戳（毫秒） */
    unsigned long long            last_read;

    /** 累计读帧耗时（每 100 帧求平均后清零） */
    unsigned long long            use_times;

    /** 当前周期已读帧数（达到 100 后计算平均） */
    int                           read_count;

    /** 上次打印性能日志时间戳（毫秒，每 60 秒打印一次） */
    unsigned long long print_last;

    /** 帧计数器（每 60 秒打印一次后清零，用于统计帧率） */
    int                frame_count;
};






#endif
