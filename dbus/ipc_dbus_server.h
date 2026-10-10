#ifndef IPC_DBUS_SERVER_H
#define IPC_DBUS_SERVER_H

#include "ipc_dbus_general.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动 D-Bus 服务端（阻塞）
 *
 * 连接系统总线、申请总线名、注册消息过滤器，然后进入事件循环，
 * 处理客户端的方法调用，直到 ipc_dbus_server_stop() 被调用才返回。
 *
 * @param methods    方法表数组
 * @param count      方法数量
 * @param bus_name   总线名，如 "com.example.yolo"
 * @param path       对象路径，如 "/com/example/yolo"
 * @param interface  接口名，如 "com.example.yolo.main"
 * @return 0 成功，<0 失败
 */
int ipc_dbus_server_start(const ipc_method_entry *methods, int count,
                          const char *bus_name, const char *path,
                          const char *interface);

/**
 * @brief 停止服务端（可从信号处理函数等异步上下文调用）
 */
void ipc_dbus_server_stop(void);

#ifdef __cplusplus
}
#endif

#endif // IPC_DBUS_SERVER_H
