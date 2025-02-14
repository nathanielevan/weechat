/*
 * irc-ctcp.c - IRC CTCP protocol
 *
 * Copyright (C) 2003-2023 Sébastien Helleu <flashcode@flashtux.org>
 *
 * This file is part of WeeChat, the extensible chat client.
 *
 * WeeChat is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * WeeChat is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with WeeChat.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <sys/utsname.h>
#include <locale.h>

#include "../weechat-plugin.h"
#include "irc.h"
#include "irc-ctcp.h"
#include "irc-channel.h"
#include "irc-color.h"
#include "irc-config.h"
#include "irc-input.h"
#include "irc-msgbuffer.h"
#include "irc-nick.h"
#include "irc-protocol.h"
#include "irc-server.h"


struct t_irc_ctcp_reply irc_ctcp_default_reply[] =
{ { "clientinfo", "${clientinfo}" },
  { "source",     "${download}" },
  { "time",       "${time}" },
  { "version",    "WeeChat ${version}" },
  { NULL,         NULL },
};


/*
 * Converts old CTCP format, by converting format "$xxx" to "${xxx}"
 * (new CTCP formats are evaluated).
 *
 * Note: result must be freed after use.
 */

char *
irc_ctcp_convert_legacy_format (const char *format)
{
    int i;
    char *str, *str2, old_format[256], new_format[256];
    char *ctcp_legacy_vars[] = {
        "clientinfo",
        "versiongit",
        "version",
        "git",
        "osinfo",
        "site",
        "download",
        "username",
        "realname",
        "date",
        "time",
        NULL,
    };

    if (!format)
        return NULL;

    str = strdup (format);;
    str2 = NULL;

    for (i = 0; ctcp_legacy_vars[i]; i++)
    {
        snprintf (old_format, sizeof (old_format),
                  "$%s",
                  ctcp_legacy_vars[i]);
        snprintf (new_format, sizeof (new_format),
                  "${%s}",
                  ctcp_legacy_vars[i]);
        str2 = weechat_string_replace (str, old_format, new_format);
        if (str)
            free (str);
        str = str2;
    }

    return str;
}

/*
 * Gets default reply for a CTCP query.
 *
 * Returns NULL if CTCP is unknown.
 */

const char *
irc_ctcp_get_default_reply (const char *ctcp)
{
    int i;

    for (i = 0; irc_ctcp_default_reply[i].name; i++)
    {
        if (weechat_strcasecmp (irc_ctcp_default_reply[i].name, ctcp) == 0)
            return irc_ctcp_default_reply[i].reply;
    }

    /* unknown CTCP */
    return NULL;
}

/*
 * Gets reply for a CTCP query.
 */

const char *
irc_ctcp_get_reply (struct t_irc_server *server, const char *ctcp)
{
    struct t_config_option *ptr_option;
    char option_name[512], *ctcp_lower;

    ctcp_lower = weechat_string_tolower (ctcp);
    if (!ctcp_lower)
        return NULL;

    snprintf (option_name, sizeof (option_name),
              "%s.%s", server->name, ctcp_lower);

    /* search for CTCP in configuration file, for server */
    ptr_option = weechat_config_search_option (irc_config_file,
                                               irc_config_section_ctcp,
                                               option_name);
    if (ptr_option)
    {
        free (ctcp_lower);
        return weechat_config_string (ptr_option);
    }

    /* search for CTCP in configuration file */
    ptr_option = weechat_config_search_option (irc_config_file,
                                               irc_config_section_ctcp,
                                               ctcp_lower);
    if (ptr_option)
    {
        free (ctcp_lower);
        return weechat_config_string (ptr_option);
    }

    free (ctcp_lower);

    /*
     * no CTCP reply found in config, then return default reply, or NULL
     * for unknown CTCP
     */
    return irc_ctcp_get_default_reply (ctcp);
}

/*
 * Displays CTCP requested by a nick.
 */

void
irc_ctcp_display_request (struct t_irc_server *server,
                          time_t date,
                          struct t_hashtable *tags,
                          const char *command,
                          struct t_irc_channel *channel,
                          const char *nick,
                          const char *address,
                          const char *ctcp,
                          const char *arguments,
                          const char *reply)
{
    /* CTCP blocked and user doesn't want to see message? then just return */
    if (reply && !reply[0]
        && !weechat_config_boolean (irc_config_look_display_ctcp_blocked))
        return;

    weechat_printf_date_tags (
        irc_msgbuffer_get_target_buffer (
            server, nick, NULL, "ctcp",
            (channel) ? channel->buffer : NULL),
        date,
        irc_protocol_tags (server, command, tags, "irc_ctcp", NULL, address),
        _("%sCTCP requested by %s%s%s: %s%s%s%s%s%s"),
        weechat_prefix ("network"),
        irc_nick_color_for_msg (server, 0, NULL, nick),
        nick,
        IRC_COLOR_RESET,
        IRC_COLOR_CHAT_CHANNEL,
        ctcp,
        IRC_COLOR_RESET,
        (arguments) ? " " : "",
        (arguments) ? arguments : "",
        (reply && !reply[0]) ? _(" (blocked)") : "");
}

/*
 * Displays reply from a nick to a CTCP query.
 */

