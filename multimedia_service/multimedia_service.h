#ifndef __MULTIMEDIA_SERVICE_H__
#define __MULTIMEDIA_SERVICE_H__

#include <string.h>
#include <stdint.h>
#include <iostream>
#include <vector>
#include <list>
#include "multimedia_service_define.h"
#include <map>

#include "multimedia_service_error_record.h"



#include "service_base.h"
#include "MultimediaServiceInterface.h"
class MultimediaService : public MultimediaServiceInterface
{
public:
    MultimediaService();
    virtual ~MultimediaService();

    /* ===== 错误检查配置结构体 =====
     * 对应 JSON 配置文件中的 error_check 节点，
     * 控制异常检测的行为和自恢复策略。
     */
    // Ctrl错误类型
    typedef struct
    {
        int enable;                // 是否启用摄像头异常自恢复（1=启用，0=关闭）
        int duration;              // 异常检测周期（秒，默认30秒）
        int interval_count;        // 连续错误多少次触发恢复（默认3次）
        int interval_time;         // 检测间隔（秒，默认3秒）
        int cycle_count_max;       // 恢复循环最大次数（默认3次）
        int reboot_enable;         // 是否启用软重启（1=启用）
        int reboot_count_max;      // 软重启最大次数（默认3次）
        int poweroff_enable;      // 是否启用硬重启（1=启用）
        int poweroff_count_max;    // 硬重启最大次数（默认3次）
        int error_action_duration; // 错误动作持续时间（秒，默认43200=12小时）
        int error_clear_duration;  // 错误清除持续时间（秒，默认600=10分钟）
    } multimedia_service_json_config;

    int init() override;
    int deinit() override;
    int start() override;
    int stop() override;
    int ctl(int type, void *args) override;
    int run() override;
    int audioStart() override;
    int audioStop() override;

 /**
     * @brief 获取所有视频通道的信息快照
     * @return 包含所有视频通道信息的 vector
     */
    std::vector<camsvc::MultimediaVideoChannel> getVideoChannels() override;
    /* ===== 状态查询 ===== */

    /**
     * @brief 获取指定视频通道的运行状态
     * @param index 通道索引号
     * @return 运行状态值，参数错误返回错误码
     */
    int videoStatus(int index) override;

protected:
    /**
     * @brief 设备探测与通道创建
     *
     * 从设备能力集（device_capability）读取视频/音频设备配置，
     * 根据 dev_impl 字段创建对应的通道对象（V4L2/MIPI/音频），
     * 存入 video_mp / audio_mp map 中。
     */
    int  camera_dev_detect(); // 通道检测  
/**
     * @brief 错误通知上报
     * @param channel 通道号
     * @param error 错误码
     * @param isHappen 是否发生（true=发生错误，false=错误解除）
     * @param desc 错误描述
     */
    void privNotifyError(int channel, int error, bool isHappen, char *desc);

    /**
     * @brief 更新指定通道的 4 类错误状态时间戳
     * @param channel 通道索引号
     *
     * 每类错误有 HAPPEN（12小时）和 CLEAR（10分钟）双阈值防抖判定。
     */
    void checkVideoError(int channel);
/* ===== JSON 配置解析 ===== */
    int  multimedia_service_parse_load(char *data, multimedia_service_json_config *config);
    int  multimedia_service_json_load(char *cfg_path);
    int  multimedia_service_json_config_confirm(multimedia_service_json_config *json_config);
    /**
     * @brief 内部错误码转换为外部通知错误码
     * @param innerCode 内部错误码（ErrorStatus 枚举）
     * @return 外部通知错误码（CMD_MULTIMEDIA_NOTIFY_ERROR_XXX）
     */
    // innerCode to outCode
    int exceptionErrorCodeConverter(int innerCode);

private:
    /* ===== 通道管理 ===== */

    /** 视频通道映射表：通道号 → VideoBaseInterface*
     *  每个 VideoBaseInterface 对应一个物理摄像头，由 camera_dev_detect() 创建 */
    std::map<int, VideoBaseInterface *> video_mp;

        /* 记录video通道主要的错误类型，方便上报异常 */
    std::map<int, SvcErrorRecord> videoErrNoNode;       // 设备节点丢失错误
    std::map<int, SvcErrorRecord> videoErrOpenFailed;   // 打开失败错误
    std::map<int, SvcErrorRecord> videoErrReadFailed;   // 读帧失败错误
    std::map<int, SvcErrorRecord> videoErrReadNull;     // 读帧为空错误

    /** JSON 配置结构体（异常检测参数、自恢复策略等）*/
    multimedia_service_json_config config_json;

    /* ===== 服务状态标志 ===== */

    /** 恢复检测标志（0=正常，1=曾进入异常恢复流程，所有通道恢复后清零）*/
    uint8_t resume_detection;

    /** 摄像头检测状态（所有通道状态的 OR 值，0=全部正常，1=存在异常）*/
    int     video_detect_status;

    /** 系统状态（0=自检模式，1=正常运行模式，自检模式下不执行异常检测）*/
    int     system_status;

    /** 系统忙碌标志（1=忙碌，REBOOT 时需等待此标志清零才执行重启）*/
    int     system_busy_flag;


};

#endif

