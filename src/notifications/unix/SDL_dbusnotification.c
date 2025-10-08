/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

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
#include "../../events/SDL_notificationevents_c.h"
#include "../../video/SDL_surface_c.h"

#define NOTIFICATION_PORTAL_NODE      "org.freedesktop.portal.Desktop"
#define NOTIFICATION_PORTAL_PATH      "/org/freedesktop/portal/desktop"
#define NOTIFICATION_PORTAL_INTERFACE "org.freedesktop.portal.Notification"

#define NOTIFICATION_CORE_NODE      "org.freedesktop.Notifications"
#define NOTIFICATION_CORE_PATH      "/org/freedesktop/Notifications"
#define NOTIFICATION_CORE_INTERFACE "org.freedesktop.Notifications"

#define NOTIFICATION_SIGNAL_NAME "ActionInvoked"

#define ALL_PRIORITY_FLAGS (SDL_NOTIFICATION_PRIORITY_LOW | SDL_NOTIFICATION_PRIORITY_NORMAL | \
                            SDL_NOTIFICATION_PRIORITY_HIGH | SDL_NOTIFICATION_PRIORITY_URGENT)

static bool IsInContainer()
{
    static bool in_container = false;
    static bool in_container_set = false;

    if (in_container_set) {
        return in_container;
    }

    if (SDL_getenv("container")) { // Flatpak
        in_container = true;
    } else if (SDL_getenv("SNAP")) { // SNAP
        in_container = true;
    }

    in_container_set = true;

    return in_container;
}

static void AppendOption(SDL_DBusContext *dbus, DBusMessageIter *options, const char *type, const char *key, const void *value)
{
    DBusMessageIter options_pair, options_value;

    dbus->message_iter_open_container(options, DBUS_TYPE_DICT_ENTRY, NULL, &options_pair);
    dbus->message_iter_append_basic(&options_pair, DBUS_TYPE_STRING, &key);
    dbus->message_iter_open_container(&options_pair, DBUS_TYPE_VARIANT, type, &options_value);
    dbus->message_iter_append_basic(&options_value, (int)type[0], value);
    dbus->message_iter_close_container(&options_pair, &options_value);
    dbus->message_iter_close_container(options, &options_pair);
}

static void AppendStringOption(SDL_DBusContext *dbus, DBusMessageIter *options, const char *key, const char *value)
{
    DBusMessageIter options_pair, options_value;

    dbus->message_iter_open_container(options, DBUS_TYPE_DICT_ENTRY, NULL, &options_pair);
    dbus->message_iter_append_basic(&options_pair, DBUS_TYPE_STRING, &key);
    dbus->message_iter_open_container(&options_pair, DBUS_TYPE_VARIANT, DBUS_TYPE_STRING_AS_STRING, &options_value);
    dbus->message_iter_append_basic(&options_value, DBUS_TYPE_STRING, &value);
    dbus->message_iter_close_container(&options_pair, &options_value);
    dbus->message_iter_close_container(options, &options_pair);
}


// org.freedesktop.Notifications, used when not running inside a container.

