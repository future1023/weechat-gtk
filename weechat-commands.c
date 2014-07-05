#include "weechat-commands.h"

void weechat_cmd_init(weechat_t* weechat, const gchar* password,
                      gboolean compression)
{
    gchar* msg = g_strdup_printf("init password=%s,compression=%s", password,
                                 (compression) ? "on" : "off");

    weechat_send(weechat, msg);
    g_free(msg);
}

void weechat_cmd_test(weechat_t* weechat)
{
    /* Send */
    g_return_if_fail(weechat_send(weechat, "test"));

    /* Process */
    weechat_receive(weechat);
}

void weechat_cmd_ping(weechat_t* weechat, const gchar* s)
{
    gchar* msg = g_strdup_printf("ping %s", s);

    g_return_if_fail(weechat_send(weechat, msg));
    weechat_receive(weechat);
    g_free(msg);
}

void weechat_cmd_quit(weechat_t* weechat)
{
    weechat_send(weechat, "quit");
}
