#include "video.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <stdint.h>

#include "cm_common.h"
#include "kn_util_sys.h"
#include "logger.h"
#include "cam_utils.h"
typedef struct
{
    char         dev[128];
    int          fd;
    unsigned int width;
    unsigned int height;
    uint8_t      fps;
    int          buf_type;   // V4L2_BUF_TYPE_VIDEO_XXXX
    int          pix_format; // V4L2_PIX_FMT_xxx
    unsigned int n_buffers;
    CamBuf  *buf;
    CamBuf   read_buf;
} VideoPrivateDataV4L2;

static int cam_video_impl_normal_read_data(VideoPrivateDataV4L2 *priv, CamBuf *vbuf);









static int cam_video_impl_normal_open(CamContext *ctx, const char *dev)
{

    if (!ctx || !dev)
        return YQ_ERROR_ARGS;

    int fd = open(dev, O_RDWR | O_NONBLOCK);
    if (fd <= 0)
    {
        log_printf(ERROR, "open failed [%s] dev [%s]", strerror(errno), dev);
        return -2;
    }

     VideoPrivateDataV4L2 *_priv = (VideoPrivateDataV4L2 *)calloc(1, sizeof(VideoPrivateDataV4L2));
    if (!_priv)
        return YQ_ERROR_NO_MEM;

    strncpy(_priv->dev, dev, sizeof(_priv->dev) - 1);

    _priv->fd         = fd;
    _priv->buf_type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    _priv->pix_format = V4L2_PIX_FMT_YUYV;
    _priv->fps        = 25;
    _priv->read_buf.start = NULL;
    _priv->read_buf.length = 0;

    ctx->priv_data = _priv;
    printf( "video open dev [%s] success [%d] [%p]", dev, fd, ctx->priv_data);

    return YQ_ERROR_SUCCESS;
}



static int cam_video_impl_normal_stop(VideoContext *ctx)
{
    if (!ctx)
        return YQ_ERROR_ARGS;

    VideoPrivateDataV4L2 *_priv = (VideoPrivateDataV4L2 *)ctx->priv_data;
    if (!_priv || _priv->fd <= 0)
        return -2;

    printf( "video munmap buffers count [%d]", _priv->n_buffers);
    for (int i = 0; i < _priv->n_buffers; i++)
    {
        munmap(_priv->buf[i].start, _priv->buf[i].length);
    }
    if (_priv->fd > 0)
    {
        int type = _priv->buf_type;
        ioctl(_priv->fd, VIDIOC_STREAMOFF, &type);
    }
    if (_priv->buf)
    {
        free(_priv->buf);
        _priv->buf = NULL;
    }
    if (_priv->read_buf.start)
    {
        free(_priv->read_buf.start);
        _priv->read_buf.start = NULL;
        _priv->read_buf.length = 0;
    }
    _priv->n_buffers = 0;

    return YQ_ERROR_SUCCESS;
}



static int cam_video_impl_normal_close(CamContext *ctx)
{
    if (!ctx || !ctx->priv_data)
        return YQ_ERROR_ARGS;

    cam_video_impl_normal_stop(ctx);

    VideoPrivateDataV4L2 *_priv = (VideoPrivateDataV4L2 *)ctx->priv_data;
    if (_priv->fd > 0)
    {
        int ret = close(_priv->fd);
        printf( "close fd [%d] ret [%d]", _priv->fd, ret);
        _priv->fd = 0;
    }
    free(_priv);

    ctx->priv_data = NULL;
    return YQ_ERROR_SUCCESS;
}





static int cam_video_impl_normal_set_stream(VideoContext *ctx)
{
    if (!ctx || ctx->priv_data == NULL)
        return YQ_ERROR_ARGS;

    VideoPrivateDataV4L2 *_priv = (VideoPrivateDataV4L2 *)ctx->priv_data;
    if (!_priv || _priv->fd <= 0)
        return -2;

    struct v4l2_streamparm stream;
    stream.type                                  = _priv->buf_type;
    stream.parm.capture.capability               = V4L2_CAP_TIMEPERFRAME;
    stream.parm.capture.timeperframe.denominator = _priv->fps;
    stream.parm.capture.timeperframe.numerator   = 1;

    int ret = ioctl(_priv->fd, VIDIOC_S_PARM, &stream);
    if (ret < 0)
    {
        return -3;
    }

    return YQ_ERROR_SUCCESS;
}




