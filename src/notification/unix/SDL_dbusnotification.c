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
#include "../../io/SDL_iostream_c.h"
#include "../../video/SDL_surface_c.h"
#include <SDL3/SDL_iostream.h>

#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

#define NOTIFICATION_PORTAL_NODE      "org.freedesktop.portal.Desktop"
#define NOTIFICATION_PORTAL_PATH      "/org/freedesktop/portal/desktop"
#define NOTIFICATION_PORTAL_INTERFACE "org.freedesktop.portal.Notification"

#define NOTIFICATION_CORE_NODE      "org.freedesktop.Notifications"
#define NOTIFICATION_CORE_PATH      "/org/freedesktop/Notifications"
#define NOTIFICATION_CORE_INTERFACE "org.freedesktop.Notifications"

#define NOTIFICATION_SIGNAL_NAME "ActionInvoked"

#define ALL_PRIORITY_FLAGS (SDL_NOTIFICATION_PRIORITY_LOW | SDL_NOTIFICATION_PRIORITY_NORMAL | \
                            SDL_NOTIFICATION_PRIORITY_HIGH | SDL_NOTIFICATION_PRIORITY_URGENT)

static bool core_listener_registered = false;
static bool portal_listener_registered = false;

static char *icon_uri;

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

static bool SetCoreHints(SDL_DBusContext *dbus, DBusMessageIter *iterInit, SDL_PropertiesID props)
{
    DBusMessageIter iterDict;
    SDL_Surface *icon = SDL_GetPointerProperty(props, SDL_PROP_NOTIFICATION_ICON_POINTER, NULL);
    SDL_NotificationPriority priority = SDL_GetNumberProperty(props, SDL_PROP_NOTIFICATION_PRIORITY_NUMBER, SDL_NOTIFICATION_PRIORITY_NORMAL);
    const bool transient = SDL_GetBooleanProperty(props, SDL_PROP_NOTIFICATION_TRANSIENT_BOOLEAN, false);
    const bool silent = SDL_GetBooleanProperty(props, SDL_PROP_NOTIFICATION_SILENT_BOOLEAN, false);

    if (!dbus->message_iter_open_container(iterInit, DBUS_TYPE_ARRAY, "{sv}", &iterDict)) {
        goto failed;
    }

    Uint8 dbus_priority;

    switch (priority) {
    case SDL_NOTIFICATION_PRIORITY_NORMAL:
    case SDL_NOTIFICATION_PRIORITY_HIGH:
    default:
        dbus_priority = 1;
        break;
    case SDL_NOTIFICATION_PRIORITY_LOW:
        dbus_priority = 0;
        break;
    case SDL_NOTIFICATION_PRIORITY_URGENT:
        dbus_priority = 2;
        break;
    }

    AppendOption(dbus, &iterDict, DBUS_TYPE_BYTE_AS_STRING, "urgency", &dbus_priority);

    const dbus_bool_t db_transient = transient;
    AppendOption(dbus, &iterDict, DBUS_TYPE_BOOLEAN_AS_STRING, "transient", &db_transient);

    if (!silent) {
        AppendStringOption(dbus, &iterDict, "sound-name", "dialog-information");
    }

    if (icon) {
        SDL_Surface *icon_surface = icon;
        if (icon->format != SDL_PIXELFORMAT_ABGR8888) {
            icon_surface = SDL_ConvertSurface(icon, SDL_PIXELFORMAT_ABGR8888);
        }

        SetCoreIcon(dbus, &iterDict, icon_surface);

        if (icon_surface != icon) {
            SDL_DestroySurface(icon_surface);
        }
    }

    if (!dbus->message_iter_close_container(iterInit, &iterDict)) {
        goto failed;
    }

    return true;

failed:

    return false;
}

