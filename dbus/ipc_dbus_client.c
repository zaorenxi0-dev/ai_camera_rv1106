#include "ipc_dbus_client.h"

#include <dbus/dbus.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ipc_dbus_client_call(const char *bus_name, const char *path,
                         const char *interface, const char *method,
                         const char *arg, char **result)
{
    if (!bus_name || !path || !interface || !method)
    {
        return -1;
    }
    if (result)
    {
        *result = NULL;
    }

    DBusError err;
    dbus_error_init(&err);

    // 连接系统总线
    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
    if (!conn)
    {
        fprintf(stderr, "dbus_bus_get failed: %s\n", err.message);
        dbus_error_free(&err);
        return -2;
    }

    // 构造方法调用消息
    DBusMessage *msg =
        dbus_message_new_method_call(bus_name, path, interface, method);
    if (!msg)
    {
        dbus_connection_unref(conn);
        return -3;
    }

    // 附加字符串参数
    const char *a = arg ? arg : "";
    if (!dbus_message_append_args(msg, DBUS_TYPE_STRING, &a, DBUS_TYPE_INVALID))
    {
        dbus_message_unref(msg);
        dbus_connection_unref(conn);
        return -4;
    }

    // 同步发送并等待回复（10 秒超时）
    DBusMessage *reply =
        dbus_connection_send_with_reply_and_block(conn, msg, 10000, &err);
    dbus_message_unref(msg);
    if (!reply)
    {
        fprintf(stderr, "send failed: %s\n", err.message);
        dbus_error_free(&err);
        dbus_connection_unref(conn);
        return -5;
    }

    // 检查错误回复
    if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR)
    {
        dbus_set_error_from_message(&err, reply);
        fprintf(stderr, "method error: %s\n", err.message);
        dbus_error_free(&err);
        dbus_message_unref(reply);
        dbus_connection_unref(conn);
        return -6;
    }

    // 读取字符串结果
    const char *out = NULL;
    if (!dbus_message_get_args(reply, &err, DBUS_TYPE_STRING, &out,
                               DBUS_TYPE_INVALID))
    {
        fprintf(stderr, "get result error: %s\n", err.message);
        dbus_error_free(&err);
        dbus_message_unref(reply);
        dbus_connection_unref(conn);
        return -7;
    }

    if (result && out)
    {
        *result = strdup(out);
    }

    dbus_message_unref(reply);
    dbus_connection_unref(conn);
    return 0;
}