void
irc_ctcp_display_reply_from_nick (struct t_irc_server *server, time_t date,
                                  struct t_hashtable *tags,
                                  const char *command, const char *nick,
                                  const char *address, const char *arguments)
{
    char *dup_arguments, *ptr_args, *pos_end, *pos_space, *pos_args, *pos_usec;
    struct timeval tv;
    long sec1, usec1, sec2, usec2, difftime;

    dup_arguments = strdup (arguments);
    if (!dup_arguments)
        return;

    ptr_args = dup_arguments;

    while (ptr_args && ptr_args[0])
    {
        pos_end = strrchr (ptr_args + 1, '\01');
        if (pos_end)
            pos_end[0] = '\0';

        pos_space = strchr (ptr_args + 1, ' ');
        if (pos_space)
        {
            pos_space[0] = '\0';
            pos_args = pos_space + 1;
            while (pos_args[0] == ' ')
            {
                pos_args++;
            }
            if (weechat_strcasecmp (ptr_args + 1, "ping") == 0)
            {
                pos_usec = strchr (pos_args, ' ');
                if (pos_usec)
                {
                    pos_usec[0] = '\0';

                    gettimeofday (&tv, NULL);
                    sec1 = atol (pos_args);
                    usec1 = atol (pos_usec + 1);
                    sec2 = tv.tv_sec;
                    usec2 = tv.tv_usec;

                    difftime = ((sec2 * 1000000) + usec2) -
                        ((sec1 * 1000000) + usec1);
                    weechat_printf_date_tags (
                        irc_msgbuffer_get_target_buffer (
                            server, nick, NULL, "ctcp", NULL),
                        date,
                        irc_protocol_tags (server, command, tags, "irc_ctcp",
                                           NULL, NULL),
                        /* TRANSLATORS: %.3fs is a float number + "s" ("seconds") */
                        _("%sCTCP reply from %s%s%s: %s%s%s %.3fs"),
                        weechat_prefix ("network"),
                        irc_nick_color_for_msg (server, 0, NULL, nick),
                        nick,
                        IRC_COLOR_RESET,
                        IRC_COLOR_CHAT_CHANNEL,
                        ptr_args + 1,
                        IRC_COLOR_RESET,
                        (float)difftime / 1000000.0);
                }
            }
            else
            {
                weechat_printf_date_tags (
                    irc_msgbuffer_get_target_buffer (
                        server, nick, NULL, "ctcp", NULL),
                    date,
                    irc_protocol_tags (server, command, tags, "irc_ctcp", NULL,
                                       address),
                    _("%sCTCP reply from %s%s%s: %s%s%s%s%s"),
                    weechat_prefix ("network"),
                    irc_nick_color_for_msg (server, 0, NULL, nick),
                    nick,
                    IRC_COLOR_RESET,
                    IRC_COLOR_CHAT_CHANNEL,
                    ptr_args + 1,
                    IRC_COLOR_RESET,
                    " ",
                    pos_args);
            }
        }
        else
        {
            weechat_printf_date_tags (
                irc_msgbuffer_get_target_buffer (
                    server, nick, NULL, "ctcp", NULL),
                date,
                irc_protocol_tags (server, command, tags, NULL, NULL, address),
                _("%sCTCP reply from %s%s%s: %s%s%s%s%s"),
                weechat_prefix ("network"),
                irc_nick_color_for_msg (server, 0, NULL, nick),
                nick,
                IRC_COLOR_RESET,
                IRC_COLOR_CHAT_CHANNEL,
                ptr_args + 1,
                "",
                "",
                "");
        }

        ptr_args = (pos_end) ? pos_end + 1 : NULL;
    }

    free (dup_arguments);
}

/*
 * Displays CTCP replied to a nick.
 */

void
irc_ctcp_reply_to_nick (struct t_irc_server *server,
                        struct t_hashtable *tags,
                        const char *command,
                        struct t_irc_channel *channel,
                        const char *nick,
                        const char *ctcp,
                        const char *arguments)
{
    struct t_arraylist *list_messages;
    int i, list_size;
    char *msg_color, *dup_ctcp, *dup_ctcp_upper, *dup_args;
    const char *ptr_message;

    dup_ctcp = NULL;
    dup_ctcp_upper = NULL;
    dup_args = NULL;
    list_messages = NULL;

    /*
     * replace any "\01" by a space to prevent any firewall attack via
     * nf_conntrack_irc (CVE-2022-2663)
     */
    dup_ctcp = weechat_string_replace (ctcp, "\01", " ");
    if (!dup_ctcp)
        goto end;

    dup_ctcp_upper = weechat_string_toupper (dup_ctcp);
    if (!dup_ctcp_upper)
        goto end;

    if (arguments)
    {
        /*
         * replace any "\01" by a space to prevent any firewall attack via
         * nf_conntrack_irc (CVE-2022-2663)
         */
        dup_args = weechat_string_replace (arguments, "\01", " ");
        if (!dup_args)
            goto end;
    }

    list_messages = irc_server_sendf (
        server,
        IRC_SERVER_SEND_OUTQ_PRIO_LOW | IRC_SERVER_SEND_RETURN_LIST,
        NULL,
        "NOTICE %s :\01%s%s%s\01",
        nick,
        dup_ctcp_upper,
        (dup_args) ? " " : "",
        (dup_args) ? dup_args : "");
    if (!list_messages)
        goto end;

    if (weechat_config_boolean (irc_config_look_display_ctcp_reply))
    {
        list_size = weechat_arraylist_size (list_messages);
        for (i = 0; i < list_size; i++)
        {
            ptr_message = (const char *)weechat_arraylist_get (list_messages, i);
            if (!ptr_message)
                break;
            msg_color = irc_color_decode (ptr_message, 1);
            if (!msg_color)
                break;
            weechat_printf_date_tags (
                irc_msgbuffer_get_target_buffer (
                    server, nick, NULL, "ctcp",
                    (channel) ? channel->buffer : NULL),
                0,
                irc_protocol_tags (
                    server,
                    command,
                    tags,
                    "irc_ctcp,irc_ctcp_reply,self_msg,notify_none,"
                    "no_highlight",
                    NULL, NULL),
                _("%sCTCP reply to %s%s%s: %s%s%s%s%s"),
                weechat_prefix ("network"),
                irc_nick_color_for_msg (server, 0, NULL, nick),
                nick,
                IRC_COLOR_RESET,
                IRC_COLOR_CHAT_CHANNEL,
                dup_ctcp_upper,
                (msg_color[0]) ? IRC_COLOR_RESET : "",
                (msg_color[0]) ? " " : "",
                msg_color);
            free (msg_color);
        }
    }

end:
    if (dup_ctcp)
        free (dup_ctcp);
    if (dup_ctcp_upper)
        free (dup_ctcp_upper);
    if (dup_args)
        free (dup_args);
    if (list_messages)
        weechat_arraylist_free (list_messages);
}