static bool InitCoreSignalListener(SDL_DBusContext *dbus)
{
    if (core_listener_registered) {
        return true;
    }

    DBusError error;
    dbus->error_init(&error);

    dbus->bus_add_match(dbus->session_conn,
                        "type='signal', interface='" NOTIFICATION_CORE_INTERFACE "',"
                        "member='" NOTIFICATION_SIGNAL_NAME "'",
                        &error);
    if (dbus->error_is_set(&error)) {
        SDL_SetError("Failed to register DBus portal notification signal filter: %s", error.message);
        dbus->error_free(&error);
        return false;
    }
    dbus->error_free(&error);

    if (dbus->connection_add_filter(dbus->session_conn, &CoreNotificationFilter, NULL, NULL)) {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Registered DBus portal notification filter");
    }
    dbus->connection_flush(dbus->session_conn);

    core_listener_registered = true;
    return true;
}

static const char *GetIconURI()
{
    if (icon_uri) {
        return icon_uri;
    }

    SDL_PropertiesID props = SDL_GetGlobalProperties();
    SDL_Surface *icon = SDL_GetPointerProperty(props, SDL_PROP_GLOBAL_NOTIFICATION_HEADER_ICON_POINTER, NULL);
    if (!icon) {
        return NULL;
    }

    static const char template[] = "/SDL_notificationicon-XXXXXX.png";
    const char *xdg_path;
    char tmp_path[PATH_MAX];

    xdg_path = SDL_getenv("XDG_RUNTIME_DIR");
    if (!xdg_path) {
        return NULL;
    }

    SDL_strlcpy(tmp_path, xdg_path, PATH_MAX);
    SDL_strlcat(tmp_path, template, PATH_MAX);

    int fd = mkostemps(tmp_path, 4, O_CLOEXEC);
    if (fd < 0) {
        goto done;
    }

    SDL_IOStream *icon_file = SDL_IOFromFD(fd, true);
    if (!icon_file) {
        goto done;
    }
    if (!SDL_SavePNG_IO(icon, icon_file, true)) {
        goto done;
    }

    fd = -1;
    SDL_asprintf(&icon_uri, "file://%s", tmp_path);

done:
    if (fd >= 0) {
        if (icon_file) {
            SDL_CloseIO(icon_file);
        } else {
            close(fd);
        }
        unlink(tmp_path);
    }

    return icon_uri;
}