static DBusHandlerResult CoreNotificationFilter(DBusConnection *conn, DBusMessage *msg, void *data)
{
    SDL_DBusContext *dbus = SDL_DBus_GetContext();

    if (dbus->message_is_signal(msg, NOTIFICATION_CORE_NODE, NOTIFICATION_SIGNAL_NAME)) {
        DBusMessageIter signal_iter; // variant_iter;
        const char *button = NULL;
        Uint32 id = 0;

        dbus->message_iter_init(msg, &signal_iter);
        // Check if the parameters are what we expect
        if (dbus->message_iter_get_arg_type(&signal_iter) != DBUS_TYPE_UINT32) {
            goto not_our_signal;
        }
        dbus->message_iter_get_basic(&signal_iter, &id);

        if (!dbus->message_iter_next(&signal_iter)) {
            goto not_our_signal;
        }
        if (dbus->message_iter_get_arg_type(&signal_iter) != DBUS_TYPE_STRING) {
            goto not_our_signal;
        }
        dbus->message_iter_get_basic(&signal_iter, &button);
        if (button) {
            SDL_SendNotificationAction(id, button);
        }

        return DBUS_HANDLER_RESULT_HANDLED;
    }

not_our_signal:
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

// Only add the filter if at least one of the settings we want is present.

static bool SetCoreIcon(SDL_DBusContext *dbus, DBusMessageIter *iterInit, SDL_Surface *surface)
{
    DBusMessageIter iterEntry, iterValue;
    DBusMessageIter iter, array;
    const dbus_bool_t alpha = true;
    Sint32 bpp = 8;

    if (!dbus->message_iter_open_container(iterInit, DBUS_TYPE_DICT_ENTRY, NULL, &iterEntry)) {
        return false;
    }

    const char *key = "image-data";
    if (!dbus->message_iter_append_basic(&iterEntry, DBUS_TYPE_STRING, &key)) {
        return false;
    }

    if (!dbus->message_iter_open_container(&iterEntry, DBUS_TYPE_VARIANT, "(iiibiiay)", &iterValue)) {
        return false;
    }

    if (!dbus->message_iter_open_container(&iterValue, DBUS_TYPE_STRUCT, NULL, &iter)) {
        return false;
    }

    // Width
    if (!dbus->message_iter_append_basic(&iter, DBUS_TYPE_INT32, &surface->w)) {
        return false;
    }

    // Height
    if (!dbus->message_iter_append_basic(&iter, DBUS_TYPE_INT32, &surface->h)) {
        return false;
    }

    // Pitch
    if (!dbus->message_iter_append_basic(&iter, DBUS_TYPE_INT32, &surface->pitch)) {
        return false;
    }

    // Alpha yes/no
    if (!dbus->message_iter_append_basic(&iter, DBUS_TYPE_BOOLEAN, &alpha)) {
        return false;
    }

    // BPP
    if (!dbus->message_iter_append_basic(&iter, DBUS_TYPE_INT32, &bpp)) {
        return false;
    }

    // Channels (always 4 with alpha)
    bpp = 4;
    if (!dbus->message_iter_append_basic(&iter, DBUS_TYPE_INT32, &bpp)) {
        return false;
    }

    // Raw image bytes
    if (!dbus->message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "y", &array)) {
        return false;
    }

    const Uint8 *pixels = surface->pixels;
    for (int i = 0; i < surface->pitch * surface->h; i++) {
        if (!dbus->message_iter_append_basic(&array, DBUS_TYPE_BYTE, &pixels[i])) {
            return false;
        }
    }

    dbus->message_iter_close_container(&iter, &array);
    dbus->message_iter_close_container(&iterValue, &iter);
    dbus->message_iter_close_container(&iterEntry, &iterValue);
    dbus->message_iter_close_container(iterInit, &iterEntry);

    return true;
}

static bool SetCoreHints(SDL_DBusContext *dbus, DBusMessageIter *iterInit, SDL_NotificationFlags flags, SDL_Surface *surface)
{
    DBusMessageIter iterDict;

    if (!dbus->message_iter_open_container(iterInit, DBUS_TYPE_ARRAY, "{sv}", &iterDict)) {
        goto failed;
    }

    if (flags & ALL_PRIORITY_FLAGS) {
        Uint8 priority;

        switch (flags & ALL_PRIORITY_FLAGS) {
        case SDL_NOTIFICATION_PRIORITY_NORMAL:
        case SDL_NOTIFICATION_PRIORITY_HIGH:
        default:
            priority = 1;
            break;
        case SDL_NOTIFICATION_PRIORITY_LOW:
            priority = 0;
            break;
        case SDL_NOTIFICATION_PRIORITY_URGENT:
            priority = 2;
            break;
        }

        AppendOption(dbus, &iterDict, DBUS_TYPE_BYTE_AS_STRING, "urgency", &priority);
    }

    if (flags & SDL_NOTIFICATION_TRANSIENT) {
        const dbus_bool_t db_transient = true;
        AppendOption(dbus, &iterDict, DBUS_TYPE_BOOLEAN_AS_STRING, "transient", &db_transient);
    }

    if (surface) {
        SetCoreIcon(dbus, &iterDict, surface);
    }

    if (!dbus->message_iter_close_container(iterInit, &iterDict)) {
        goto failed;
    }

    return true;

failed:

    return false;
}

