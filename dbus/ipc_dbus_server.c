#include "ipc_dbus_server.h"

#include <dbus/dbus.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static DBusConnection *         g_conn      = NULL;
static const ipc_method_entry * g_methods   = NULL;
static int                      g_count     = 0;
static char                     g_interface[128];
static volatile int             g_stop      = 0;

/**
 * 消息过滤器：匹配方法调用，调用处理函数，构造回复
 */
static DBusHandlerResult ipc_dbus_filter(DBusConnection *conn, DBusMessage *msg,
                                         void *data)
{
    (void)data;

    for (int i = 0; i < g_count; i++)
    {
        if (!dbus_message_is_method_call(msg, g_interface, g_methods[i].name))
        {
            continue;
        }

        // 读取字符串参数（可为空）
        DBusError err;
        dbus_error_init(&err);
        const char *arg = NULL;
        if (!dbus_message_get_args(msg, &err, DBUS_TYPE_STRING, &arg,
                                   DBUS_TYPE_INVALID))
        {
            fprintf(stderr, "get args error: %s\n", err.message);
            dbus_error_free(&err);
            DBusMessage *reply = dbus_message_new_error(
                msg, "org.freedesktop.DBus.Error.InvalidArgs", "invalid args");
            dbus_connection_send(conn, reply, NULL);
            dbus_message_unref(reply);
            return DBUS_HANDLER_RESULT_HANDLED;
        }

        // 调用用户处理函数
        char *result = g_methods[i].func(arg);

        // 构造回复（字符串结果，NULL 时返回空字符串）
        DBusMessage *reply = dbus_message_new_method_return(msg);
        if (reply)
        {
            const char *out = result ? result : "";
            dbus_message_append_args(reply, DBUS_TYPE_STRING, &out,
                                     DBUS_TYPE_INVALID);
            dbus_connection_send(conn, reply, NULL);
            dbus_message_unref(reply);
        }

        if (result)
        {
            free(result);
        }
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

int ipc_dbus_server_start(const ipc_method_entry *methods, int count,
                          const char *bus_name, const char *path,
                          const char *interface)
{
    if (!methods || count <= 0 || !bus_name || !path || !interface)
    {
        return -1;
    }

    DBusError err;
    dbus_error_init(&err);

    // 连接系统总线
    g_conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
    if (!g_conn)
    {
        fprintf(stderr, "dbus_bus_get failed: %s\n", err.message);
        dbus_error_free(&err);
        return -2;
    }

    // 申请总线名
    int rv = dbus_bus_request_name(g_conn, bus_name,
                                   DBUS_NAME_FLAG_REPLACE_EXISTING, &err);
    if (rv != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER)
    {
        fprintf(stderr, "dbus_bus_request_name failed: rv=%d, %s\n",
                rv, err.message ? err.message : "unknown");
        dbus_error_free(&err);
        dbus_connection_unref(g_conn);
        g_conn = NULL;
        return -3;
    }

    g_methods = methods;
    g_count   = count;
    snprintf(g_interface, sizeof(g_interface), "%s", interface);
    g_stop = 0;

    if (!dbus_connection_add_filter(g_conn, ipc_dbus_filter, NULL, NULL))
    {
        fprintf(stderr, "add filter failed\n");
        dbus_connection_unref(g_conn);
        g_conn = NULL;
        return -4;
    }

    printf("dbus server started: %s %s %s\n", bus_name, path, interface);

    // 事件循环：每 100ms 检查一次停止标志
    while (!g_stop)
    {
        dbus_connection_read_write_dispatch(g_conn, 100);
    }

    dbus_connection_remove_filter(g_conn, ipc_dbus_filter, NULL);
    dbus_connection_unref(g_conn);
    g_conn = NULL;
    return 0;
}

void ipc_dbus_server_stop(void)
{
    g_stop = 1;
}
