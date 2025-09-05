/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

#include "SDL_internal.h"
#include "../SDL_syslocale.h"

#ifdef HAVE_DBUS_DBUS_H
#include "../../core/linux/SDL_dbus.h"

#define LOCALE_NODE "org.freedesktop.locale1"
#define LOCALE_PATH "/org/freedesktop/locale1"
#define LOCALE_INTERFACE "org.freedesktop.DBus.Properties"
#define LOCALE_METHOD "Get"

bool Locale_ReadDBusProperty()
{
    static const char *iface = LOCALE_NODE;
    static const char *key = "Locale";

    SDL_DBusContext *dbus = SDL_DBus_GetContext();

    if (!dbus || !dbus->system_conn) {
        return NULL;
    }

    DBusError err;
    DBusMessage *reply = NULL;
    DBusMessage *msg = dbus->message_new_method_call(LOCALE_NODE,
                                                     LOCALE_PATH,
                                                     LOCALE_INTERFACE,
                                                     "Get"); // Method

    if (msg) {
        dbus->error_init(&err);
        if (dbus->message_append_args(msg, DBUS_TYPE_STRING, &iface, DBUS_TYPE_STRING, &key, DBUS_TYPE_INVALID)) {
            reply = dbus->connection_send_with_reply_and_block(dbus->system_conn, msg, DBUS_TIMEOUT_USE_DEFAULT, &err);
        }
        if (dbus->error_is_set(&err)) {
            return NULL;
        }
        dbus->message_unref(msg);
    }

    if (reply) {
        DBusMessageIter iter, item;
        char *paths = NULL;

        dbus->message_iter_init(reply, &iter);
        dbus->message_iter_recurse(&iter, &iter);
        dbus->message_iter_recurse(&iter, &item);

        do {
            int type = dbus->message_iter_get_arg_type(&item);

            if (type == DBUS_TYPE_STRING) {
                dbus->message_iter_get_basic(&item, &paths);
                SDL_Log("LocaleStr: %s", paths);
            }
        } while (dbus->message_iter_next(&item));
        //dbus->message_get_args(reply, &err, DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &paths, &path_count, DBUS_TYPE_INVALID);
        dbus->message_unref(reply);
    }

    return reply;
}

#endif

static void normalize_locale_str(char *dst, char *str, size_t buflen)
{
    char *ptr;

    ptr = SDL_strchr(str, '.'); // chop off encoding if specified.
    if (ptr) {
        *ptr = '\0';
    }

    ptr = SDL_strchr(str, '@'); // chop off extra bits if specified.
    if (ptr) {
        *ptr = '\0';
    }

    // The "C" locale isn't useful for our needs, ignore it if you see it.
    if ((str[0] == 'C') && (str[1] == '\0')) {
        return;
    }

    if (*str) {
        if (*dst) {
            SDL_strlcat(dst, ",", buflen); // SDL has these split by commas
        }
        SDL_strlcat(dst, str, buflen);
    }
}

static void normalize_locales(char *dst, char *src, size_t buflen)
{
    char *ptr;

    // entries are separated by colons
    while ((ptr = SDL_strchr(src, ':')) != NULL) {
        *ptr = '\0';
        normalize_locale_str(dst, src, buflen);
        src = ptr + 1;
    }
    normalize_locale_str(dst, src, buflen);
}

bool SDL_SYS_GetPreferredLocales(char *buf, size_t buflen)
{
    // !!! FIXME: should we be using setlocale()? Or some D-Bus thing?
    bool isstack;
    const char *envr;
    char *tmp;

    Locale_ReadDBusProperty();

    SDL_assert(buflen > 0);
    tmp = SDL_small_alloc(char, buflen, &isstack);
    if (!tmp) {
        return false;
    }

    *tmp = '\0';

    // LANG is the primary locale (maybe)
    envr = SDL_getenv("LANG");
    if (envr) {
        SDL_strlcpy(tmp, envr, buflen);
    }

    // fallback languages
    envr = SDL_getenv("LANGUAGE");
    if (envr) {
        if (*tmp) {
            SDL_strlcat(tmp, ":", buflen);
        }
        SDL_strlcat(tmp, envr, buflen);
    }

    if (*tmp == '\0') {
        SDL_SetError("LANG environment variable isn't set");
    } else {
        normalize_locales(buf, tmp, buflen);
    }

    SDL_small_free(tmp, isstack);
    return true;
}
