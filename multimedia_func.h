#ifndef __MULTIMEDIA_FUNC_H__
#define __MULTIMEDIA_FUNC_H__


    /**@enum MEDIA_EVENT_TYPE
     * @brief 多媒体事件枚举
     */
    typedef enum
    {
        MEDIA_TYPE_NONE = 0, ///< None
        CAPTURE_START,       ///< 开始捕捉
        CAPTURE_STOP,        ///< 停止捕捉
        PLAYER_MP3_FILE,     ///< 播放MP3文件
    } MEDIA_EVENT_TYPE;

    /**@enum MEDIA_DEV_STATUS
     * @brief 多媒体设备状态枚举
     */
    typedef enum
    {
        DEV_NOREADY = 0, ///< 设备尚未准备好
        DEV_ISREADY,     ///< 设备已经准备好
        DEV_BUSYING,     ///< 设备忙碌
    } MEDIA_DEV_STATUS;

    /**@enum CAPTURE_INFO
     * @brief 定义抓取数据结构体
     */
    typedef struct
    {
        MEDIA_EVENT_TYPE capture_type; ///< 抓取多媒体类型
        unsigned int     capture_num;  ///< 拍摄个数 0: 不限制数量，一直到stop;
        unsigned int     capture_dev;  ///< 拍摄设备 see:DEV_TYPE
        unsigned int     process_type; ///< 拍摄目的 see:CAM_PROCESS_TYPE
        /**
         * 回调函数, 回传capture状态和消息
         * @param msg 信息类型 见 MEDIA_CB_MSG_TYPE
         * @param *args 信息数据
         */
        void (*cb)(int msg, void *args); ///< 回调函数: 不为空的时候会回传一些信息
                                         ///< 参见:MEDIA_CB_MSG_TYPE
    } CAPTURE_INFO;





#endif