/*
 * Compares two CTCPs in arraylist.
 */

int
irc_ctcp_list_ctcp_cmp_cb (void *data, struct t_arraylist *arraylist,
                           void *pointer1, void *pointer2)
{
    /* make C compiler happy */
    (void) data;
    (void) arraylist;

    return weechat_strcasecmp ((const char *)pointer1, (const char *)pointer2);
}

/*
 * Frees a CTCP in arraylist.
 */

void
irc_ctcp_list_ctcp_free_cb (void *data, struct t_arraylist *arraylist,
                            void *pointer)
{
    /* make C compiler happy */
    (void) data;
    (void) arraylist;

    free (pointer);
}

/*
 * Returns list of supported/configured CTCP replies, aggregation of these
 * lists:
 *
 *   - list of default CTCP replies (if not blocked)
 *   - list of CTCP replies defined in options irc.ctcp.* (if not blocked)
 *   - other CTCP: ACTION, DCC, PING.
 *
 * The list returned is a string with multiple CTCP (upper case) separated by
 * spaces.
 *
 * Note: result must be freed after use.
 */

char *
irc_ctcp_get_supported_ctcp (struct t_irc_server *server)
{
    struct t_arraylist *list_ctcp;
    struct t_hdata *hdata_config_section, *hdata_config_option;
    struct t_config_option *ptr_option;
    const char *reply, *ptr_name;
    char *ctcp_upper, **result;
    int i, list_size;

    list_ctcp = weechat_arraylist_new (16, 1, 0,
                                       &irc_ctcp_list_ctcp_cmp_cb, NULL,
                                       &irc_ctcp_list_ctcp_free_cb, NULL);
    if (!list_ctcp)
        return NULL;

    /* add default CTCPs */
    for (i = 0; irc_ctcp_default_reply[i].name; i++)
    {
        reply = irc_ctcp_get_reply (server, irc_ctcp_default_reply[i].name);
        if (reply && reply[0])
        {
            weechat_arraylist_add (list_ctcp,
                                   strdup (irc_ctcp_default_reply[i].name));
        }
    }

    /* add customized CTCPs */
    hdata_config_section = weechat_hdata_get ("config_section");
    hdata_config_option = weechat_hdata_get ("config_option");
    ptr_option = weechat_hdata_pointer (hdata_config_section,
                                        irc_config_section_ctcp,
                                        "options");
    while (ptr_option)
    {
        ptr_name = weechat_hdata_string (hdata_config_option, ptr_option, "name");
        if (ptr_name)
        {
            reply = irc_ctcp_get_reply (server, ptr_name);
            if (reply && reply[0])
                weechat_arraylist_add (list_ctcp, strdup (ptr_name));
        }
        ptr_option = weechat_hdata_move (hdata_config_option, ptr_option, 1);
    }

    /* add other CTCPs */
    weechat_arraylist_add (list_ctcp, strdup ("action"));
    weechat_arraylist_add (list_ctcp, strdup ("dcc"));
    weechat_arraylist_add (list_ctcp, strdup ("ping"));

    result = weechat_string_dyn_alloc (128);
    if (result)
    {
        list_size = weechat_arraylist_size (list_ctcp);
        for (i = 0; i < list_size; i++)
        {
            ctcp_upper = weechat_string_toupper (
                (const char *)weechat_arraylist_get (list_ctcp, i));
            if (ctcp_upper)
            {
                if (*result[0])
                    weechat_string_dyn_concat (result, " ", -1);
                weechat_string_dyn_concat (result, ctcp_upper, -1);
                free (ctcp_upper);
            }
        }
    }

    weechat_arraylist_free (list_ctcp);

    return (result) ? weechat_string_dyn_free (result, 0) : NULL;
}

/*
 * Evaluates CTCP reply format.
 *
 * Note: result must be freed after use.
 */

