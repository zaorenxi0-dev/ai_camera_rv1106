# ai_camera_rv1106

RV1106（Luckfox Pico）摄像头采集客户端 + RKNN YOLO 检测服务的配对工程。

```
├── main.cpp / video_*.c        摄像头客户端：V4L2 / rockit 采集 → JPEG → D-Bus 调用检测服务
├── multimedia_service/         通道管理中间件（open/start/stop/close 状态机）
├── dbus/                       共享 D-Bus 模块（client.c / server.c，纯 libdbus）
└── yolo_detect/                板端检测服务：RKNN YOLOv5 INT8 推理，注册 D-Bus 方法 Detect
```

## 数据流

```
摄像头(VI) → VENC(JPEG) → main.cpp 保存快照
                              │ ipc_dbus_client_call("Detect", 图片路径)
                              ▼
                     rknn_yolo（yolo_detect/）: 加载 .rknn → 推理 → 画框 → 返回 "N 结果图路径"
```

## 构建（交叉工具链：arm-rockchip830-linux-uclibcgnueabihf）

```bash
# 客户端
mkdir build && cd build && cmake .. && make

# 检测服务
cd yolo_detect && mkdir build && cd build && cmake .. && make
```

## 模型转换与测试（PC 端，需 rknn-toolkit2）

```bash
cd yolo_detect
python convert.py    # ONNX → INT8 RKNN（使用 calib_list.txt 校准集）
python test_rknn.py  # ADB 连板后单帧推理自检
```

## 部署前必须修改的占位配置

D-Bus 名称与配置路径使用了中性占位值，两端**必须同时改回**实际值才能互通：

| 文件 | 宏 |
|---|---|
| `app_config.h`（客户端） | `AI_DETECT_DBUS_NAME / PATH / IFACE / METHOD`、`APP_MULTIMEDIA_CFG_PATH` |
| `yolo_detect/yolo_config.h`（服务端） | `YOLO_DBUS_BUS_NAME / PATH / INTERFACE` |

## 已知问题

- ch1 通道打开失败（-99）：`video_v4l2.c` 中 `VIDIOC_S_PARM` 失败被降级吞掉，且 `struct v4l2_streamparm` 未清零
- `video_rv1106.c` 存在 `_priv->bufs->start` 空指针风险、ISP 初始化早于 `RK_MPI_SYS_Init()` 等问题