static SDL_NotificationID ShowCoreNotification(SDL_DBusContext *dbus, SDL_PropertiesID props)
{
    DBusConnection *conn = dbus->session_conn;
    DBusMessage *msg = NULL;
    DBusMessageIter iter, array;
    const char *id = SDL_GetAppMetadataProperty(SDL_PROP_APP_METADATA_NAME_STRING);
    Uint32 message_id = 0;

    if (!InitCoreSignalListener(dbus)) {
        return 0;
    }

    const SDL_PropertiesID replaces = SDL_GetNumberProperty(props, SDL_PROP_NOTIFICATION_REPLACES_NUMBER, 0);
    const char *title = SDL_GetStringProperty(props, SDL_PROP_NOTIFICATION_TITLE_STRING, NULL);
    const char *message = SDL_GetStringProperty(props, SDL_PROP_NOTIFICATION_MESSAGE_STRING, NULL);
    const SDL_NotificationAction **actions = SDL_GetPointerProperty(props, SDL_PROP_NOTIFICATION_ACTIONS_POINTER, NULL);

    // Call org.freedesktop.Notifications.Notify()
    msg = dbus->message_new_method_call(NOTIFICATION_CORE_NODE, NOTIFICATION_CORE_PATH, NOTIFICATION_CORE_INTERFACE, "Notify");
    if (msg == NULL) {
        goto failure;
    }

    dbus->message_iter_init_append(msg, &iter);
    // App ID
    if (!id) {
        SDL_GetAppID();
    }
    dbus->message_iter_append_basic(&iter, DBUS_TYPE_STRING, &id);
    // Replaces id
    const Uint32 uid = replaces;
    dbus->message_iter_append_basic(&iter, DBUS_TYPE_UINT32, &uid);
    // Icon URI
    id = GetIconURI();
    dbus->message_iter_append_basic(&iter, DBUS_TYPE_STRING, &id);
    // Summary
    dbus->message_iter_append_basic(&iter, DBUS_TYPE_STRING, &title);
    // Body
    dbus->message_iter_append_basic(&iter, DBUS_TYPE_STRING, &message);
    // Actions
    dbus->message_iter_open_container(&iter, DBUS_TYPE_ARRAY, DBUS_TYPE_STRING_AS_STRING, &array);
    for (int i = 0; actions[i]; ++i) {
        dbus->message_iter_append_basic(&array, DBUS_TYPE_STRING, &actions[i]->button_id);
        dbus->message_iter_append_basic(&array, DBUS_TYPE_STRING, &actions[i]->button_label);
    }
    dbus->message_iter_close_container(&iter, &array);

    // Hints
    SetCoreHints(dbus, &iter, props);

    // Timeout
    const Sint32 timeout = -1;
    dbus->message_iter_append_basic(&iter, DBUS_TYPE_INT32, &timeout);

    DBusError error;
    dbus->error_init(&error);
    DBusMessage *reply = dbus->connection_send_with_reply_and_block(conn, msg, -1, &error);
    if (!reply) {
        if (error.message) {
            SDL_SetError("Notification failed: %s", error.message);
        }
        dbus->error_free(&error);
        goto failure;
    }

    dbus->error_free(&error);
    dbus->message_unref(msg);

    DBusMessageIter reply_iter;
    dbus->message_iter_init(reply, &reply_iter);
    if (dbus->message_iter_get_arg_type(&reply_iter) != DBUS_TYPE_UINT32) {
        dbus->message_unref(reply);
        goto failure;
    }
    dbus->message_iter_get_basic(&reply_iter, &message_id);
    dbus->message_unref(reply);

    return message_id;

failure:
    if (msg) {
        dbus->message_unref(msg);
    }
    return 0;
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

        // Parse the ID.
        Uint32 id = 0;
        const int ret = SDL_sscanf(str, "SDL3_DBusPortalNotification-%" SDL_PRIu32, &id);
        if (ret != 1) {
            goto not_our_signal;
        }

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

static void SetPortalSound(SDL_DBusContext *dbus, DBusMessageIter *iterInit, bool silent)
{
    DBusMessageIter options_pair, variant_iter, struct_iter, sound_val_iter;
    const char *key = "sound";
    const char *val = silent ? "silent" : "default";

    dbus->message_iter_open_container(iterInit, DBUS_TYPE_DICT_ENTRY, NULL, &options_pair);
    dbus->message_iter_append_basic(&options_pair, DBUS_TYPE_STRING, &key);
    dbus->message_iter_open_container(&options_pair, DBUS_TYPE_VARIANT, "(sv)", &variant_iter);
    dbus->message_iter_open_container(&variant_iter, DBUS_TYPE_STRUCT, NULL, &struct_iter);
    dbus->message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &key);
    dbus->message_iter_open_container(&struct_iter, DBUS_TYPE_VARIANT, "s", &sound_val_iter);
    dbus->message_iter_append_basic(&sound_val_iter, DBUS_TYPE_STRING, &val);
    dbus->message_iter_close_container(&struct_iter, &sound_val_iter);
    dbus->message_iter_close_container(&variant_iter, &struct_iter);
    dbus->message_iter_close_container(&options_pair, &variant_iter);
    dbus->message_iter_close_container(iterInit, &options_pair);
}

static void AddPortalButtons(SDL_DBusContext *dbus, DBusMessageIter *iterInit, const SDL_NotificationAction **actions)
{
    DBusMessageIter options_pair, options_value, button_array, properties_array;
    const char *key = "buttons";

    dbus->message_iter_open_container(iterInit, DBUS_TYPE_DICT_ENTRY, NULL, &options_pair);
    dbus->message_iter_append_basic(&options_pair, DBUS_TYPE_STRING, &key);
    dbus->message_iter_open_container(&options_pair, DBUS_TYPE_VARIANT, "aa{sv}", &options_value);
    dbus->message_iter_open_container(&options_value, DBUS_TYPE_ARRAY, "a{sv}", &button_array);

    for (int i = 0; actions[i]; ++i) {
        dbus->message_iter_open_container(&button_array, DBUS_TYPE_ARRAY, "{sv}", &properties_array);

        AppendStringOption(dbus, &properties_array, "action", actions[i]->button_id);
        AppendStringOption(dbus, &properties_array, "label", actions[i]->button_label);

        dbus->message_iter_close_container(&button_array, &properties_array);
    }

    dbus->message_iter_close_container(&options_value, &button_array);
    dbus->message_iter_close_container(&options_pair, &options_value);
    dbus->message_iter_close_container(iterInit, &options_pair);
}