static int cam_video_impl_normal_set_format(CamContext *ctx)
{
    if (!ctx || ctx->priv_data == NULL)
        return YQ_ERROR_ARGS;

    VideoPrivateDataV4L2 *_priv = (VideoPrivateDataV4L2 *)ctx->priv_data;
    if (!_priv || _priv->fd <= 0)
        return -2;

    struct v4l2_format format;
    format.type                = _priv->buf_type;
    format.fmt.pix.width       = _priv->width;
    format.fmt.pix.height      = _priv->height;
    format.fmt.pix.field       = V4L2_FIELD_INTERLACED;
    format.fmt.pix.pixelformat = _priv->pix_format;

    int ret = ioctl(_priv->fd, VIDIOC_S_FMT, &format);
    if (ret < 0)
    {
        return -3;
    }

    ret = ioctl(_priv->fd, VIDIOC_G_FMT, &format);
    if (ret < 0)
    {
        return -4;
    }

    char fmtstr[8] = {0};
    memcpy(fmtstr, &format.fmt.pix.pixelformat, 4);
    printf( "video format is [%s]", fmtstr);

    return YQ_ERROR_SUCCESS;
}



static int cam_video_impl_normal_queue(CamContext *ctx)
{


    if (!ctx || ctx->priv_data == NULL)
        return YQ_ERROR_ARGS;

    VideoPrivateDataV4L2 *_priv = (VideoPrivateDataV4L2 *)ctx->priv_data;
    if (_priv->fd <= 0)
        return -2;
    struct v4l2_requestbuffers req;
    struct v4l2_buffer         buf;

    req.count  = 4;
    req.type   = _priv->buf_type;
    req.memory = V4L2_MEMORY_MMAP;

    int ret = ioctl(_priv->fd, VIDIOC_REQBUFS, &req);
    if (ret < 0)
    {
        return -3;
    }

      _priv->buf = (CamBuf *)calloc(req.count, sizeof(CamBuf));
      if (!_priv->buf)
      {
          return YQ_ERROR_NO_MEM;
      }

    int n_buffers = 0;
    for (n_buffers = 0; n_buffers < req.count; n_buffers++)
    {
        buf.type   = _priv->buf_type;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = n_buffers;
        ret        = ioctl(_priv->fd, VIDIOC_QUERYBUF, &buf);
        if (ret < 0)
        {
            printf( "video querybuf index [%d] fail [%d] [%s]", n_buffers, ret, strerror(errno));
            continue;
        }
        _priv->buf[n_buffers].length = buf.length;
        _priv->buf[n_buffers].start =
            mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, _priv->fd, buf.m.offset);
    }
    _priv->n_buffers = n_buffers;

    for (int i = 0; i < n_buffers; i++)
    {
        buf.type   = _priv->buf_type;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;
        ret        = ioctl(_priv->fd, VIDIOC_QBUF, &buf);
        if (ret < 0)
        {
            printf( "video QBUF index [%d] fail [%d] [%s]", i, ret, strerror(errno));
            continue;
        }
    }

    return YQ_ERROR_SUCCESS;

}


static int cam_video_impl_normal_start(VideoContext *ctx)
{
    if (!ctx)
        return YQ_ERROR_ARGS;

    VideoPrivateDataV4L2 *_priv = (VideoPrivateDataV4L2 *)ctx->priv_data;
    if (!_priv || _priv->fd <= 0)
        return -2;

    int ret = cam_video_impl_normal_set_stream(ctx);
    if (ret < 0)
    {
        printf( "video set_stream fail [%d] [%s]", ret, strerror(errno));
        return -2;
    }
    ret = cam_video_impl_normal_set_format(ctx);
    if (ret < 0)
    {
        printf( "video set format fail [%d] [%s]", ret, strerror(errno));
        return -3;
    }

    ret = cam_video_impl_normal_queue(ctx);
    if (ret < 0)
    {
        printf( "video set queue fail [%d] [%s]", ret, strerror(errno));
        return -4;
    }

    int type = _priv->buf_type;
    ret      = ioctl(_priv->fd, VIDIOC_STREAMON, &type);
    if (ret < 0)
    {
        printf( "video stream on fail [%d] [%s]", ret, strerror(errno));    
        return -5;
    }

    return YQ_ERROR_SUCCESS;
}


