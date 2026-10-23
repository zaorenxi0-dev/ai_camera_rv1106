#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rk_aiq_comm.h"
#include "rk_comm_rc.h"
#include "rk_comm_venc.h"
#include "rk_comm_vi.h"
#include "rk_comm_video.h"
#include "rk_mpi_venc.h"
#include "rk_mpi_vi.h"
#include "rk_type.h"
#include "common.h"
#include "jemalloc/jemalloc.h"
#include "sample_comm.h"
#include "video_rv1106.h"
#include "video.h"
#include "video_handler_manager.h"

typedef struct VideoPrivateData {
    char dev[128];//保存设备描述字符串，比如 "isp:dev:0:chn:0"
    unsigned int width;//视频宽度
    unsigned int height;//视频高度
    uint8_t fps;
    int buf_type;
    int pix_format;
    int dst_pix_format;//目标输出格式（编码格式），与 pix_format 同源
    int bitrate;//目标码率，单位 bps，<=0 表示使用默认值
    char rc_mode[8];//码控模式："cbr"/"vbr"/"avbr"，空串表示使用默认值
    unsigned int n_buffers;
    MPP_CHN_S rkvin_chn;//视频输入通道
    MPP_CHN_S rkvenc_chn;//视频编码通道
    VENC_STREAM_S stFrame;//视频编码通道帧结构体
    CamBuf *bufs;//视频编码通道帧缓冲区

    /* 初始化进度标记：每完成一步置 1，stop/shutdown 只回滚已完成的步骤。
     * 原实现靠函数入参传 isp_started/ref_added，且 UnBind 失败就 return，
     * 一旦中途失败会留下 VI/VENC/ISP 悬挂 */
    int state_ref;//已持有 MPI 系统引用
    int state_isp;//ISP 已启动
    int state_dev;//VI 设备已使能
    int state_chn;//VI 通道已使能
    int state_venc;//VENC 通道已创建
    int state_bind;//VI -> VENC 已绑定

} VideoPrivateData;

static int video_impl_rkvenc_parse_dev(const char *dev, MPP_CHN_S *vi_chn)
{
    int dev_id = 0;
    int chn_id = 0;

    if (!dev || !vi_chn) {
        return -1;
    }

    if (sscanf(dev, "isp:dev:%d:chn:%d", &dev_id, &chn_id) == 2) {
        vi_chn->s32DevId = dev_id;
        vi_chn->s32ChnId = chn_id;
        return 0;
    }

    if (sscanf(dev, "rkispp_scale%d", &chn_id) == 1) {
        vi_chn->s32DevId = 0;
        vi_chn->s32ChnId = chn_id;
        return 0;
    }

    return -1;
}

static void video_impl_rkvenc_free_buffers(VideoPrivateData *priv) {
    if (!priv) {
        return;
    }

    if (priv->stFrame.pstPack) {
        free(priv->stFrame.pstPack);
        priv->stFrame.pstPack = NULL;
    }

    if (priv->bufs) {
        if (priv->bufs->start) {
            free(priv->bufs->start);
            priv->bufs->start = NULL;
        }
        free(priv->bufs);
        priv->bufs = NULL;
    }
}

static void video_impl_rkvenc_shutdown(VideoPrivateData *priv) {
    if (!priv) {
        return;
    }

    /* 严格逆序回滚，每一步都先看标记，避免对未创建的通道下发命令 */
    if (priv->state_bind) {
        RK_MPI_SYS_UnBind(&priv->rkvin_chn, &priv->rkvenc_chn);
        priv->state_bind = 0;
    }

    if (priv->state_venc) {
        RK_MPI_VENC_StopRecvFrame(priv->rkvenc_chn.s32ChnId);
        RK_MPI_VENC_DestroyChn(priv->rkvenc_chn.s32ChnId);
        priv->state_venc = 0;
    }

    if (priv->state_chn) {
        RK_MPI_VI_DisableChn(priv->rkvin_chn.s32DevId, priv->rkvin_chn.s32ChnId);
        priv->state_chn = 0;
    }

    if (priv->state_dev) {
        RK_MPI_VI_StopPipe(priv->rkvin_chn.s32DevId);
        RK_MPI_VI_DisableDev(priv->rkvin_chn.s32DevId);
        priv->state_dev = 0;
    }

    if (priv->state_isp) {
#ifdef RKAIQ
        SAMPLE_COMM_ISP_Stop(priv->rkvin_chn.s32ChnId);
#endif
        priv->state_isp = 0;
    }

    video_impl_rkvenc_free_buffers(priv);

    if (priv->state_ref) {
        cam_video_handler_del_reference();
        priv->state_ref = 0;
    }
}