char *
irc_ctcp_eval_reply (struct t_irc_server *server, const char *format)
{
    struct t_hashtable *extra_vars;
    char *info, *info_version, *info_version_git, *username, *realname;
    char buf[4096], *value;
    time_t now;
    struct tm *local_time;
    struct utsname *buf_uname;

    if (!server || !format)
        return NULL;

    extra_vars = weechat_hashtable_new (
        32,
        WEECHAT_HASHTABLE_STRING,
        WEECHAT_HASHTABLE_STRING,
        NULL, NULL);
    if (!extra_vars)
        return NULL;

    /*
     * $clientinfo: supported CTCP, example with default config:
     *   ACTION CLIENTINFO DCC PING SOURCE TIME VERSION
     */
    info = irc_ctcp_get_supported_ctcp (server);
    if (info)
    {
        weechat_hashtable_set (extra_vars, "clientinfo", info);
        free (info);
    }

    info_version = weechat_info_get ("version", "");
    info_version_git = weechat_info_get ("version_git", "");

    /*
     * $version: WeeChat version, examples:
     *   0.3.9
     *   0.4.0-dev
     */
    if (info_version)
        weechat_hashtable_set (extra_vars, "version", info_version);

    /*
     * $git: git version (output of "git describe" for a development version
     * only, empty string if unknown), example:
     *   v0.3.9-104-g7eb5cc4
     */
    if (info_version_git)
        weechat_hashtable_set (extra_vars, "git", info_version_git);

    /*
     * $versiongit: WeeChat version + git version (if known), examples:
     *   0.3.9
     *   0.4.0-dev
     *   0.4.0-dev (git: v0.3.9-104-g7eb5cc4)
     */
    if (info_version && info_version_git)
    {
        snprintf (buf, sizeof (buf), "%s (git: %s)",
                  info_version,
                  info_version_git);
        weechat_hashtable_set (extra_vars, "versiongit", buf);
    }

    /*
     * $compilation: compilation date, example:
     *   Dec 16 2012
     */
    info = weechat_info_get ("date", "");
    if (info)
    {
        weechat_hashtable_set (extra_vars, "compilation", info);
        free (info);
    }

    /*
     * $osinfo: info about OS, example:
     *   Linux 2.6.32-5-amd64 / x86_64
     */
    buf_uname = (struct utsname *)malloc (sizeof (struct utsname));
    if (buf_uname)
    {
        if (uname (buf_uname) >= 0)
        {
            snprintf (buf, sizeof (buf),
                      "%s %s / %s",
                      buf_uname->sysname,
                      buf_uname->release,
                      buf_uname->machine);
            weechat_hashtable_set (extra_vars, "osinfo", buf);
        }
        if (buf_uname)
            free (buf_uname);
    }

    /*
     * $site: WeeChat website, example:
     *   https://weechat.org/
     */
    info = weechat_info_get ("weechat_site", "");
    if (info)
    {
        weechat_hashtable_set (extra_vars, "site", info);
        free (info);
    }

    /*
     * $download: WeeChat download page, example:
     *   https://weechat.org/download/
     */
    info = weechat_info_get ("weechat_site_download", "");
    if (info)
    {
        weechat_hashtable_set (extra_vars, "download", info);
        free (info);
    }

    /*
     * $time: local date/time of user, example:
     *   Sun, 16 Dec 2012 10:40:48 +0100
     */
    now = time (NULL);
    local_time = localtime (&now);
    setlocale (LC_ALL, "C");
    if (strftime (buf, sizeof (buf),
                  weechat_config_string (irc_config_look_ctcp_time_format),
                  local_time) == 0)
        buf[0] = '\0';
    setlocale (LC_ALL, "");
    weechat_hashtable_set (extra_vars, "time", buf);

    /*
     * $username: user name, example:
     *   name
     */
    username = irc_server_eval_expression (
        server,
        IRC_SERVER_OPTION_STRING(server, IRC_SERVER_OPTION_USERNAME));
    if (username)
    {
        weechat_hashtable_set (extra_vars, "username", username);
        free (username);
    }

    /*
     * $realname: real name, example:
     *   John doe
     */
    realname = irc_server_eval_expression (
        server,
        IRC_SERVER_OPTION_STRING(server, IRC_SERVER_OPTION_REALNAME));
    if (realname)
    {
        weechat_hashtable_set (extra_vars, "realname", realname);
        free (realname);
    }

    value = weechat_string_eval_expression (format, NULL, extra_vars, NULL);

    if (info_version)
        free (info_version);
    if (info_version_git)
        free (info_version_git);

    weechat_hashtable_free (extra_vars);

    return value;
}

/*
 * Returns filename for DCC, without double quotes.
 *
 * Note: result must be freed after use.
 */

char *
irc_ctcp_dcc_filename_without_quotes (const char *filename)
{
    int length;

    length = strlen (filename);
    if (length > 1)
    {
        if ((filename[0] == '\"') && (filename[length - 1] == '\"'))
            return weechat_strndup (filename + 1, length - 2);
    }
    return strdup (filename);
}

/*
 * Parses CTCP DCC.
 */

