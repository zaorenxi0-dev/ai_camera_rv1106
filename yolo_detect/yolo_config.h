#ifndef YOLO_CONFIG_H
#define YOLO_CONFIG_H

// ========== 部署相关配置（与摄像头客户端 app_config.h 保持一致） ==========
// 板端部署前必须将下列占位值改回实际使用的 D-Bus 名称

#define YOLO_DBUS_BUS_NAME  "com.example.detect"
#define YOLO_DBUS_PATH      "/com/example/detect"
#define YOLO_DBUS_INTERFACE "com.example.detect.main"

// 默认模型文件（相对运行目录，也可在命令行传入覆盖）
#define YOLO_MODEL_PATH "beverage_v1_int8.rknn"

#endif // YOLO_CONFIG_H