static RK_S32 test_venc_init(int chnId, int width, int height, int fps,
                             RK_CODEC_ID_E enType, int bitrate) {
    VENC_RECV_PIC_PARAM_S stRecvParam;
    VENC_CHN_ATTR_S stAttr;
    int dst_fps = fps > 0 ? fps : 30;
    memset(&stAttr,0,sizeof(VENC_CHN_ATTR_S));

    if (enType == RK_VIDEO_ID_MJPEG || enType == RK_VIDEO_ID_JPEG) {
        // MJPEG 必须用 MJPEG 自己的码控，H264CBR 会让编码器跑几帧后停住
        stAttr.stRcAttr.enRcMode = VENC_RC_MODE_MJPEGFIXQP;
        stAttr.stRcAttr.stMjpegFixQp.u32SrcFrameRateNum = dst_fps;
        stAttr.stRcAttr.stMjpegFixQp.u32SrcFrameRateDen = 1;
        stAttr.stRcAttr.stMjpegFixQp.fr32DstFrameRateNum = dst_fps;
        stAttr.stRcAttr.stMjpegFixQp.fr32DstFrameRateDen = 1;
        stAttr.stRcAttr.stMjpegFixQp.u32Qfactor = 60; // 1~99，越大质量越高
    } else {
        stAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
        /* RK MPI 的 u32BitRate 单位是 kbps，外部按 bps 下发，这里换算；
         * 未下发（<=0）时保留原来的 10kbps 兜底值，避免出现 0 码率 */
        stAttr.stRcAttr.stH264Cbr.u32BitRate = bitrate > 0 ? (RK_U32)(bitrate / 1024) : 10 * 1024;
        stAttr.stRcAttr.stH264Cbr.u32Gop = dst_fps * 2;
        stAttr.stVencAttr.u32Profile = H264E_PROFILE_HIGH;
    }

    stAttr.stVencAttr.enType = enType;
    stAttr.stVencAttr.enPixelFormat = RK_FMT_YUV420SP;
    stAttr.stVencAttr.u32PicWidth = width;
    stAttr.stVencAttr.u32VirWidth =width;
    stAttr.stVencAttr.u32VirHeight =height;
    stAttr.stVencAttr.u32PicHeight = height;
    stAttr.stVencAttr.u32StreamBufCnt = 8;
    stAttr.stVencAttr.u32BufSize = width*height*3/2;
    stAttr.stVencAttr.enMirror = MIRROR_NONE;
    
    int ret = RK_MPI_VENC_CreateChn(chnId, &stAttr);

    if(ret < 0) {
        printf("RK_MPI_VENC_CreateChn failed, ret = %d\n",ret);
        return ret;
    }
    
    memset(&stRecvParam,0,sizeof(VENC_RECV_PIC_PARAM_S));
    stRecvParam.s32RecvPicNum = -1;
    ret = RK_MPI_VENC_StartRecvFrame(chnId, &stRecvParam);
    if(ret < 0) {
        printf("RK_MPI_VENC_StartRecvFrame failed, ret = %d\n",ret);
        return ret;
    }

    return 0;
}

int vi_dev_init(int devId){
    int ret = 0;
    int pipeId = devId;

    VI_DEV_ATTR_S stDevAttr;
    VI_DEV_BIND_PIPE_S stBindPipe;
    memset(&stDevAttr,0,sizeof(VI_DEV_ATTR_S));
    memset(&stBindPipe,0,sizeof(VI_DEV_BIND_PIPE_S));

    ret = RK_MPI_VI_GetDevAttr(devId, &stDevAttr);
    if(ret == RK_ERR_VI_NOT_CONFIG) {
        ret = RK_MPI_VI_SetDevAttr(devId, &stDevAttr);
        if(ret !=RK_SUCCESS) {
            printf("RK_MPI_VI_SetDevAttr failed, ret = %d\n",ret);
            return ret;
        }
    }else{
        printf("RK_MPI_V_GetDevAttr success, ret = %d\n",ret);
    }

    ret = RK_MPI_VI_GetDevIsEnable(devId);
    if(ret !=RK_SUCCESS) {
        ret = RK_MPI_VI_EnableDev(devId);
        if(ret !=RK_SUCCESS) {
            printf("RK_MPI_VO_EnableDev failed, ret = %d\n",ret);
            return ret;
        }
        stBindPipe.u32Num = 1;
        stBindPipe.PipeId[0] = pipeId;
        ret = RK_MPI_VI_SetDevBindPipe(devId, &stBindPipe);
        if(ret !=RK_SUCCESS) {
            printf("RK_MPI_VI_SetDevBindPipe failed, ret = %d\n",ret);
            return ret;
        }
        // ★ 关键：启动 pipe，漏了它 VI 只出一帧
        ret = RK_MPI_VI_StartPipe(pipeId);
        if (ret != RK_SUCCESS) {
            printf("RK_MPI_VI_StartPipe fail ret = [%x] %d\n", ret, ret);
            return -1;
        }
    }else{
        printf("RK_MPI_VI_GetDevIsEnable success, ret = %d\n",ret);
    }
    return 0;

}