void
irc_ctcp_recv_dcc (struct t_irc_server *server, const char *nick,
                   const char *arguments, const char *message)
{
    char *dcc_args, *pos, *pos_file, *pos_addr, *pos_port, *pos_size;
    char *pos_start_resume, *filename;
    struct t_infolist *infolist;
    struct t_infolist_item *item;
    char charset_modifier[1024];

    if (!arguments || !arguments[0])
        return;

    if (strncmp (arguments, "SEND ", 5) == 0)
    {
        arguments += 5;
        while (arguments[0] == ' ')
        {
            arguments++;
        }
        dcc_args = strdup (arguments);

        if (!dcc_args)
        {
            weechat_printf (
                server->buffer,
                _("%s%s: not enough memory for \"%s\" command"),
                weechat_prefix ("error"), IRC_PLUGIN_NAME, "privmsg");
            return;
        }

        /* DCC filename */
        pos_file = dcc_args;
        while (pos_file[0] == ' ')
        {
            pos_file++;
        }

        /* look for file size */
        pos_size = strrchr (pos_file, ' ');
        if (!pos_size)
        {
            weechat_printf (
                server->buffer,
                _("%s%s: cannot parse \"%s\" command"),
                weechat_prefix ("error"), IRC_PLUGIN_NAME, "privmsg");
            free (dcc_args);
            return;
        }

        pos = pos_size;
        pos_size++;
        while (pos[0] == ' ')
        {
            pos--;
        }
        pos[1] = '\0';

        /* look for DCC port */
        pos_port = strrchr (pos_file, ' ');
        if (!pos_port)
        {
            weechat_printf (
                server->buffer,
                _("%s%s: cannot parse \"%s\" command"),
                weechat_prefix ("error"), IRC_PLUGIN_NAME, "privmsg");
            free (dcc_args);
            return;
        }

        pos = pos_port;
        pos_port++;
        while (pos[0] == ' ')
        {
            pos--;
        }
        pos[1] = '\0';

        /* look for DCC IP address */
        pos_addr = strrchr (pos_file, ' ');
        if (!pos_addr)
        {
            weechat_printf (
                server->buffer,
                _("%s%s: cannot parse \"%s\" command"),
                weechat_prefix ("error"), IRC_PLUGIN_NAME, "privmsg");
            free (dcc_args);
            return;
        }

        pos = pos_addr;
        pos_addr++;
        while (pos[0] == ' ')
        {
            pos--;
        }
        pos[1] = '\0';

        /* remove double quotes around filename */
        filename = irc_ctcp_dcc_filename_without_quotes (pos_file);

        /* add DCC file via xfer plugin */
        infolist = weechat_infolist_new ();
        if (infolist)
        {
            item = weechat_infolist_new_item (infolist);
            if (item)
            {
                weechat_infolist_new_var_string (item, "plugin_name", weechat_plugin->name);
                weechat_infolist_new_var_string (item, "plugin_id", server->name);
                weechat_infolist_new_var_string (item, "type_string", "file_recv");
                weechat_infolist_new_var_string (item, "protocol_string", "dcc");
                weechat_infolist_new_var_string (item, "remote_nick", nick);
                weechat_infolist_new_var_string (item, "local_nick", server->nick);
                weechat_infolist_new_var_string (item, "filename",
                                                 (filename) ? filename : pos_file);
                weechat_infolist_new_var_string (item, "size", pos_size);
                weechat_infolist_new_var_string (item, "proxy",
                                                 IRC_SERVER_OPTION_STRING(server, IRC_SERVER_OPTION_PROXY));
                weechat_infolist_new_var_string (item, "remote_address", pos_addr);
                weechat_infolist_new_var_integer (item, "port", atoi (pos_port));
                (void) weechat_hook_signal_send ("xfer_add",
                                                 WEECHAT_HOOK_SIGNAL_POINTER,
                                                 infolist);
            }
            weechat_infolist_free (infolist);
        }

        (void) weechat_hook_signal_send ("irc_dcc",
                                         WEECHAT_HOOK_SIGNAL_STRING,
                                         (void *)message);

        if (filename)
            free (filename);

        free (dcc_args);
    }
    else if (strncmp (arguments, "RESUME ", 7) == 0)
    {
        arguments += 7;
        while (arguments[0] == ' ')
        {
            arguments++;
        }
        dcc_args = strdup (arguments);

        if (!dcc_args)
        {
            weechat_printf (
                server->buffer,
                _("%s%s: not enough memory for \"%s\" command"),
                weechat_prefix ("error"), IRC_PLUGIN_NAME, "privmsg");
            return;
        }

        /* DCC filename */
        pos_file = dcc_args;
        while (pos_file[0] == ' ')
        {
            pos_file++;
        }

        /* look for resume start position */
        pos_start_resume = strrchr (pos_file, ' ');
        if (!pos_start_resume)
        {
            weechat_printf (
                server->buffer,
                _("%s%s: cannot parse \"%s\" command"),
                weechat_prefix ("error"), IRC_PLUGIN_NAME, "privmsg");
            free (dcc_args);
            return;
        }
        pos = pos_start_resume;
        pos_start_resume++;
        while (pos[0] == ' ')
        {
            pos--;
        }
        pos[1] = '\0';

        /* look for DCC port */
        pos_port = strrchr (pos_file, ' ');
        if (!pos_port)
        {
            weechat_printf (
                server->buffer,
                _("%s%s: cannot parse \"%s\" command"),
                weechat_prefix ("error"), IRC_PLUGIN_NAME, "privmsg");
            free (dcc_args);
            return;
        }
        pos = pos_port;
        pos_port++;
        while (pos[0] == ' ')
        {
            pos--;
        }
        pos[1] = '\0';

        /* remove double quotes around filename */
        filename = irc_ctcp_dcc_filename_without_quotes (pos_file);

        /* accept resume via xfer plugin */
        infolist = weechat_infolist_new ();
        if (infolist)
        {
            item = weechat_infolist_new_item (infolist);
            if (item)
            {
                weechat_infolist_new_var_string (item, "plugin_name", weechat_plugin->name);
                weechat_infolist_new_var_string (item, "plugin_id", server->name);
                weechat_infolist_new_var_string (item, "type_string", "file_recv");
                weechat_infolist_new_var_string (item, "filename",
                                                 (filename) ? filename : pos_file);
                weechat_infolist_new_var_integer (item, "port", atoi (pos_port));
                weechat_infolist_new_var_string (item, "start_resume", pos_start_resume);
                (void) weechat_hook_signal_send ("xfer_accept_resume",
                                                 WEECHAT_HOOK_SIGNAL_POINTER,
                                                 infolist);
            }
            weechat_infolist_free (infolist);
        }

        (void) weechat_hook_signal_send ("irc_dcc",
                                         WEECHAT_HOOK_SIGNAL_STRING,
                                         (void *)message);

        if (filename)
            free (filename);

        free (dcc_args);
    }
    else if (strncmp (arguments, "ACCEPT ", 7) == 0)
    {
        arguments += 7;
        while (arguments[0] == ' ')
        {
            arguments++;
        }
        dcc_args = strdup (arguments);

        if (!dcc_args)
        {
            weechat_printf (
                server->buffer,
                _("%s%s: not enough memory for \"%s\" command"),
                weechat_prefix ("error"), IRC_PLUGIN_NAME, "privmsg");
            return;
        }

        /* DCC filename */
        pos_file = dcc_args;
        while (pos_file[0] == ' ')
        {
            pos_file++;
        }

        /* look for resume start position */
        pos_start_resume = strrchr (pos_file, ' ');
        if (!pos_start_resume)
        {
            weechat_printf (
                server->buffer,
                _("%s%s: cannot parse \"%s\" command"),
                weechat_prefix ("error"), IRC_PLUGIN_NAME, "privmsg");
            free (dcc_args);
            return;
        }
        pos = pos_start_resume;
        pos_start_resume++;
        while (pos[0] == ' ')
        {
            pos--;
        }
        pos[1] = '\0';

        /* look for DCC port */
        pos_port = strrchr (pos_file, ' ');
        if (!pos_port)
        {
            weechat_printf (
                server->buffer,
                _("%s%s: cannot parse \"%s\" command"),
                weechat_prefix ("error"), IRC_PLUGIN_NAME, "privmsg");
            free (dcc_args);
            return;
        }
        pos = pos_port;
        pos_port++;
        while (pos[0] == ' ')
        {
            pos--;
        }
        pos[1] = '\0';

        /* remove double quotes around filename */
        filename = irc_ctcp_dcc_filename_without_quotes (pos_file);

        /* resume file via xfer plugin */
        infolist = weechat_infolist_new ();
        if (infolist)
        {
            item = weechat_infolist_new_item (infolist);
            if (item)
            {
                weechat_infolist_new_var_string (item, "plugin_name", weechat_plugin->name);
                weechat_infolist_new_var_string (item, "plugin_id", server->name);
                weechat_infolist_new_var_string (item, "type_string", "file_recv");
                weechat_infolist_new_var_string (item, "filename",
                                                 (filename) ? filename : pos_file);
                weechat_infolist_new_var_integer (item, "port", atoi (pos_port));
                weechat_infolist_new_var_string (item, "start_resume", pos_start_resume);
                (void) weechat_hook_signal_send ("xfer_start_resume",
                                                 WEECHAT_HOOK_SIGNAL_POINTER,
                                                 infolist);
            }
            weechat_infolist_free (infolist);
        }

        (void) weechat_hook_signal_send ("irc_dcc",
                                         WEECHAT_HOOK_SIGNAL_STRING,
                                         (void *)message);

        if (filename)
            free (filename);

        free (dcc_args);
    }
    else if (strncmp (arguments, "CHAT ", 5) == 0)
    {
        arguments += 5;
        while (arguments[0] == ' ')
        {
            arguments++;
        }
        dcc_args = strdup (arguments);

        if (!dcc_args)
        {
            weechat_printf (
                server->buffer,
                _("%s%s: not enough memory for \"%s\" command"),
                weechat_prefix ("error"), IRC_PLUGIN_NAME, "privmsg");
            return;
        }

        /* CHAT type */
        pos_file = dcc_args;
        while (pos_file[0] == ' ')
        {
            pos_file++;
        }

        /* DCC IP address */
        pos_addr = strchr (pos_file, ' ');
        if (!pos_addr)
        {
            weechat_printf (
                server->buffer,
                _("%s%s: cannot parse \"%s\" command"),
                weechat_prefix ("error"), IRC_PLUGIN_NAME, "privmsg");
            free (dcc_args);
            return;
        }
        pos_addr[0] = '\0';
        pos_addr++;
        while (pos_addr[0] == ' ')
        {
            pos_addr++;
        }

        /* look for DCC port */
        pos_port = strchr (pos_addr, ' ');
        if (!pos_port)
        {
            weechat_printf (
                server->buffer,
                _("%s%s: cannot parse \"%s\" command"),
                weechat_prefix ("error"), IRC_PLUGIN_NAME, "privmsg");
            free (dcc_args);
            return;
        }
        pos_port[0] = '\0';
        pos_port++;
        while (pos_port[0] == ' ')
        {
            pos_port++;
        }

        if (weechat_strcasecmp (pos_file, "chat") != 0)
        {
            weechat_printf (
                server->buffer,
                _("%s%s: unknown DCC CHAT type received from %s%s%s: \"%s\""),
                weechat_prefix ("error"),
                IRC_PLUGIN_NAME,
                irc_nick_color_for_msg (server, 0, NULL, nick),
                nick,
                IRC_COLOR_RESET,
                pos_file);
            free (dcc_args);
            return;
        }

        /* add DCC chat via xfer plugin */
        infolist = weechat_infolist_new ();
        if (infolist)
        {
            item = weechat_infolist_new_item (infolist);
            if (item)
            {
                weechat_infolist_new_var_string (item, "plugin_name", weechat_plugin->name);
                weechat_infolist_new_var_string (item, "plugin_id", server->name);
                weechat_infolist_new_var_string (item, "type_string", "chat_recv");
                weechat_infolist_new_var_string (item, "remote_nick", nick);
                weechat_infolist_new_var_string (item, "local_nick", server->nick);
                snprintf (charset_modifier, sizeof (charset_modifier),
                          "irc.%s.%s", server->name, nick);
                weechat_infolist_new_var_string (item, "charset_modifier", charset_modifier);
                weechat_infolist_new_var_string (item, "proxy",
                                                 IRC_SERVER_OPTION_STRING(server, IRC_SERVER_OPTION_PROXY));
                weechat_infolist_new_var_string (item, "remote_address", pos_addr);
                weechat_infolist_new_var_integer (item, "port", atoi (pos_port));
                (void) weechat_hook_signal_send ("xfer_add",
                                                 WEECHAT_HOOK_SIGNAL_POINTER,
                                                 infolist);
            }
            weechat_infolist_free (infolist);
        }

        (void) weechat_hook_signal_send ("irc_dcc",
                                         WEECHAT_HOOK_SIGNAL_STRING,
                                         (void *)message);

        free (dcc_args);
    }
}