static SDL_NotificationID ShowCoreNotification(const SDL_NotificationData *notification_data)
{
    SDL_DBusContext *dbus = SDL_DBus_GetContext();
    DBusConnection *conn = dbus->session_conn;
    DBusMessage *msg = NULL;
    DBusMessageIter iter, array;
    const char *id = SDL_GetAppID();
    Uint32 message_id = 0;
    DBusError error;

    dbus->error_init(&error);

    dbus->bus_add_match(dbus->session_conn,
                        "type='signal', interface='" NOTIFICATION_CORE_INTERFACE "',"
                        "member='" NOTIFICATION_SIGNAL_NAME "'",
                        &error);
    if (dbus->error_is_set(&error)) {
        goto failure;
    }
    if (dbus->connection_add_filter(dbus->session_conn, &CoreNotificationFilter, NULL, NULL)) {
        SDL_Log("Registered filter");
    }
    dbus->connection_flush(dbus->session_conn);

    // Call org.freedesktop.Notifications.Notify()
    msg = dbus->message_new_method_call(NOTIFICATION_CORE_NODE, NOTIFICATION_CORE_PATH, NOTIFICATION_CORE_INTERFACE, "Notify");
    if (msg == NULL) {
        goto failure;
    }

    dbus->message_iter_init_append(msg, &iter);
    // App ID
    dbus->message_iter_append_basic(&iter, DBUS_TYPE_STRING, &id);
    // Replace id
    Uint32 uid = 0;
    dbus->message_iter_append_basic(&iter, DBUS_TYPE_UINT32, &uid);
    // Icon string (always empty, set later via a hint)
    id = "";
    dbus->message_iter_append_basic(&iter, DBUS_TYPE_STRING, &id);
    // Summary
    dbus->message_iter_append_basic(&iter, DBUS_TYPE_STRING, &notification_data->title);
    // Body
    dbus->message_iter_append_basic(&iter, DBUS_TYPE_STRING, &notification_data->message);
    // Actions
    dbus->message_iter_open_container(&iter, DBUS_TYPE_ARRAY, DBUS_TYPE_STRING_AS_STRING, &array);
    for (int i = 0; i < notification_data->num_actions; ++i) {
        dbus->message_iter_append_basic(&array, DBUS_TYPE_STRING, &notification_data->actions[i].button_id);
        dbus->message_iter_append_basic(&array, DBUS_TYPE_STRING, &notification_data->actions[i].button_label);
    }
    dbus->message_iter_close_container(&iter, &array);

    // Hints
    SetCoreHints(dbus, &iter, notification_data->flags, notification_data->icon);

    // Timeout
    const Sint32 timeout = -1;
    dbus->message_iter_append_basic(&iter, DBUS_TYPE_INT32, &timeout);

    DBusError err;
    dbus->error_init(&err);
    DBusMessage *reply = dbus->connection_send_with_reply_and_block(conn, msg, -1, &err);
    if (!reply) {
        if (err.message) {
            SDL_SetError("Notification failed: %s", err.message);
        }
        goto failure;
    }

    dbus->message_unref(msg);

    DBusMessageIter reply_iter;
    dbus->message_iter_init(reply, &reply_iter);
    if (dbus->message_iter_get_arg_type(&reply_iter) != DBUS_TYPE_UINT32) {
        dbus->message_unref(reply);
        goto failure;
    }
    dbus->message_iter_get_basic(&reply_iter, &message_id);
    dbus->message_unref(reply);

    return true;

failure:
    if (msg) {
        dbus->message_unref(msg);
    }
    return false;
}


// org.freedesktop.portal.Notification interface, used when running in a Flatpak or SNAP container