int vi_chn_init(int devId, int channelId, int width, int height, int fps){
    int ret = 0;
    int buf_cnt = 4;
    int dst_fps = fps > 0 ? fps : 30;

    VI_CHN_ATTR_S vi_chn_attr;
    memset(&vi_chn_attr,0,sizeof(VI_CHN_ATTR_S));
    vi_chn_attr.stIspOpt.u32BufCount = buf_cnt;
    vi_chn_attr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;
    vi_chn_attr.stSize.u32Width = width;
    vi_chn_attr.stSize.u32Height = height;
    vi_chn_attr.enPixelFormat = RK_FMT_YUV420SP;
    vi_chn_attr.u32Depth = 0; // bind 给 VENC 时必须 < u32BufCount，这里用 bind 不 GetFrame，置 0

     // ★ 关键：设置帧率
    vi_chn_attr.stFrameRate.s32SrcFrameRate = dst_fps;
    vi_chn_attr.stFrameRate.s32DstFrameRate = dst_fps;
    
    ret = RK_MPI_VI_SetChnAttr(devId, channelId, &vi_chn_attr);
    VI_DEV_STATUS_S st;
    st.bProbeOk = false;
    RK_MPI_VI_QueryDevStatus(devId, &st);
    if(!st.bProbeOk) {
        printf("RK_MPI_VI_QueryDevStatus failed, ret = %d\n",ret);
    }
    ret |= RK_MPI_VI_EnableChn(devId, channelId);
    if(ret) {
        printf("RK_MPI_VI_EnableChn failed, ret = %d\n",ret);
    }
        
    return ret;

}

// isp:dev:%d:chn:%d
static int video_impl_rkvenc_open(VideoContext *ctx,const char *dev) {
   if(!ctx || !dev)
        return YQ_ERROR_ARGS;

    VideoPrivateData *priv = (VideoPrivateData *)malloc(sizeof(VideoPrivateData));
    if(!priv)
        return YQ_ERROR_NO_MEM;

    /* 必须清零：width/height/fps/bitrate 等由 ctrl 下发，若某次没下发到，
     * 未清零的栈垃圾会直接变成硬件参数（bitrate 尤其危险） */
    memset(priv, 0, sizeof(VideoPrivateData));

     strncpy(priv->dev, dev, sizeof(priv->dev)-1);
      int ret = video_impl_rkvenc_parse_dev(priv->dev, &priv->rkvin_chn);
      if (ret < 0)
          {
                  printf("dev [%s] parse error\n", dev);
                  free(priv);
                  return YQ_ERROR_ARGS;
          }else{
                  printf("dev [%s] success, dev=%d chn=%d\n", dev, priv->rkvin_chn.s32DevId, priv->rkvin_chn.s32ChnId);
          }
          priv->rkvenc_chn.s32DevId = priv->rkvin_chn.s32DevId;
          priv->rkvenc_chn.s32ChnId = priv->rkvin_chn.s32ChnId;
        priv->rkvin_chn.enModId = RK_ID_VI;
        priv->rkvenc_chn.enModId = RK_ID_VENC;

        ctx->priv_data = priv;

        return 0;

}

static int video_impl_rkvenc_close(VideoContext *ctx) {

        if (!ctx || !ctx->priv_data)
                return YQ_ERROR_ARGS;

        VideoPrivateData *_priv = (VideoPrivateData *)ctx->priv_data;

        free(_priv);

        ctx->priv_data = NULL;

        return 0;
}