static int cam_video_impl_normal_ctrl(CamContext *ctx, int cmd, void *args)
{
   if (!ctx || cmd <= 0 || ctx->priv_data == NULL)
        return YQ_ERROR_ARGS;

    VideoPrivateDataV4L2 *_priv = (VideoPrivateDataV4L2 *)ctx->priv_data;

      switch (cmd)
    {
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
    {
        int pix_format = *(int *)args;
        if (pix_format == CAM_FMT_YUYV)
        {
            _priv->pix_format = V4L2_PIX_FMT_YUYV;
        }
        else if (pix_format == CAM_FMT_JPEG)
        {
            _priv->pix_format = V4L2_PIX_FMT_MJPEG;
        }
        else
        {
            return YQ_ERROR_ARGS;
        }
    }
    break;
    case CAM_CMD_SET_KEY_FRAME:
#ifndef __MIPS32__
        printf( "request key frame fd [%d]", _priv->fd);
        if (_priv->fd)
        {
            struct v4l2_control control;
            control.value = 1;
            control.id    = V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME;
            ioctl(_priv->fd, VIDIOC_S_CTRL, &control);
        }
#else
        printf( "unsupported request key frame");
#endif
        break;
    }

    return YQ_ERROR_SUCCESS;

}

static int cam_video_impl_normal_read(CamContext *ctx, CamBuf *buf)
{

    if (!ctx)
        return YQ_ERROR_ARGS;

      VideoPrivateDataV4L2 *_priv = (VideoPrivateDataV4L2 *)ctx->priv_data;
    if (!_priv || _priv->fd <= 0)
        return -2;



    struct pollfd pfd[1];
    pfd[0].fd = _priv->fd;
    pfd[0].events = POLLIN | POLLERR;
    pfd[0].revents = 0;
    int ret        = poll(pfd, 1, 40);
    if (ret < 0)
    {
        printf( "video poll error [%d]", ret);
        return -3;
    }
    // 有数据
    if (pfd[0].revents & POLLIN)
    {
        ret = cam_video_impl_normal_read_data(_priv, buf);
        if (ret < 0)
        {
            printf( "video read data fail [%d]", ret);
            return -4;
        }
        return YQ_ERROR_DONE;
    }
    else if (pfd[0].revents & POLLERR)
    {
        printf( "video poll has error result, may be has closed");
        return -5;
    }
    else
    {
        //        log_printf(DEBUG, "poll events is [%02x] ret [%d]",
        //        pfd[0].revents, ret);
    }

    return YQ_ERROR_SUCCESS;

}




  static int cam_video_impl_normal_read_data(VideoPrivateDataV4L2 *priv, CamBuf *vbuf)
{
    if (!priv || priv->fd <= 0 || !vbuf)
        return YQ_ERROR_ARGS;

      struct v4l2_buffer     buf;

    memset(&buf, 0, sizeof(struct v4l2_buffer));
    buf.type   = priv->buf_type;
    buf.memory = V4L2_MEMORY_MMAP;
    int ret    = ioctl(priv->fd, VIDIOC_DQBUF, &buf);
    if (ret < 0)
        return -2;

      if (buf.index >= priv->n_buffers)
      {
          ioctl(priv->fd, VIDIOC_QBUF, &buf);
          return -3;
      }

      if (buf.bytesused > priv->read_buf.length)
      {
          void *new_buf = realloc(priv->read_buf.start, buf.bytesused);
          if (!new_buf)
          {
              ioctl(priv->fd, VIDIOC_QBUF, &buf);
              return YQ_ERROR_NO_MEM;
          }
          priv->read_buf.start = new_buf;
          priv->read_buf.length = buf.bytesused;
      }

      memcpy(priv->read_buf.start, priv->buf[buf.index].start, buf.bytesused);
      vbuf->start = priv->read_buf.start;
      vbuf->length = buf.bytesused;

    ret = ioctl(priv->fd, VIDIOC_QBUF, &buf);
    if (ret < 0)
    {
        printf( "video qbuf fail [%d] [%s]", ret, strerror(errno));
        return -4;
    }
    return YQ_ERROR_SUCCESS;


}
const CamVideoImpl cam_video_impl_v4l2 = {
    .name  = "v4l2-normal",
    .open  = cam_video_impl_normal_open,
    .close = cam_video_impl_normal_close,
    .ctrl  = cam_video_impl_normal_ctrl,
    .start = cam_video_impl_normal_start,
    .stop  = cam_video_impl_normal_stop,
    .read  = cam_video_impl_normal_read,
};
