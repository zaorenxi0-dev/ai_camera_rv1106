#ifndef __VIDEO_H__
#define __VIDEO_H__
#include <unistd.h>




/**@enum CAM_FMT
     * @brief 音频采集控制类命令列表
     */
    typedef enum
    {
        CAM_FMT_NONE = 0, ///< None
        CAM_FMT_H264,     ///< h264
        CAM_FMT_H265,     ///< H265
        CAM_FMT_YUYV,     ///< YUYV
        CAM_FMT_JPEG,     ///< MJPEG
    } CAM_FMT;
    /**
     * 视频数据流结构体
     */
    typedef struct
    {
        void  *start;
        size_t length;
    } CamBuf;
    
    typedef struct
    {
        void *priv_data;
    } VideoContext;

typedef VideoContext CamContext;

    /**
     * control 控制命令列表
     */
    typedef enum
    {
        CAM_CMD_NONE = 0,
        CAM_CMD_SET_WIDTH,      // width
        CAM_CMD_SET_HEIGHT,     // height
        CAM_CMD_SET_BUF_TYPE,   // V4L2_BUF_TYPE_XXX (linux/videodev2.h)
        CAM_CMD_SET_PIX_FORMAT, // CAM_FMT (linux/videodev2.h)
        CAM_CMD_SET_FPS,
        CAM_CMD_SET_CHANNEL,        // rk 多通道时需要区别出不同的索引号，需要外部维护
        CAM_CMD_SET_DST_PIX_FORMAT, // CAM_FMT (linux/videodev2.h)
        CAM_CMD_SET_DST_RC_MODE,
        CAM_CMD_SET_DST_BITRATE,
        CAM_CMD_SET_KEY_FRAME,
    } CAM_CMD;

    typedef struct
    {
        char *name; // 设置惟一识别码
        /**
         * 打开设备
         * @param ctx 上下文
         * @param dev 设备名
         */
        int (*open)(VideoContext *ctx, const char *dev);
        int (*close)(VideoContext *ctx);
        /**
         * 控制设备接口
         * @param ctx
         * @param cmd  命令见 CAM_CMD
         * @param args
         */
        int (*ctrl)(VideoContext *ctx, int cmd, void *args);
        int (*start)(VideoContext *ctx);
        int (*stop)(VideoContext *ctx);
        /**
         * 读取数据，数据放到 buf 中，内部实现基于 poll,
         * 外部循环读即可，无需要等待
         * @param ctx
         * @param buf
         * @return : 1 读取到数据了
         *    0 没有出错，但没有读取到数据
         *    < 0 读取出错了
         */
        int (*read)(VideoContext *ctx, CamBuf *buf);
    } CamVideoImpl;

    extern const CamVideoImpl video_impl_rk_rv1106;
      extern const CamVideoImpl cam_video_impl_v4l2;

#endif