static int video_impl_rkvenc_ctrl(VideoContext *ctx, int cmd, void *args) {

        if (!ctx || cmd <= 0 || ctx->priv_data == NULL)
                return YQ_ERROR_ARGS;
        VideoPrivateData *_priv = (VideoPrivateData *)ctx->priv_data;

        if(!_priv)
                return YQ_ERROR_ARGS;
        
        switch(cmd) {
            case CAM_CMD_SET_WIDTH:
                _priv->width = *(unsigned int *)args;
                break;
        case CAM_CMD_SET_HEIGHT:
                _priv->height = *(unsigned int *)args;
                break;
        case CAM_CMD_SET_FPS:
                _priv->fps = *(uint8_t *)args;
                break;
        case CAM_CMD_SET_BUF_TYPE:
                _priv->buf_type = *(int *)args;
                break;
        case CAM_CMD_SET_PIX_FORMAT:
                _priv->pix_format = *(int *)args;
                break;
        case CAM_CMD_SET_DST_PIX_FORMAT:
                _priv->dst_pix_format = *(int *)args;
                break;
        case CAM_CMD_SET_DST_BITRATE:
                _priv->bitrate = *(int *)args;
                printf("video ctrl: bitrate = %d bps\n", _priv->bitrate);
                break;
        case CAM_CMD_SET_DST_RC_MODE:
                if (args) {
                        strncpy(_priv->rc_mode, (const char *)args, sizeof(_priv->rc_mode) - 1);
                        _priv->rc_mode[sizeof(_priv->rc_mode) - 1] = '\0';
                        printf("video ctrl: rc_mode = [%s]\n", _priv->rc_mode);
                }
                break;
        case CAM_CMD_SET_CHANNEL:
                /* 通道号已由 dev 字符串（isp:dev:X:chn:Y）解析得到，这里只做记录 */
                if (args) {
                        printf("video ctrl: channel index = %d (fixed by dev string)\n", *(int *)args);
                }
                break;
        case CAM_CMD_SET_KEY_FRAME:
                if (RK_MPI_VENC_RequestIDR(_priv->rkvenc_chn.s32ChnId, RK_TRUE) != RK_SUCCESS) {
                        printf("video ctrl: request IDR failed, venc not ready\n");
                }
                break;
        default:
                /* 上报而不是静默返回成功：未实现的命令必须让调用方看见 */
                printf("video ctrl: unsupported cmd [%d]\n", cmd);
                return YQ_ERROR_ARGS;
        }

        return YQ_ERROR_SUCCESS;

}


static int video_impl_rkvenc_start(VideoContext *ctx) {
    #define APP_IQ_PATH        "/app/iqfiles"

    if (!ctx)
        return YQ_ERROR_ARGS;

    VideoPrivateData *_priv = (VideoPrivateData *)ctx->priv_data;
    if (!_priv)
        return -2;

    RK_S32 ret = 0;

#ifdef  RKAIQ
    RK_BOOL bMultictx = RK_FALSE;
    rk_aiq_working_mode_t hdr_mode = RK_AIQ_WORKING_MODE_NORMAL;
    char iq_file_dir[1024] = {0};
    RK_U32 isp_fps = _priv->fps > 0 ? _priv->fps : 15;
    float exposure_time_sec = 1.0f / (float)isp_fps;
    snprintf(iq_file_dir, sizeof(iq_file_dir), "%s", APP_IQ_PATH);
    SAMPLE_COMM_ISP_Init(_priv->rkvin_chn.s32ChnId, hdr_mode, bMultictx, iq_file_dir);
    SAMPLE_COMM_ISP_Run(_priv->rkvin_chn.s32ChnId);
    _priv->state_isp = 1;
    SAMPLE_COMM_ISP_SetFrameRate(_priv->rkvin_chn.s32ChnId, isp_fps);
    SAMPLE_COMM_ISP_SetManualExposureAutoGain(_priv->rkvin_chn.s32ChnId,
                                              exposure_time_sec);
#endif

    /* 注意：本 SDK 实测必须 ISP 先初始化再 RK_MPI_SYS_Init，反过来会在
     * VI prepareRuntime 阶段随机段错误 / VENC 拿不到码流（2026-10-22 实测）。
     * 这里只保证 ref 计数与回滚用 state_ref 记录 */
    ret = cam_video_handler_add_reference();
    if (ret != RK_SUCCESS) {
        printf("mpi sys init failed ret = [%d]\n", ret);
        return -3;
    }
    _priv->state_ref = 1;
    _priv->stFrame.pstPack = NULL;

    ret = vi_dev_init(_priv->rkvin_chn.s32DevId);
    if (ret < 0) {
        printf("vi dev init failed , exit [%d]\n", ret);
        ret = -4;
        goto fail;
    }
    _priv->state_dev = 1;

    ret = vi_chn_init(_priv->rkvin_chn.s32DevId, _priv->rkvin_chn.s32ChnId,
                      _priv->width, _priv->height, _priv->fps);
    if (ret < 0) {
        ret = -5;
        goto fail;
    }
    _priv->state_chn = 1;

    /* dst_format 优先，未下发时回落到 pix_format */
    int fmt = _priv->dst_pix_format > 0 ? _priv->dst_pix_format : _priv->pix_format;
    int enCodecType = RK_VIDEO_ID_AVC;
    switch (fmt) {
    case CAM_FMT_H264:
        enCodecType = RK_VIDEO_ID_AVC;
        break;
    case CAM_FMT_H265:
        enCodecType = RK_VIDEO_ID_HEVC;
        break;
    case CAM_FMT_JPEG:
        enCodecType = RK_VIDEO_ID_MJPEG;
        break;
    }
    printf("enc with type = [%d]\n", enCodecType);

    ret = test_venc_init(_priv->rkvin_chn.s32ChnId, _priv->width,
                         _priv->height, _priv->fps,
                         enCodecType, _priv->bitrate);
    if (ret < 0) {
        ret = -6;
        goto fail;
    }
    _priv->state_venc = 1;

    RK_U32 s32Ret = RK_MPI_SYS_Bind(&_priv->rkvin_chn, &_priv->rkvenc_chn);
    if (s32Ret != RK_SUCCESS) {
        printf("sys bind failed of vi channel = [%d] venc channel = [%d]\n",
               _priv->rkvin_chn.s32ChnId, _priv->rkvenc_chn.s32ChnId);
        ret = -7;
        goto fail;
    }
    _priv->state_bind = 1;

    _priv->stFrame.pstPack = malloc(sizeof(VENC_PACK_S));
    _priv->bufs = (CamBuf *)malloc(sizeof(CamBuf));
    if (!_priv->bufs) {
        printf("alloc bufs failed\n");
        ret = YQ_ERROR_NO_MEM;
        goto fail;
    }

    size_t buffSize = _priv->width * _priv->height * 3;
    _priv->bufs->start = malloc(buffSize);
    if (!_priv->bufs->start) {
        printf("alloc frame buffer failed\n");
        ret = YQ_ERROR_NO_MEM;
        goto fail;
    }

    printf("alloc frame buffer success, size = %zu\n", buffSize);
    return 0;

fail:
    video_impl_rkvenc_shutdown(_priv);
    return ret;
}

