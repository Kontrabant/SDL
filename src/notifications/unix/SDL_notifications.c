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

#include "../../core/linux/SDL_dbus.h"
#include "../../core/unix/SDL_appid.h"
#include "../../video/SDL_surface_c.h"

#if 0
#define NOTIFICATION_NODE "org.freedesktop.portal.Desktop"
#define NOTIFICATION_PATH "/org/freedesktop/portal/desktop"
#define NOTIFICATION_IF "org.freedesktop.portal.Notification"
#endif

#define NOTIFY_DBUS_NAME           "org.freedesktop.Notifications"
#define NOTIFY_DBUS_CORE_INTERFACE "org.freedesktop.Notifications"
#define NOTIFY_DBUS_CORE_OBJECT    "/org/freedesktop/Notifications"

typedef struct DBus_NotificationHint
{
    const char *key;
    union
    {
        dbus_bool_t bvalue;
        const char *strvalue;
    };
    int type;
} DBus_NotificationHint;

static bool SDL_DBus_SetIcon(SDL_DBusContext *dbus, DBusMessageIter *iterInit, SDL_Surface *surface)
{
    DBusMessageIter iter, array;

    dbus->message_iter_open_container(iterInit, DBUS_TYPE_STRUCT, NULL, &iter);
    dbus->message_iter_append_basic(&iter, DBUS_TYPE_INT32, &surface->w);
    dbus->message_iter_append_basic(&iter, DBUS_TYPE_INT32, &surface->h);
    dbus->message_iter_append_basic(&iter, DBUS_TYPE_INT32, &surface->pitch);
    dbus_bool_t alpha = true;
    Sint32 bpp = 8;
    dbus->message_iter_append_basic(&iter, DBUS_TYPE_BOOLEAN, &alpha);
    dbus->message_iter_append_basic(&iter, DBUS_TYPE_INT32, &bpp);
    bpp = 4;
    dbus->message_iter_append_basic(&iter, DBUS_TYPE_INT32, &bpp);

    dbus->message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "y", &array);

    Uint8 *pixels = surface->pixels;
    for (int i = 0; i < surface->pitch * surface->h; i++) {
        dbus->message_iter_append_basic(&array, DBUS_TYPE_BYTE, &pixels[i]);
    }

    dbus->message_iter_close_container(&iter, &array);
    dbus->message_iter_close_container(iterInit, &iter);

    return true;
}

static bool SDL_DBus_SetHints(SDL_DBusContext *dbus, DBusMessageIter *iterInit, bool transient, SDL_Surface *surface)
{
    DBusMessageIter iterDict;

    if (!dbus->message_iter_open_container(iterInit, DBUS_TYPE_ARRAY, "{sv}", &iterDict)) {
        goto failed;
    }

    {
        DBusMessageIter iterEntry, iterValue;

        if (!dbus->message_iter_open_container(&iterDict, DBUS_TYPE_DICT_ENTRY, NULL, &iterEntry)) {
            goto failed;
        }

        const char *key = "transient";
        if (!dbus->message_iter_append_basic(&iterEntry, DBUS_TYPE_STRING, &key)) {
            goto failed;
        }

        if (!dbus->message_iter_open_container(&iterEntry, DBUS_TYPE_VARIANT, DBUS_TYPE_BOOLEAN_AS_STRING, &iterValue)) {
            goto failed;
        }

        const dbus_bool_t db_transient = transient;
        if (!dbus->message_iter_append_basic(&iterValue, DBUS_TYPE_BOOLEAN, &db_transient)) {
            goto failed;
        }

        if (!dbus->message_iter_close_container(&iterEntry, &iterValue) || !dbus->message_iter_close_container(&iterDict, &iterEntry)) {
            goto failed;
        }
    }

    if (surface) {
        DBusMessageIter iterEntry, iterValue;

        if (!dbus->message_iter_open_container(&iterDict, DBUS_TYPE_DICT_ENTRY, NULL, &iterEntry)) {
            goto failed;
        }

        const char *key = "image-data";
        if (!dbus->message_iter_append_basic(&iterEntry, DBUS_TYPE_STRING, &key)) {
            goto failed;
        }

        if (!dbus->message_iter_open_container(&iterEntry, DBUS_TYPE_VARIANT, "(iiibiiay)", &iterValue)) {
            goto failed;
        }

        SDL_DBus_SetIcon(dbus, &iterValue, surface);

        if (!dbus->message_iter_close_container(&iterEntry, &iterValue) || !dbus->message_iter_close_container(&iterDict, &iterEntry)) {
            goto failed;
        }
    }

    if (!dbus->message_iter_close_container(iterInit, &iterDict)) {
        goto failed;
    }

    return true;

    failed:
        /* message_iter_abandon_container_if_open() and message_iter_abandon_container() might be
         * missing if libdbus is too old. Instead, we just return without cleaning up any eventual
         * open container */
        return false;
}