static void SetPortalDisplayHints(SDL_DBusContext *dbus, DBusMessageIter *iterInit, SDL_PropertiesID props)
{
    DBusMessageIter options_pair, options_value, var_struct, string_array;
    const char *key = "display-hint";
    const bool transient = SDL_GetBooleanProperty(props, SDL_PROP_NOTIFICATION_TRANSIENT_BOOLEAN, false);

    dbus->message_iter_open_container(iterInit, DBUS_TYPE_DICT_ENTRY, NULL, &options_pair);
    dbus->message_iter_append_basic(&options_pair, DBUS_TYPE_STRING, &key);
    dbus->message_iter_open_container(&options_pair, DBUS_TYPE_VARIANT, "(as)", &options_value);
    dbus->message_iter_open_container(&options_value, DBUS_TYPE_STRUCT, NULL, &var_struct);
    dbus->message_iter_open_container(&var_struct, DBUS_TYPE_ARRAY, "s", &string_array);

    if (transient) {
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
    if (portal_listener_registered) {
        return true;
    }

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
    dbus->error_free(&error);

    if (dbus->connection_add_filter(dbus->session_conn, &PortalNotificationFilter, NULL, NULL)) {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Registered DBus portal notification filter");
    }
    dbus->connection_flush(dbus->session_conn);

    portal_listener_registered = true;
    return true;
}

static SDL_NotificationID GetNewID()
{
    SDL_NotificationID random_id = 0;

    /* Generate a reasonably unique ID for the notification
     * with a low chance of collision.
     *
     * Try /dev/urandom first, then fall back to the time.
     */
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        open("/dev/random", O_RDONLY);
    }
    if (fd >= 0) {
        if (read(fd, &random_id, sizeof(random_id)) != sizeof(random_id)) {
            random_id = 0;
        }
        close(fd);
    }
    if (!random_id) {
        random_id = (SDL_NotificationID)SDL_GetTicksNS();
    }

    return random_id;
}