static int video_impl_rkvenc_stop(VideoContext *ctx) {

    if (!ctx)
        return YQ_ERROR_ARGS;

    VideoPrivateData *_priv = (VideoPrivateData *)ctx->priv_data;
    if (!_priv)
                return -2;

    printf("%s exit! dev id = [%d] channel id = [%d]\n", __func__,
                   _priv->rkvin_chn.s32DevId, _priv->rkvin_chn.s32ChnId);

    /* 按 state_* 标记逆序回滚，任何一步失败都不中断后续回收。
     * 原实现 UnBind 失败就 return -8，导致 VI/VENC/ISP 与 MPI 引用全部悬挂 */
    video_impl_rkvenc_shutdown(_priv);

    return YQ_ERROR_SUCCESS;
}

static int video_impl_rkvenc_read(VideoContext *ctx, CamBuf *buf)
{
    if (!ctx || !ctx->priv_data || !buf)
        return YQ_ERROR_ARGS;

    VideoPrivateData *_priv = (VideoPrivateData *)ctx->priv_data;

    RK_S32 ret = RK_MPI_VENC_GetStream(_priv->rkvenc_chn.s32ChnId, &_priv->stFrame, 500);
    if (ret != RK_SUCCESS) {
        printf("RK_MPI_VENC_GetStream fail ret = [%x] %d\n", ret, ret);
        return YQ_ERROR_FILE_READ;
    }

    void *src = RK_MPI_MB_Handle2VirAddr(_priv->stFrame.pstPack->pMbBlk);
    size_t len = _priv->stFrame.pstPack->u32Len;

    if (!src || len == 0) {
        RK_MPI_VENC_ReleaseStream(_priv->rkvenc_chn.s32ChnId, &_priv->stFrame);
        return YQ_ERROR_FILE_READ;
    }

    memcpy(_priv->bufs->start, src, len);
    _priv->bufs->length = len;

    RK_MPI_VENC_ReleaseStream(_priv->rkvenc_chn.s32ChnId, &_priv->stFrame);

    buf->start = _priv->bufs->start;
    buf->length = _priv->bufs->length;

    return YQ_ERROR_DONE;
}

const CamVideoImpl video_impl_rk_rv1106 = {
    .name  = "rk-rv1106",
    .open  = video_impl_rkvenc_open,
    .close = video_impl_rkvenc_close,
    .ctrl  = video_impl_rkvenc_ctrl,
    .start = video_impl_rkvenc_start,
    .stop  = video_impl_rkvenc_stop,
    .read  = video_impl_rkvenc_read,
};