/*
 * Receives a CTCP and if needed replies to query.
 */

void
irc_ctcp_recv (struct t_irc_server *server, time_t date,
               struct t_hashtable *tags, const char *command,
               struct t_irc_channel *channel, const char *target,
               const char *address, const char *nick, const char *remote_nick,
               const char *arguments, const char *message)
{
    char *dup_arguments, *ptr_args, *pos_end, *pos_space, *pos_args;
    char *nick_color, *reply_eval;
    const char *reply;
    struct t_irc_channel *ptr_channel;
    struct t_irc_nick *ptr_nick;
    int nick_is_me;

    dup_arguments = strdup (arguments);
    if (!dup_arguments)
        return;

    ptr_args = dup_arguments;

    while (ptr_args && ptr_args[0])
    {
        pos_end = strrchr (ptr_args + 1, '\01');
        if (pos_end)
            pos_end[0] = '\0';

        pos_args = NULL;
        pos_space = strchr (ptr_args + 1, ' ');
        if (pos_space)
        {
            pos_space[0] = '\0';
            pos_args = pos_space + 1;
            while (pos_args[0] == ' ')
            {
                pos_args++;
            }
        }

        /* CTCP ACTION */
        if (weechat_strcasecmp (ptr_args + 1, "action") == 0)
        {
            nick_is_me = (irc_server_strcasecmp (server, server->nick, nick) == 0);
            if (channel)
            {
                ptr_nick = irc_nick_search (server, channel, nick);
                irc_channel_nick_speaking_add (channel,
                                               nick,
                                               (pos_args) ?
                                               weechat_string_has_highlight (pos_args,
                                                                             server->nick) : 0);
                irc_channel_nick_speaking_time_remove_old (channel);
                irc_channel_nick_speaking_time_add (server, channel, nick,
                                                    time (NULL));
                if (ptr_nick)
                    nick_color = strdup (ptr_nick->color);
                else if (nick)
                    nick_color = irc_nick_find_color (nick);
                else
                    nick_color = strdup (IRC_COLOR_CHAT_NICK);
                if (irc_server_prefix_char_statusmsg (server, target[0]))
                {
                    /* STATUSMSG action */
                    weechat_printf_date_tags (
                        channel->buffer,
                        date,
                        irc_protocol_tags (
                            server,
                            command,
                            tags,
                            (nick_is_me) ?
                            "irc_action,self_msg,notify_none,no_highlight" :
                            "irc_action,notify_message",
                            nick,
                            address),
                        "%s%s -> %s%s%s: %s%s%s%s%s%s",
                        weechat_prefix ("network"),
                        /* TRANSLATORS: "Action" is an IRC CTCP "ACTION" sent with /me */
                        _("Action"),
                        IRC_COLOR_CHAT_CHANNEL,
                        target,
                        IRC_COLOR_RESET,
                        irc_nick_mode_for_display (server, ptr_nick, 0),
                        nick_color,
                        nick,
                        (pos_args) ? IRC_COLOR_RESET : "",
                        (pos_args) ? " " : "",
                        (pos_args) ? pos_args : "");
                }
                else
                {
                    /* standard action */
                    weechat_printf_date_tags (
                        channel->buffer,
                        date,
                        irc_protocol_tags (
                            server,
                            command,
                            tags,
                            (nick_is_me) ?
                            "irc_action,self_msg,notify_none,no_highlight" :
                            "irc_action,notify_message",
                            nick,
                            address),
                        "%s%s%s%s%s%s%s",
                        weechat_prefix ("action"),
                        irc_nick_mode_for_display (server, ptr_nick, 0),
                        nick_color,
                        nick,
                        (pos_args) ? IRC_COLOR_RESET : "",
                        (pos_args) ? " " : "",
                        (pos_args) ? pos_args : "");
                }
                if (nick_color)
                    free (nick_color);
            }
            else
            {
                ptr_channel = irc_channel_search (server, remote_nick);
                if (!ptr_channel)
                {
                    ptr_channel = irc_channel_new (server,
                                                   IRC_CHANNEL_TYPE_PRIVATE,
                                                   remote_nick, 0, 0);
                    if (!ptr_channel)
                    {
                        weechat_printf (
                            server->buffer,
                            _("%s%s: cannot create new private buffer \"%s\""),
                            weechat_prefix ("error"), IRC_PLUGIN_NAME,
                            remote_nick);
                    }
                }
                if (ptr_channel)
                {
                    if (!ptr_channel->topic)
                        irc_channel_set_topic (ptr_channel, address);

                    weechat_printf_date_tags (
                        ptr_channel->buffer,
                        date,
                        irc_protocol_tags (
                            server,
                            command,
                            tags,
                            (nick_is_me) ?
                            "irc_action,self_msg,notify_none,no_highlight" :
                            "irc_action,notify_private",
                            nick, address),
                        "%s%s%s%s%s%s",
                        weechat_prefix ("action"),
                        (nick_is_me) ?
                        IRC_COLOR_CHAT_NICK_SELF : irc_nick_color_for_pv (ptr_channel, nick),
                        nick,
                        (pos_args) ? IRC_COLOR_RESET : "",
                        (pos_args) ? " " : "",
                        (pos_args) ? pos_args : "");
                    (void) weechat_hook_signal_send ("irc_pv",
                                                     WEECHAT_HOOK_SIGNAL_STRING,
                                                     (void *)message);
                }
            }
        }
        /* CTCP PING */
        else if (weechat_strcasecmp (ptr_args + 1, "ping") == 0)
        {
            reply = irc_ctcp_get_reply (server, ptr_args + 1);
            irc_ctcp_display_request (server, date, tags, command, channel,
                                      nick, address, ptr_args + 1, pos_args,
                                      reply);
            if (!reply || reply[0])
            {
                if (reply)
                {
                    reply_eval = irc_ctcp_eval_reply (server, reply);
                    if (reply_eval)
                    {
                        irc_ctcp_reply_to_nick (server, tags, command, channel,
                                                nick, ptr_args + 1,
                                                reply_eval);
                        free (reply_eval);
                    }
                }
                else
                {
                    irc_ctcp_reply_to_nick (server, tags, command, channel,
                                            nick, ptr_args + 1, pos_args);
                }
            }
        }
        /* CTCP DCC */
        else if (weechat_strcasecmp (ptr_args + 1, "dcc") == 0)
        {
            irc_ctcp_recv_dcc (server, nick, pos_args, message);
        }
        /* other CTCP */
        else
        {
            reply = irc_ctcp_get_reply (server, ptr_args + 1);
            if (reply)
            {
                irc_ctcp_display_request (server, date, tags, command, channel,
                                          nick, address, ptr_args + 1, pos_args,
                                          reply);

                if (reply[0])
                {
                    reply_eval = irc_ctcp_eval_reply (server, reply);
                    if (reply_eval)
                    {
                        irc_ctcp_reply_to_nick (server, tags, command, channel,
                                                nick, ptr_args + 1,
                                                reply_eval);
                        free (reply_eval);
                    }
                }
            }
            else
            {
                if (weechat_config_boolean (irc_config_look_display_ctcp_unknown))
                {
                    weechat_printf_date_tags (
                        irc_msgbuffer_get_target_buffer (
                            server, nick, NULL, "ctcp",
                            (channel) ? channel->buffer : NULL),
                        date,
                        irc_protocol_tags (server, command, tags, "irc_ctcp",
                                           NULL, address),
                        _("%sUnknown CTCP requested by %s%s%s: %s%s%s%s%s"),
                        weechat_prefix ("network"),
                        irc_nick_color_for_msg (server, 0, NULL, nick),
                        nick,
                        IRC_COLOR_RESET,
                        IRC_COLOR_CHAT_CHANNEL,
                        ptr_args + 1,
                        (pos_args) ? IRC_COLOR_RESET : "",
                        (pos_args) ? " " : "",
                        (pos_args) ? pos_args : "");
                }
            }
        }

        (void) weechat_hook_signal_send ("irc_ctcp",
                                         WEECHAT_HOOK_SIGNAL_STRING,
                                         (void *)message);

        ptr_args = (pos_end) ? pos_end + 1 : NULL;
    }

    free (dup_arguments);
}

/*
 * Sends a CTCP to a target.
 */

void
irc_ctcp_send (struct t_irc_server *server,
               const char *target, const char *type, const char *args)
{
    irc_server_sendf (server,
                      IRC_SERVER_SEND_OUTQ_PRIO_HIGH, NULL,
                      "PRIVMSG %s :\01%s%s%s\01",
                      target,
                      type,
                      (args) ? " " : "",
                      (args) ? args : "");
}