static SDL_NotificationID ShowPortalNotification(SDL_DBusContext *dbus, SDL_PropertiesID props)
{
    DBusConnection *conn = dbus->session_conn;
    DBusMessage *msg = NULL;
    DBusMessageIter iter, array;

    if (!InitPortalSignalListener(dbus)) {
        return 0;
    }

    const SDL_PropertiesID replaces = SDL_GetNumberProperty(props, SDL_PROP_NOTIFICATION_REPLACES_NUMBER, 0);
    const char *title = SDL_GetStringProperty(props, SDL_PROP_NOTIFICATION_TITLE_STRING, NULL);
    const char *message = SDL_GetStringProperty(props, SDL_PROP_NOTIFICATION_MESSAGE_STRING, NULL);
    const SDL_NotificationPriority priority = SDL_GetNumberProperty(props, SDL_PROP_NOTIFICATION_PRIORITY_NUMBER, SDL_NOTIFICATION_PRIORITY_NORMAL);
    SDL_Surface *icon = SDL_GetPointerProperty(props, SDL_PROP_NOTIFICATION_ICON_POINTER, NULL);
    const SDL_NotificationAction **actions = SDL_GetPointerProperty(props, SDL_PROP_NOTIFICATION_ACTIONS_POINTER, NULL);
    const bool silent = SDL_GetBooleanProperty(props, SDL_PROP_NOTIFICATION_SILENT_BOOLEAN, false);

    // Call Notification.AddNotification()
    msg = dbus->message_new_method_call(NOTIFICATION_PORTAL_NODE, NOTIFICATION_PORTAL_PATH, NOTIFICATION_PORTAL_INTERFACE, "AddNotification");
    if (msg == NULL) {
        goto failure;
    }

    dbus->message_iter_init_append(msg, &iter);

    // Notification ID
    char id_str[128];
    const Uint32 new_id = replaces ? replaces : GetNewID();
    if (!new_id) {
        SDL_SetError("Failed to generate an ID for the notification");
        goto failure;
    }
    SDL_snprintf(id_str, SDL_arraysize(id_str), "SDL3_DBusPortalNotification-%" SDL_PRIu32, new_id);
    const char *id = id_str;
    dbus->message_iter_append_basic(&iter, DBUS_TYPE_STRING, &id);

    // Parameters
    dbus->message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &array);
    AppendStringOption(dbus, &array, "title", title);
    AppendStringOption(dbus, &array, "body", message);

    SetPortalSound(dbus, &array, silent);

    const char *priority_str;

    switch (priority) {
    case SDL_NOTIFICATION_PRIORITY_NORMAL:
    default:
        priority_str = "normal";
        break;
    case SDL_NOTIFICATION_PRIORITY_LOW:
        priority_str = "low";
        break;
    case SDL_NOTIFICATION_PRIORITY_HIGH:
        priority_str = "high";
        break;
    case SDL_NOTIFICATION_PRIORITY_URGENT:
        priority_str = "urgent";
        break;
    }

    AppendStringOption(dbus, &array, "priority", priority_str);
    SetPortalDisplayHints(dbus, &array, props);

    if (icon) {
        SDL_Surface *icon_surface = icon;
        if (icon_surface->format != SDL_PIXELFORMAT_ABGR8888) {
            icon_surface = SDL_ConvertSurface(icon_surface, SDL_PIXELFORMAT_ABGR8888);
        }

        SetPortalIcon(dbus, &array, icon_surface);

        if (icon_surface != icon) {
            SDL_DestroySurface(icon_surface);
        }
    }

    if (actions) {
        AddPortalButtons(dbus, &array, actions);
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

    return new_id;

failure:
    if (msg) {
        dbus->message_unref(msg);
    }
    return 0;
}

SDL_NotificationID SDL_SYS_ShowNotification(SDL_PropertiesID props)
{
    SDL_DBusContext *dbus = SDL_DBus_GetContext();

    if (!dbus || !dbus->session_conn) {
        return 0;
    }

    // The portal is only used if inside a container, or the app association can be wrong.
    if (IsInContainer()) {
        return ShowPortalNotification(dbus, props);
    } else {
        return ShowCoreNotification(dbus, props);
    }
}

void SDL_CleanupNotifications()
{
    SDL_DBusContext *dbus = SDL_DBus_GetContext();

    if (dbus && dbus->session_conn) {
        DBusConnection *conn = dbus->session_conn;

        if (portal_listener_registered) {
            dbus->connection_remove_filter(conn, &PortalNotificationFilter, NULL);
            dbus->bus_remove_match(conn,
                                   "type='signal', interface='" NOTIFICATION_PORTAL_INTERFACE "',"
                                   "member='" NOTIFICATION_SIGNAL_NAME "'",
                                   NULL);
        }
        if (core_listener_registered) {
            dbus->connection_remove_filter(conn, &CoreNotificationFilter, NULL);
            dbus->bus_remove_match(dbus->session_conn,
                                   "type='signal', interface='" NOTIFICATION_CORE_INTERFACE "',"
                                   "member='" NOTIFICATION_SIGNAL_NAME "'",
                                   NULL);
        }
        dbus->connection_flush(conn);

        portal_listener_registered = false;
        core_listener_registered = false;
    }

    if (icon_uri) {
        unlink(icon_uri);
        SDL_free(icon_uri);
    }
}

bool SDL_RequestNotificationPermission(void)
{
    // TODO: Anything to do here?
    return true;
}
