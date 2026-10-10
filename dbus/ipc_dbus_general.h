#ifndef IPC_DBUS_GENERAL_H
#define IPC_DBUS_GENERAL_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 方法处理函数
 * @param arg  客户端传来的字符串参数（可为 NULL 表示无参数）
 * @return 结果字符串（malloc 分配，由框架负责 free；可为 NULL 表示无返回值）
 */
typedef char *(*ipc_method_func)(const char *arg);

/**
 * @brief 方法表条目：方法名 -> 处理函数
 */
typedef struct
{
    const char *    name; // 方法名
    ipc_method_func func; // 处理函数
} ipc_method_entry;

#ifdef __cplusplus
}
#endif

#endif // IPC_DBUS_GENERAL_H
