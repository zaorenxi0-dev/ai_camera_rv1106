#ifndef IPC_DBUS_CLIENT_H
#define IPC_DBUS_CLIENT_H

#include "ipc_dbus_general.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 同步调用远程方法（阻塞，直到收到回复或超时）
 *
 * @param bus_name   目标服务总线名，如 "com.example.yolo"
 * @param path       目标服务对象路径，如 "/com/example/yolo"
 * @param interface  目标服务接口名，如 "com.example.yolo.main"
 * @param method     方法名
 * @param arg        输入字符串参数（可为 NULL）
 * @param result     输出：结果字符串（malloc 分配，调用方负责 free；可为 NULL 表示无返回值）
 * @return 0 成功，<0 失败
 */
int ipc_dbus_client_call(const char *bus_name, const char *path,
                         const char *interface, const char *method,
                         const char *arg, char **result);

#ifdef __cplusplus
}
#endif

#endif // IPC_DBUS_CLIENT_H