static DBusHandlerResult PortalNotificationFilter(DBusConnection *conn, DBusMessage *msg, void *data)
{
    SDL_DBusContext *dbus = SDL_DBus_GetContext();

    if (dbus->message_is_signal(msg, NOTIFICATION_PORTAL_INTERFACE, NOTIFICATION_SIGNAL_NAME)) {
        DBusMessageIter signal_iter; //, variant_iter;
        const char *str = NULL;

        dbus->message_iter_init(msg, &signal_iter);
        // Check if the parameters are what we expect
        if (dbus->message_iter_get_arg_type(&signal_iter) != DBUS_TYPE_STRING) {
            goto not_our_signal;
        }
        dbus->message_iter_get_basic(&signal_iter, &str);
        const Uint32 id = (Uint32)SDL_strtoul(str, NULL, 10);

        if (!dbus->message_iter_next(&signal_iter)) {
            goto not_our_signal;
        }
        if (dbus->message_iter_get_arg_type(&signal_iter) != DBUS_TYPE_STRING) {
            goto not_our_signal;
        }
        dbus->message_iter_get_basic(&signal_iter, &str);
        if (str) {
            SDL_SendNotificationAction(id, str);
        }

        return DBUS_HANDLER_RESULT_HANDLED;
    }

    not_our_signal:
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void SetPortalIcon(SDL_DBusContext *dbus, DBusMessageIter *iterInit, SDL_Surface *surface)
{
    DBusMessageIter options_pair, variant_iter, struct_iter, array_iter, byte_array_iter;
    const char *key = "icon";
    const char *bytes = "bytes";

    SDL_IOStream *png = SDL_IOFromDynamicMem();
    if (!png) {
        return;
    }
    if (!SDL_SavePNG_IO(surface, png, false)) {
        goto done;
    }
    const Sint64 size = SDL_GetIOSize(png);

    SDL_PropertiesID io_props = SDL_GetIOProperties(png);
    Uint8 *png_ptr = SDL_GetPointerProperty(io_props, SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, NULL);

    dbus->message_iter_open_container(iterInit, DBUS_TYPE_DICT_ENTRY, NULL, &options_pair);
    dbus->message_iter_append_basic(&options_pair, DBUS_TYPE_STRING, &key);
    dbus->message_iter_open_container(&options_pair, DBUS_TYPE_VARIANT, "(sv)", &variant_iter);
    dbus->message_iter_open_container(&variant_iter, DBUS_TYPE_STRUCT, NULL, &struct_iter);
    dbus->message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &bytes);
    dbus->message_iter_open_container(&struct_iter, DBUS_TYPE_VARIANT, "ay", &byte_array_iter);
    dbus->message_iter_open_container(&byte_array_iter, DBUS_TYPE_ARRAY, "y", &array_iter);

    for (Sint64 i = 0; i < size; ++i) {
        if (!dbus->message_iter_append_basic(&array_iter, DBUS_TYPE_BYTE, &png_ptr[i])) {
            goto done;
        }
    }

    dbus->message_iter_close_container(&byte_array_iter, &array_iter);
    dbus->message_iter_close_container(&struct_iter, &byte_array_iter);
    dbus->message_iter_close_container(&variant_iter, &struct_iter);
    dbus->message_iter_close_container(&options_pair, &variant_iter);
    dbus->message_iter_close_container(iterInit, &options_pair);

done:

    SDL_CloseIO(png);
}

static void AddPortalButtons(SDL_DBusContext *dbus, DBusMessageIter *iterInit, const SDL_NotificationData *notification_data)
{
    DBusMessageIter options_pair, options_value, button_array, properties_array;
    const char *key = "buttons";

    dbus->message_iter_open_container(iterInit, DBUS_TYPE_DICT_ENTRY, NULL, &options_pair);
    dbus->message_iter_append_basic(&options_pair, DBUS_TYPE_STRING, &key);
    dbus->message_iter_open_container(&options_pair, DBUS_TYPE_VARIANT, "aa{sv}", &options_value);
    dbus->message_iter_open_container(&options_value, DBUS_TYPE_ARRAY, "a{sv}", &button_array);

    for (int i = 0; i < notification_data->num_actions; ++i) {
        dbus->message_iter_open_container(&button_array, DBUS_TYPE_ARRAY, "{sv}", &properties_array);

        AppendStringOption(dbus, &properties_array, "action", notification_data->actions[i].button_id);
        AppendStringOption(dbus, &properties_array, "label", notification_data->actions[i].button_label);

        dbus->message_iter_close_container(&button_array, &properties_array);
    }

    dbus->message_iter_close_container(&options_value, &button_array);
    dbus->message_iter_close_container(&options_pair, &options_value);
    dbus->message_iter_close_container(iterInit, &options_pair);
}

static void SetPortalDisplayHint(SDL_DBusContext *dbus, DBusMessageIter *iterInit, SDL_NotificationFlags flags)
{
    DBusMessageIter options_pair, options_value, var_struct, string_array;
    const char *key = "display-hint";

    dbus->message_iter_open_container(iterInit, DBUS_TYPE_DICT_ENTRY, NULL, &options_pair);
    dbus->message_iter_append_basic(&options_pair, DBUS_TYPE_STRING, &key);
    dbus->message_iter_open_container(&options_pair, DBUS_TYPE_VARIANT, "(as)", &options_value);
    dbus->message_iter_open_container(&options_value, DBUS_TYPE_STRUCT, NULL, &var_struct);
    dbus->message_iter_open_container(&var_struct, DBUS_TYPE_ARRAY, "s", &string_array);

    if (flags & SDL_NOTIFICATION_TRANSIENT) {
        const char *val = "transient";
        dbus->message_iter_append_basic(&string_array, DBUS_TYPE_STRING, &val);
    }

    dbus->message_iter_close_container(&var_struct, &string_array);
    dbus->message_iter_close_container(&options_value, &var_struct);
    dbus->message_iter_close_container(&options_pair, &options_value);
    dbus->message_iter_close_container(iterInit, &options_pair);
}

