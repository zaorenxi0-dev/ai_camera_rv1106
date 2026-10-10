#ifndef AI_CAMERA_APP_CONFIG_H
#define AI_CAMERA_APP_CONFIG_H

/*
 * 部署配置
 *
 * 这里的取值与具体运行环境相关，默认使用中性占位值。
 * 移植到你自己的环境时，请替换为实际的服务标识与文件路径。
 *
 * 之所以集中在此处，是为了避免把环境相关的字符串散落在业务代码里。
 */

/* ===== 目标检测服务的 D-Bus 标识 ===== */
#ifndef AI_DETECT_DBUS_NAME
#define AI_DETECT_DBUS_NAME "com.example.detect"
#endif

#ifndef AI_DETECT_DBUS_PATH
#define AI_DETECT_DBUS_PATH "/com/example/detect"
#endif

#ifndef AI_DETECT_DBUS_IFACE
#define AI_DETECT_DBUS_IFACE "com.example.detect.main"
#endif

#ifndef AI_DETECT_DBUS_METHOD
#define AI_DETECT_DBUS_METHOD "Detect"
#endif

/* ===== 多媒体服务配置文件路径 ===== */
#ifndef APP_MULTIMEDIA_CFG_PATH
#define APP_MULTIMEDIA_CFG_PATH "/app/etc/multimedia.json"
#endif

#ifndef APP_MULTIMEDIA_CFG_PATH_SAVE
#define APP_MULTIMEDIA_CFG_PATH_SAVE "/userdata/cfg/multimedia.json"
#endif

#endif /* AI_CAMERA_APP_CONFIG_H */