int SDL_DBus_ShowNotification(const SDL_NotificationData *notificationdata)
{
    SDL_DBusContext *dbus = SDL_DBus_GetContext();
    DBusConnection *conn = dbus->session_conn;
    DBusMessage *msg = NULL;
    DBusMessageIter iter, array;
    const char *id = SDL_GetAppID();

    /* Call Notification.AddNotification() */
    msg = dbus->message_new_method_call(NOTIFY_DBUS_NAME, NOTIFY_DBUS_CORE_OBJECT, NOTIFY_DBUS_CORE_INTERFACE, "Notify");
    if (msg == NULL) {
        goto failure;
    }

    dbus->message_iter_init_append(msg, &iter);
    /* App Id */
    dbus->message_iter_append_basic(&iter, DBUS_TYPE_STRING, &id);
    // replace id
    Uint32 uid = 0;
    dbus->message_iter_append_basic(&iter, DBUS_TYPE_UINT32, &uid);
    // icon string
    id = "";
    dbus->message_iter_append_basic(&iter, DBUS_TYPE_STRING, &id);
    // summary
    dbus->message_iter_append_basic(&iter, DBUS_TYPE_STRING, &notificationdata->title);
    // body
    dbus->message_iter_append_basic(&iter, DBUS_TYPE_STRING, &notificationdata->message);
    // actions
    dbus->message_iter_open_container(&iter, DBUS_TYPE_ARRAY, DBUS_TYPE_STRING_AS_STRING, &array);
    id = "Button1";
    dbus->message_iter_append_basic(&array, DBUS_TYPE_STRING, &id);
    id = "Button1";
    dbus->message_iter_append_basic(&array, DBUS_TYPE_STRING, &id);
    dbus->message_iter_close_container(&iter, &array);
    // hints
    SDL_DBus_SetHints(dbus, &iter, false, notificationdata->icon);

    // timeout
    Sint32 timeout = -1;
    dbus->message_iter_append_basic(&iter, DBUS_TYPE_INT32, &timeout);

    DBusError err;
    dbus->error_init(&err);
    if (!dbus->connection_send_with_reply_and_block(conn, msg, -1, &err)) {
        goto failure;
    }

    dbus->connection_flush(conn);

    dbus->message_unref(msg);

    return true;

failure:
    if (msg) {
        dbus->message_unref(msg);
    }
    return false;
}

int SDL_ShowNotification(const SDL_NotificationData *notificationdata)
{
    if (notificationdata == NULL) {
        return SDL_InvalidParamError("notificationdata");
    }

    if (notificationdata->title == NULL) {
        return SDL_InvalidParamError("notificationdata->title");
    }

    if (notificationdata->message == NULL) {
        return SDL_InvalidParamError("notificationdata->message");
    }

    return SDL_DBus_ShowNotification(notificationdata);
}


int SDL_ShowSimpleNotification(const char *title, const char *message)
{
    SDL_NotificationData notificationdata;
    SDL_zero(notificationdata);
    notificationdata.title = title;
    notificationdata.message = message;
    return SDL_ShowNotification(&notificationdata);
}