static bool InitPortalSignalListener(SDL_DBusContext *dbus)
{
    DBusError error;
    dbus->error_init(&error);

    dbus->bus_add_match(dbus->session_conn,
                        "type='signal', interface='" NOTIFICATION_PORTAL_INTERFACE "',"
                        "member='" NOTIFICATION_SIGNAL_NAME "'",
                        &error);
    if (dbus->error_is_set(&error)) {
        SDL_SetError("Failed to register DBus portal notification signal filter: %s", error.message);
        dbus->error_free(&error);
        return false;
    }

    if (dbus->connection_add_filter(dbus->session_conn, &PortalNotificationFilter, NULL, NULL)) {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Registered DBus portal notification filter");
    }

    dbus->error_free(&error);

    return true;
}

static SDL_NotificationID ShowPortalNotification(const SDL_NotificationData *notification_data)
{
    static SDL_NotificationID notification_id = 0;

    SDL_DBusContext *dbus = SDL_DBus_GetContext();
    DBusConnection *conn = dbus->session_conn;
    DBusMessage *msg = NULL;
    DBusMessageIter iter, array;

    if (!InitPortalSignalListener(dbus)) {
        return 0;
    }

    /* Call Notification.AddNotification() */
    msg = dbus->message_new_method_call(NOTIFICATION_PORTAL_NODE, NOTIFICATION_PORTAL_PATH, NOTIFICATION_PORTAL_INTERFACE, "AddNotification");
    if (msg == NULL) {
        goto failure;
    }

    dbus->message_iter_init_append(msg, &iter);
    // Notification ID
    char id_str[32];
    SDL_snprintf(id_str, SDL_arraysize(id_str), "%u", ++notification_id);
    const char *id = id_str;
    dbus->message_iter_append_basic(&iter, DBUS_TYPE_STRING, &id);

    // Parameters
    dbus->message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &array);
    AppendStringOption(dbus, &array, "title", notification_data->title);
    AppendStringOption(dbus, &array, "body", notification_data->message);

    if (notification_data->flags & ALL_PRIORITY_FLAGS) {
        const char *priority;

        switch (notification_data->flags & ALL_PRIORITY_FLAGS) {
        case SDL_NOTIFICATION_PRIORITY_NORMAL:
        default:
            priority = "normal";
            break;
        case SDL_NOTIFICATION_PRIORITY_LOW:
            priority = "low";
            break;
        case SDL_NOTIFICATION_PRIORITY_HIGH:
            priority = "high";
            break;
        case SDL_NOTIFICATION_PRIORITY_URGENT:
            priority = "urgent";
            break;
        }

        AppendStringOption(dbus, &array, "priority", priority);
    }

    SetPortalDisplayHint(dbus, &array, notification_data->flags);

    if (notification_data->icon) {
        SetPortalIcon(dbus, &array, notification_data->icon);
    }

    if (notification_data->num_actions) {
        AddPortalButtons(dbus, &array, notification_data);
    }

    dbus->message_iter_close_container(&iter, &array);

    DBusError err;
    dbus->error_init(&err);
    DBusMessage *reply = dbus->connection_send_with_reply_and_block(conn, msg, -1, &err);
    dbus->message_unref(msg);

    if (!reply) {
        if (err.message) {
            SDL_SetError("Notification failed: %s", err.message);
        }
        dbus->message_unref(reply);
        dbus->error_free(&err);
        goto failure;
    }

    dbus->message_unref(reply);

    return true;

failure:
    if (msg) {
        dbus->message_unref(msg);
    }
    return false;
}

SDL_NotificationID SDL_SYS_ShowNotification(const SDL_NotificationData *notification_data)
{
    // The portal is only used if inside a container, or the app association can be wrong.
    if (IsInContainer()) {
        return ShowPortalNotification(notification_data);
    } else {
        return ShowCoreNotification(notification_data);
    }
}
