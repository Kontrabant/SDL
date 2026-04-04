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

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef HAVE_MEMFD_CREATE
#include <sys/mman.h>
#endif

#define NOTIFICATION_PORTAL_NODE      "org.freedesktop.portal.Desktop"
#define NOTIFICATION_PORTAL_PATH      "/org/freedesktop/portal/desktop"
#define NOTIFICATION_PORTAL_INTERFACE "org.freedesktop.portal.Notification"

#define NOTIFICATION_CORE_NODE      "org.freedesktop.Notifications"
#define NOTIFICATION_CORE_PATH      "/org/freedesktop/Notifications"
#define NOTIFICATION_CORE_INTERFACE "org.freedesktop.Notifications"

#define NOTIFICATION_ACTION_SIGNAL_NAME           "ActionInvoked"
#define NOTIFICATION_CLOSED_SIGNAL_NAME           "NotificationClosed"
#define NOTIFICATION_ACTIVATION_TOKEN_SIGNAL_NAME "ActivationToken"

#define SDL_NOTIFICATION_PREAMBLE "SDL_LocalNotification-"

static char *icon_uri;
static Uint64 session_id;

static Uint32 core_id_list[32];
static Uint32 core_id_count;

static Uint32 interface_version;

static bool core_listener_registered;
static bool portal_listener_registered;

static void GetRandom(void *dst, size_t size)
{
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        fd = open("/dev/random", O_RDONLY);
    }
    if (fd >= 0) {
        while (read(fd, dst, size) != size) {
        }
        close(fd);
    } else {
        size_t written = 0;

        while (written < size) {
            const Uint64 tval = SDL_GetTicksNS();
            const size_t towrite = SDL_min(size - written, sizeof(tval));
            SDL_memcpy((Uint8 *)dst + written, &tval, towrite);
            written += towrite;
        }
    }
}

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

static bool AppendOption(SDL_DBusContext *dbus, DBusMessageIter *options, const char *type, const char *key, const void *value)
{
    DBusMessageIter options_pair, options_value;

    if (!dbus->message_iter_open_container(options, DBUS_TYPE_DICT_ENTRY, NULL, &options_pair)) {
        return false;
    }
    if (!dbus->message_iter_append_basic(&options_pair, DBUS_TYPE_STRING, &key)) {
        return false;
    }
    if (!dbus->message_iter_open_container(&options_pair, DBUS_TYPE_VARIANT, type, &options_value)) {
        return false;
    }
    if (!dbus->message_iter_append_basic(&options_value, (int)type[0], value)) {
        return false;
    }
    if (!dbus->message_iter_close_container(&options_pair, &options_value)) {
        return false;
    }
    return (bool)dbus->message_iter_close_container(options, &options_pair);
}

static bool AppendStringOption(SDL_DBusContext *dbus, DBusMessageIter *options, const char *key, const char *value)
{
    DBusMessageIter options_pair, options_value;

    if (!dbus->message_iter_open_container(options, DBUS_TYPE_DICT_ENTRY, NULL, &options_pair)) {
        return false;
    }
    if (!dbus->message_iter_append_basic(&options_pair, DBUS_TYPE_STRING, &key)) {
        return false;
    }
    if (!dbus->message_iter_open_container(&options_pair, DBUS_TYPE_VARIANT, DBUS_TYPE_STRING_AS_STRING, &options_value)) {
        return false;
    }
    if (!dbus->message_iter_append_basic(&options_value, DBUS_TYPE_STRING, &value)) {
        return false;
    }
    if (!dbus->message_iter_close_container(&options_pair, &options_value)) {
        return false;
    }
    return (bool)dbus->message_iter_close_container(options, &options_pair);
}

static bool AppendTargetString(SDL_DBusContext *dbus, DBusMessageIter *options, const char *key)
{
    char target_buf[128];
    const char *target_val = target_buf;
    DBusMessageIter options_pair, target_variant, target_string;

    SDL_snprintf(target_buf, sizeof(target_buf), "%" SDL_PRIu64, session_id);

    if (!dbus->message_iter_open_container(options, DBUS_TYPE_DICT_ENTRY, NULL, &options_pair)) {
        return false;
    }
    if (!dbus->message_iter_append_basic(&options_pair, DBUS_TYPE_STRING, &key)) {
        return false;
    }
    if (!dbus->message_iter_open_container(&options_pair, DBUS_TYPE_VARIANT, DBUS_TYPE_VARIANT_AS_STRING, &target_variant)) {
        return false;
    }
    if (!dbus->message_iter_open_container(&target_variant, DBUS_TYPE_VARIANT, DBUS_TYPE_STRING_AS_STRING, &target_string)) {
        return false;
    }
    if (!dbus->message_iter_append_basic(&target_string, DBUS_TYPE_STRING, &target_val)) {
        return false;
    }
    if (!dbus->message_iter_close_container(&target_variant, &target_string)) {
        return false;
    }
    if (!dbus->message_iter_close_container(&options_pair, &target_variant)) {
        return false;
    }
    return (bool)dbus->message_iter_close_container(options, &options_pair);
}

static void RemoveIDFromListAtIndex(Uint32 index)
{
    --core_id_count;
    if (index < core_id_count) {
        SDL_memmove(&core_id_list[index], &core_id_list[index + 1], (core_id_count - index) * sizeof(Uint32));
    }
}

// org.freedesktop.Notifications, used when not running inside a container.
static DBusHandlerResult CoreNotificationFilter(DBusConnection *conn, DBusMessage *msg, void *data)
{
    SDL_DBusContext *dbus = SDL_DBus_GetContext();

    if (dbus->message_is_signal(msg, NOTIFICATION_CORE_NODE, NOTIFICATION_ACTION_SIGNAL_NAME)) {
        DBusMessageIter signal_iter; // variant_iter;
        const char *button = NULL;
        Uint32 id = 0;
        bool own_id = false;

        if (!dbus->message_iter_init(msg, &signal_iter)) {
            goto not_our_signal;
        }

        // Check if the parameters are what we expect
        if (dbus->message_iter_get_arg_type(&signal_iter) != DBUS_TYPE_UINT32) {
            goto not_our_signal;
        }
        dbus->message_iter_get_basic(&signal_iter, &id);

        // See if this signal is for this client.
        for (Uint32 i = 0; i < core_id_count; ++i) {
            if (id == core_id_list[i]) {
                RemoveIDFromListAtIndex(i);
                own_id = true;
                break;
            }
        }
        if (!own_id) {
            goto not_our_signal;
        }

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
    } else if (dbus->message_is_signal(msg, NOTIFICATION_CORE_NODE, NOTIFICATION_CLOSED_SIGNAL_NAME)) {
        DBusMessageIter signal_iter; // variant_iter;
        Uint32 id = 0, reason = 0;

        dbus->message_iter_init(msg, &signal_iter);
        // Check if the parameters are what we expect
        if (dbus->message_iter_get_arg_type(&signal_iter) != DBUS_TYPE_UINT32) {
            goto not_our_signal;
        }
        dbus->message_iter_get_basic(&signal_iter, &id);

        if (!dbus->message_iter_next(&signal_iter)) {
            goto not_our_signal;
        }
        if (dbus->message_iter_get_arg_type(&signal_iter) != DBUS_TYPE_UINT32) {
            goto not_our_signal;
        }
        dbus->message_iter_get_basic(&signal_iter, &reason);
        if (id && reason) {
            for (Uint32 i = 0; i < core_id_count; ++i) {
                if (core_id_list[i] == id) {
                    RemoveIDFromListAtIndex(i);
                    return DBUS_HANDLER_RESULT_HANDLED;
                }
            }
        }
    } else if (dbus->message_is_signal(msg, NOTIFICATION_CORE_NODE, NOTIFICATION_ACTIVATION_TOKEN_SIGNAL_NAME)) {
        DBusMessageIter signal_iter; // variant_iter;
        const char *token = NULL;
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
        dbus->message_iter_get_basic(&signal_iter, &token);
        if (id && token) {
            for (Uint32 i = 0; i < core_id_count; ++i) {
                if (core_id_list[i] == id) {
                    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "XDG_ACTIVATION_TOKEN", token, true);
                    return DBUS_HANDLER_RESULT_HANDLED;
                }
            }
        }
    }

not_our_signal:
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

// Only add the filter if at least one of the settings we want is present.

static bool SetCoreImage(SDL_DBusContext *dbus, DBusMessageIter *iterInit, SDL_Surface *surface)
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

    if (!dbus->message_iter_close_container(&iter, &array)) {
        return false;
    }
    if (!dbus->message_iter_close_container(&iterValue, &iter)) {
        return false;
    }
    if (!dbus->message_iter_close_container(&iterEntry, &iterValue)) {
        return false;
    }
    if (!dbus->message_iter_close_container(iterInit, &iterEntry)) {
        return false;
    }

    return true;
}

static bool SetCoreHints(SDL_DBusContext *dbus, DBusMessageIter *iterInit, SDL_PropertiesID props)
{
    DBusMessageIter iterDict;
    const char *app_id = SDL_GetAppMetadataProperty(SDL_PROP_APP_METADATA_IDENTIFIER_STRING);
    SDL_Surface *icon = SDL_GetPointerProperty(props, SDL_PROP_NOTIFICATION_IMAGE_POINTER, NULL);
    SDL_NotificationPriority priority = SDL_GetNumberProperty(props, SDL_PROP_NOTIFICATION_PRIORITY_NUMBER, SDL_NOTIFICATION_PRIORITY_NORMAL);
    const bool transient = SDL_GetBooleanProperty(props, SDL_PROP_NOTIFICATION_TRANSIENT_BOOLEAN, false);
    const bool silent = SDL_GetBooleanProperty(props, SDL_PROP_NOTIFICATION_SILENT_BOOLEAN, false);

    if (!dbus->message_iter_open_container(iterInit, DBUS_TYPE_ARRAY, "{sv}", &iterDict)) {
        return false;
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
    case SDL_NOTIFICATION_PRIORITY_CRITICAL:
        dbus_priority = 2;
        break;
    }

    if (!AppendOption(dbus, &iterDict, DBUS_TYPE_BYTE_AS_STRING, "urgency", &dbus_priority)) {
        return false;
    }

    if (app_id) {
        if (!AppendOption(dbus, &iterDict, DBUS_TYPE_STRING_AS_STRING, "desktop-entry", &app_id)) {
            return false;
        }
    }

    const dbus_bool_t db_transient = transient;
    if (!AppendOption(dbus, &iterDict, DBUS_TYPE_BOOLEAN_AS_STRING, "transient", &db_transient)) {
        return false;
    }

    if (!silent) {
        if (!AppendStringOption(dbus, &iterDict, "sound-name", "dialog-information")) {
            return false;
        }
    }

    if (icon) {
        SDL_Surface *icon_surface = icon;
        if (icon->format != SDL_PIXELFORMAT_ABGR8888) {
            icon_surface = SDL_ConvertSurface(icon, SDL_PIXELFORMAT_ABGR8888);
        }

        SetCoreImage(dbus, &iterDict, icon_surface);

        if (icon_surface != icon) {
            SDL_DestroySurface(icon_surface);
        }
    }

    if (!dbus->message_iter_close_container(iterInit, &iterDict)) {
        return false;
    }

    return true;
}

static bool InitCoreSignalListener(SDL_DBusContext *dbus)
{
    if (core_listener_registered) {
        return true;
    }

    if (!session_id) {
        GetRandom(&session_id, sizeof(session_id));
    }

    DBusError error;
    dbus->error_init(&error);

    dbus->bus_add_match(dbus->session_conn,
                        "type='signal', interface='" NOTIFICATION_CORE_INTERFACE "',"
                        "member='" NOTIFICATION_ACTION_SIGNAL_NAME "'",
                        &error);
    if (dbus->error_is_set(&error)) {
        SDL_SetError("Failed to register DBus portal notification signal filter: %s", error.message);
        dbus->error_free(&error);
        return false;
    }
    dbus->error_free(&error);

    dbus->bus_add_match(dbus->session_conn,
                        "type='signal', interface='" NOTIFICATION_CORE_INTERFACE "',"
                        "member='" NOTIFICATION_CLOSED_SIGNAL_NAME "'",
                        &error);
    if (dbus->error_is_set(&error)) {
        SDL_SetError("Failed to register DBus portal notification signal filter: %s", error.message);
        dbus->error_free(&error);
        return false;
    }
    dbus->error_free(&error);

    dbus->bus_add_match(dbus->session_conn,
                        "type='signal', interface='" NOTIFICATION_CORE_INTERFACE "',"
                        "member='" NOTIFICATION_ACTIVATION_TOKEN_SIGNAL_NAME "'",
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

    const Uint32 hash = SDL_murmur3_32(icon->pixels, icon->pitch * icon->h, 0);

    SDL_IOStream *icon_file = NULL;
    const char *xdg_path;
    char tmp_path[PATH_MAX];

    xdg_path = SDL_getenv("XDG_RUNTIME_DIR");
    if (!xdg_path) {
        return NULL;
    }

    // Some implementations don't like the icon filename to be too long.
    SDL_snprintf(tmp_path, sizeof(tmp_path), "%s/SDL_icon-%" SDL_PRIu32 ".png", xdg_path, hash);

    int fd = open(tmp_path, O_RDWR | O_CREAT | O_CLOEXEC | O_EXCL, S_IRUSR | S_IWUSR);
    if (fd < 0 && errno != EEXIST) {
        goto done;
    }

    if (fd >= 0) {
        icon_file = SDL_IOFromFD(fd, true);
        if (!icon_file) {
            goto done;
        }
        if (!SDL_SavePNG_IO(icon, icon_file, true)) {
            goto done;
        }
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
    const char *tmpstr = NULL;
    const Sint32 timeout = -1;
    Uint32 message_id = 0;
    bool error_set = false;

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
    tmpstr = SDL_GetAppMetadataProperty(SDL_PROP_APP_METADATA_NAME_STRING);
    if (!tmpstr) {
        SDL_GetAppID();
    }
    if (!dbus->message_iter_append_basic(&iter, DBUS_TYPE_STRING, &tmpstr)) {
        goto failure;
    }

    // Replaces id
    if (!dbus->message_iter_append_basic(&iter, DBUS_TYPE_UINT32, &replaces)) {
        goto failure;
    }

    // Icon URI
    tmpstr = GetIconURI();
    if (!tmpstr) {
        tmpstr = "";
    }
    if (!dbus->message_iter_append_basic(&iter, DBUS_TYPE_STRING, &tmpstr)) {
        goto failure;
    }

    // Summary
    if (!dbus->message_iter_append_basic(&iter, DBUS_TYPE_STRING, &title)) {
        goto failure;
    }

    // Body
    tmpstr = message ? message : "";
    if (!dbus->message_iter_append_basic(&iter, DBUS_TYPE_STRING, &message)) {
        goto failure;
    }

    {
        // Actions
        if (!dbus->message_iter_open_container(&iter, DBUS_TYPE_ARRAY, DBUS_TYPE_STRING_AS_STRING, &array)) {
            goto failure;
        }

        // Add the default action
        tmpstr = "default";
        if (!dbus->message_iter_append_basic(&array, DBUS_TYPE_STRING, &tmpstr)) {
            goto failure;
        }
        if (!dbus->message_iter_append_basic(&array, DBUS_TYPE_STRING, &tmpstr)) {
            goto failure;
        }

        // Add the actions
        if (actions) {
            for (int i = 0; actions[i]; ++i) {
                if (!dbus->message_iter_append_basic(&array, DBUS_TYPE_STRING, &actions[i]->action_id)) {
                    goto failure;
                }
                if (!dbus->message_iter_append_basic(&array, DBUS_TYPE_STRING, &actions[i]->action_label)) {
                    goto failure;
                }
            }
        }
        if (!dbus->message_iter_close_container(&iter, &array)) {
            goto failure;
        }
    }

    // Hints
    if (!SetCoreHints(dbus, &iter, props)) {
        goto failure;
    }

    // Timeout
    if (!dbus->message_iter_append_basic(&iter, DBUS_TYPE_INT32, &timeout)) {
        goto failure;
    }

    {
        DBusMessageIter reply_iter;
        DBusError error;

        dbus->error_init(&error);
        DBusMessage *reply = dbus->connection_send_with_reply_and_block(conn, msg, -1, &error);
        if (!reply) {
            if (error.message) {
                SDL_SetError("Notification failed: %s", error.message);
                error_set = true;
            }
            dbus->error_free(&error);
            goto failure;
        }

        dbus->error_free(&error);
        dbus->message_unref(msg);

        if (!dbus->message_iter_init(reply, &reply_iter)) {
            goto failure;
        }
        if (dbus->message_iter_get_arg_type(&reply_iter) != DBUS_TYPE_UINT32) {
            dbus->message_unref(reply);
            goto failure;
        }
        dbus->message_iter_get_basic(&reply_iter, &message_id);
        dbus->message_unref(reply);
    }

    if (core_id_count == SDL_arraysize(core_id_list)) {
        RemoveIDFromListAtIndex(0);
    }
    core_id_list[core_id_count++] = message_id;

    return message_id;

failure:
    if (msg) {
        dbus->message_unref(msg);
    }
    if (!error_set) {
        SDL_SetError("Failed to dispatch org.freedesktop.Notifications request");
    }
    return 0;
}

static bool RemoveCoreNotification(SDL_DBusContext *dbus, SDL_NotificationID id)
{
    if (!id) {
        return SDL_InvalidParamError("id");
    }

    DBusMessageIter iter;
    bool ret = false;

    // Call org.freedesktop.Notifications.CloseNotification()
    DBusMessage *msg = dbus->message_new_method_call(NOTIFICATION_CORE_NODE, NOTIFICATION_CORE_PATH, NOTIFICATION_CORE_INTERFACE, "CloseNotification");
    if (!msg) {
        return SDL_OutOfMemory();
    }

    dbus->message_iter_init_append(msg, &iter);
    if (dbus->message_iter_append_basic(&iter, DBUS_TYPE_UINT32, &id)) {
        ret = (bool)dbus->connection_send(dbus->session_conn, msg, NULL);
        if (!ret) {
            SDL_SetError("Failed to send notification removal request");
        }
    } else {
        ret = SDL_OutOfMemory();
    }

    dbus->message_unref(msg);
    return ret;
}

// org.freedesktop.portal.Notification interface, used when running in a Flatpak or SNAP container
static DBusHandlerResult PortalNotificationFilter(DBusConnection *conn, DBusMessage *msg, void *data)
{
    SDL_DBusContext *dbus = SDL_DBus_GetContext();

    if (dbus->message_is_signal(msg, NOTIFICATION_PORTAL_INTERFACE, NOTIFICATION_ACTION_SIGNAL_NAME)) {
        DBusMessageIter signal_iter; //, variant_iter;
        const char *str = NULL;

        if (!dbus->message_iter_init(msg, &signal_iter)) {
            goto not_our_signal;
        }

        // Check if the parameters are what we expect
        if (dbus->message_iter_get_arg_type(&signal_iter) != DBUS_TYPE_STRING) {
            goto not_our_signal;
        }
        dbus->message_iter_get_basic(&signal_iter, &str);

        // Parse the ID.
        if (SDL_strncmp(str, SDL_NOTIFICATION_PREAMBLE, sizeof(SDL_NOTIFICATION_PREAMBLE) - 1) != 0) {
            goto not_our_signal;
        }
        const Uint32 id = (Uint32)SDL_strtoul(str + sizeof(SDL_NOTIFICATION_PREAMBLE), NULL, 10);
        if (!id) {
            goto not_our_signal;
        }

        if (!dbus->message_iter_next(&signal_iter)) {
            goto not_our_signal;
        }
        if (dbus->message_iter_get_arg_type(&signal_iter) != DBUS_TYPE_STRING) {
            goto not_our_signal;
        }
        dbus->message_iter_get_basic(&signal_iter, &str);

        // Check for the target and optional XDG activation parameter.
        const char *target = NULL, *activation_token = NULL;
        if (dbus->message_iter_next(&signal_iter) && dbus->message_iter_get_arg_type(&signal_iter) == DBUS_TYPE_ARRAY) {
            DBusMessageIter param_iter;
            dbus->message_iter_recurse(&signal_iter, &param_iter);

            // The order of parameters in the array is defined: first the target, then the activation ID.
            if (dbus->message_iter_get_arg_type(&param_iter) == DBUS_TYPE_VARIANT) {
                DBusMessageIter target_variant, target_string;

                dbus->message_iter_recurse(&param_iter, &target_variant);
                if (dbus->message_iter_get_arg_type(&target_variant) == DBUS_TYPE_VARIANT) {
                    dbus->message_iter_recurse(&target_variant, &target_string);
                    if (dbus->message_iter_get_arg_type(&target_string) == DBUS_TYPE_STRING) {
                        dbus->message_iter_get_basic(&target_string, &target);
                    }
                }
                dbus->message_iter_next(&param_iter);
            }

            // System properties array.
            if (dbus->message_iter_get_arg_type(&param_iter) == DBUS_TYPE_ARRAY) {
                DBusMessageIter pdata_iter;

                dbus->message_iter_recurse(&param_iter, &pdata_iter);
                while (dbus->message_iter_get_arg_type(&pdata_iter) == DBUS_TYPE_DICT_ENTRY) {
                    DBusMessageIter dict_entry_iter;

                    // Enter the dictionary entry.
                    dbus->message_iter_recurse(&param_iter, &dict_entry_iter);

                    // Get the key
                    const char *key = NULL;
                    dbus->message_iter_get_basic(&dict_entry_iter, &key);

                    // Get the activation token string.
                    if (SDL_strcmp(key, "activation-token") == 0) {
                        DBusMessageIter dict_val_iter;
                        // Enter the value variant.
                        dbus->message_iter_recurse(&dict_entry_iter, &dict_val_iter);

                        if (dbus->message_iter_get_arg_type(&dict_val_iter) == DBUS_TYPE_STRING) {
                            dbus->message_iter_get_basic(&dict_val_iter, &activation_token);
                        }

                        // Found the activation token, nothing else to do.
                        break;
                    }

                    dbus->message_iter_next(&pdata_iter);
                }
            }
        }

        if (target && SDL_strtoull(target, NULL, 10) == session_id) {
            if (activation_token) {
                SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "XDG_ACTIVATION_TOKEN", activation_token, true);
            }
            SDL_SendNotificationAction(id, str);

            return DBUS_HANDLER_RESULT_HANDLED;
        }
    }

not_our_signal:
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static bool SetPortalImage(SDL_DBusContext *dbus, DBusMessageIter *iterInit, SDL_Surface *surface)
{
    DBusMessageIter options_pair, variant_iter, struct_iter;
    const char *key = "icon";
    SDL_IOStream *png = NULL;
    int fd = -1;
    bool ret = false;

#ifdef HAVE_MEMFD_CREATE
    /* Version 2 of the portal interface wants images passed as a sealable file descriptor,
     * which is only possible with memfd_create().
     */
    if (interface_version >= 2) {
        fd = memfd_create("SDL_NotificationImage", MFD_ALLOW_SEALING);
        if (fd >= 0) {
            png = SDL_IOFromFD(fd, false);
            if (!png) {
                close(fd);
                fd = -1;
            }
        }
    }
#endif

    if (!png) {
        png = SDL_IOFromDynamicMem();
    }
    if (!png) {
        return false;
    }
    if (!SDL_SavePNG_IO(surface, png, false)) {
        goto done;
    }
    const Sint64 size = SDL_GetIOSize(png);

    if (!dbus->message_iter_open_container(iterInit, DBUS_TYPE_DICT_ENTRY, NULL, &options_pair)) {
        goto done;
    }
    if (!dbus->message_iter_append_basic(&options_pair, DBUS_TYPE_STRING, &key)) {
        goto done;
    }
    if (fd >= 0) {
        DBusMessageIter fd_variant_iter;
        const char *fd_string = "file-descriptor";

        if (!dbus->message_iter_open_container(&options_pair, DBUS_TYPE_VARIANT, "(sv)", &variant_iter)) {
            goto done;
        }
        if (!dbus->message_iter_open_container(&variant_iter, DBUS_TYPE_STRUCT, NULL, &struct_iter)) {
            goto done;
        }
        if (!dbus->message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &fd_string)) {
            goto done;
        }
        if (!dbus->message_iter_open_container(&struct_iter, DBUS_TYPE_VARIANT, DBUS_TYPE_UNIX_FD_AS_STRING, &fd_variant_iter)) {
            goto done;
        }
        if (!dbus->message_iter_append_basic(&fd_variant_iter, DBUS_TYPE_UNIX_FD, &fd)) {
            goto done;
        }
        if (!dbus->message_iter_close_container(&struct_iter, &fd_variant_iter)) {
            goto done;
        }
        if (!dbus->message_iter_close_container(&variant_iter, &struct_iter)) {
            goto done;
        }
        if (!dbus->message_iter_close_container(&options_pair, &variant_iter)) {
            goto done;
        }
    } else {
        DBusMessageIter array_iter, byte_array_iter;
        const char *bytes_string = "bytes";

        SDL_PropertiesID io_props = SDL_GetIOProperties(png);
        Uint8 *png_ptr = SDL_GetPointerProperty(io_props, SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, NULL);

        if (!dbus->message_iter_open_container(&options_pair, DBUS_TYPE_VARIANT, "(sv)", &variant_iter)) {
            goto done;
        }
        if (!dbus->message_iter_open_container(&variant_iter, DBUS_TYPE_STRUCT, NULL, &struct_iter)) {
            goto done;
        }
        if (!dbus->message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &bytes_string)) {
            goto done;
        }
        if (!dbus->message_iter_open_container(&struct_iter, DBUS_TYPE_VARIANT, "ay", &byte_array_iter)) {
            goto done;
        }
        if (!dbus->message_iter_open_container(&byte_array_iter, DBUS_TYPE_ARRAY, "y", &array_iter)) {
            goto done;
        }

        for (Sint64 i = 0; i < size; ++i) {
            if (!dbus->message_iter_append_basic(&array_iter, DBUS_TYPE_BYTE, &png_ptr[i])) {
                goto done;
            }
        }

        if (!dbus->message_iter_close_container(&byte_array_iter, &array_iter)) {
            goto done;
        }
        if (!dbus->message_iter_close_container(&struct_iter, &byte_array_iter)) {
            goto done;
        }
        if (!dbus->message_iter_close_container(&variant_iter, &struct_iter)) {
            goto done;
        }
        if (!dbus->message_iter_close_container(&options_pair, &variant_iter)) {
            goto done;
        }
    }
    ret = (bool)dbus->message_iter_close_container(iterInit, &options_pair);

done:

    SDL_CloseIO(png);
    return ret;
}

static bool SetPortalSound(SDL_DBusContext *dbus, DBusMessageIter *iterInit, bool silent)
{
    DBusMessageIter options_pair, variant_iter, struct_iter, sound_val_iter;
    const char *key = "sound";
    const char *val = silent ? "silent" : "default";

    if (!dbus->message_iter_open_container(iterInit, DBUS_TYPE_DICT_ENTRY, NULL, &options_pair)) {
        return false;
    }
    if (!dbus->message_iter_append_basic(&options_pair, DBUS_TYPE_STRING, &key)) {
        return false;
    }
    if (!dbus->message_iter_open_container(&options_pair, DBUS_TYPE_VARIANT, "(sv)", &variant_iter)) {
        return false;
    }
    if (!dbus->message_iter_open_container(&variant_iter, DBUS_TYPE_STRUCT, NULL, &struct_iter)) {
        return false;
    }
    if (!dbus->message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &key)) {
        return false;
    }
    if (!dbus->message_iter_open_container(&struct_iter, DBUS_TYPE_VARIANT, "s", &sound_val_iter)) {
        return false;
    }
    if (!dbus->message_iter_append_basic(&sound_val_iter, DBUS_TYPE_STRING, &val)) {
        return false;
    }
    if (!dbus->message_iter_close_container(&struct_iter, &sound_val_iter)) {
        return false;
    }
    if (!dbus->message_iter_close_container(&variant_iter, &struct_iter)) {
        return false;
    }
    if (!dbus->message_iter_close_container(&options_pair, &variant_iter)) {
        return false;
    }
    if (!dbus->message_iter_close_container(iterInit, &options_pair)) {
        return false;
    }

    return true;
}

static bool AddPortalActions(SDL_DBusContext *dbus, DBusMessageIter *iterInit, const SDL_NotificationAction **actions)
{
    DBusMessageIter options_pair, options_value, button_array, properties_array;
    const char *key = "buttons";

    if (!dbus->message_iter_open_container(iterInit, DBUS_TYPE_DICT_ENTRY, NULL, &options_pair)) {
        return false;
    }
    if (!dbus->message_iter_append_basic(&options_pair, DBUS_TYPE_STRING, &key)) {
        return false;
    }
    if (!dbus->message_iter_open_container(&options_pair, DBUS_TYPE_VARIANT, "aa{sv}", &options_value)) {
        return false;
    }
    if (!dbus->message_iter_open_container(&options_value, DBUS_TYPE_ARRAY, "a{sv}", &button_array)) {
        return false;
    }

    for (int i = 0; actions[i]; ++i) {
        if (!dbus->message_iter_open_container(&button_array, DBUS_TYPE_ARRAY, "{sv}", &properties_array)) {
            return false;
        }

        if (!AppendStringOption(dbus, &properties_array, "action", actions[i]->action_id)) {
            return false;
        }
        if (!AppendStringOption(dbus, &properties_array, "label", actions[i]->action_label)) {
            return false;
        }
        if (!AppendTargetString(dbus, &properties_array, "target")) {
            return false;
        }

        if (!dbus->message_iter_close_container(&button_array, &properties_array)) {
            return false;
        }
    }

    if (!dbus->message_iter_close_container(&options_value, &button_array)) {
        return false;
    }
    if (!dbus->message_iter_close_container(&options_pair, &options_value)) {
        return false;
    }
    if (!dbus->message_iter_close_container(iterInit, &options_pair)) {
        return false;
    }

    return true;
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

    if (!SDL_DBus_QueryProperty(NULL,
                                NOTIFICATION_PORTAL_NODE, NOTIFICATION_PORTAL_PATH, NOTIFICATION_PORTAL_INTERFACE,
                                "version", DBUS_TYPE_UINT32, &interface_version)) {
        return false;
    }

    if (!session_id) {
        GetRandom(&session_id, sizeof(session_id));
    }

    DBusError error;
    dbus->error_init(&error);

    dbus->bus_add_match(dbus->session_conn,
                        "type='signal', interface='" NOTIFICATION_PORTAL_INTERFACE "',"
                        "member='" NOTIFICATION_ACTION_SIGNAL_NAME "'",
                        &error);
    if (dbus->error_is_set(&error)) {
        SDL_SetError("Failed to register DBus portal notification filter: %s", error.message);
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

static SDL_NotificationID ShowPortalNotification(SDL_DBusContext *dbus, SDL_PropertiesID props)
{
    DBusConnection *conn = dbus->session_conn;
    DBusMessage *msg = NULL;
    DBusMessageIter iter, array;
    bool error_set = false;

    if (!InitPortalSignalListener(dbus)) {
        return 0;
    }

    const SDL_PropertiesID replaces = SDL_GetNumberProperty(props, SDL_PROP_NOTIFICATION_REPLACES_NUMBER, 0);
    const char *title = SDL_GetStringProperty(props, SDL_PROP_NOTIFICATION_TITLE_STRING, NULL);
    const char *message = SDL_GetStringProperty(props, SDL_PROP_NOTIFICATION_MESSAGE_STRING, NULL);
    const SDL_NotificationPriority priority = SDL_GetNumberProperty(props, SDL_PROP_NOTIFICATION_PRIORITY_NUMBER, SDL_NOTIFICATION_PRIORITY_NORMAL);
    SDL_Surface *image = SDL_GetPointerProperty(props, SDL_PROP_NOTIFICATION_IMAGE_POINTER, NULL);
    const SDL_NotificationAction **actions = SDL_GetPointerProperty(props, SDL_PROP_NOTIFICATION_ACTIONS_POINTER, NULL);
    const bool silent = SDL_GetBooleanProperty(props, SDL_PROP_NOTIFICATION_SILENT_BOOLEAN, false);

    // Call Notification.AddNotification()
    msg = dbus->message_new_method_call(NOTIFICATION_PORTAL_NODE, NOTIFICATION_PORTAL_PATH, NOTIFICATION_PORTAL_INTERFACE, "AddNotification");
    if (msg == NULL) {
        goto failure;
    }

    dbus->message_iter_init_append(msg, &iter);

    // Notification ID
    Uint32 new_id = 0;
    if (!replaces) {
        GetRandom(&new_id, sizeof(new_id));
    } else {
        new_id = replaces;
    }

    {
        char id_str[128];
        SDL_snprintf(id_str, SDL_arraysize(id_str), SDL_NOTIFICATION_PREAMBLE "%" SDL_PRIu32, new_id);
        const char *id = id_str;
        dbus->message_iter_append_basic(&iter, DBUS_TYPE_STRING, &id);
    }

    // Parameters
    dbus->message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &array);
    AppendStringOption(dbus, &array, "title", title);
    AppendStringOption(dbus, &array, "body", message);
    AppendStringOption(dbus, &array, "default-action", "default");
    AppendTargetString(dbus, &array, "default-action-target");

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
    case SDL_NOTIFICATION_PRIORITY_CRITICAL:
        priority_str = "urgent";
        break;
    }

    AppendStringOption(dbus, &array, "priority", priority_str);
    SetPortalDisplayHints(dbus, &array, props);

    if (image) {
        SDL_Surface *image_surface = image;
        if (image_surface->format != SDL_PIXELFORMAT_ABGR8888) {
            image_surface = SDL_ConvertSurface(image_surface, SDL_PIXELFORMAT_ABGR8888);
        }

        SetPortalImage(dbus, &array, image_surface);

        if (image_surface != image) {
            SDL_DestroySurface(image_surface);
        }
    }

    if (actions) {
        AddPortalActions(dbus, &array, actions);
    }

    if (!dbus->message_iter_close_container(&iter, &array)) {
        goto failure;
    }

    DBusError err;
    dbus->error_init(&err);
    DBusMessage *reply = dbus->connection_send_with_reply_and_block(conn, msg, -1, &err);
    dbus->message_unref(msg);

    if (!reply) {
        if (err.message) {
            SDL_SetError("Notification failed: %s", err.message);
            error_set = true;
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
    if (!error_set) {
        SDL_SetError("Failed to dispatch org.freedesktop.portal.Notification request");
    }
    return 0;
}

static bool RemovePortalNotification(SDL_DBusContext *dbus, SDL_NotificationID id)
{
    if (!id) {
        return SDL_InvalidParamError("id");
    }

    char id_str[128];
    DBusMessageIter iter;
    bool ret = false;

    // Call org.freedesktop.Notifications.CloseNotification()
    DBusMessage *msg = dbus->message_new_method_call(NOTIFICATION_PORTAL_NODE, NOTIFICATION_PORTAL_PATH, NOTIFICATION_PORTAL_INTERFACE, "RemoveNotification");
    if (msg == NULL) {
        return SDL_OutOfMemory();
    }

    dbus->message_iter_init_append(msg, &iter);

    SDL_snprintf(id_str, SDL_arraysize(id_str), SDL_NOTIFICATION_PREAMBLE "%" SDL_PRIu32, id);
    const char *cstr = id_str;
    if (dbus->message_iter_append_basic(&iter, DBUS_TYPE_STRING, &cstr)) {
        ret = (bool)dbus->connection_send(dbus->session_conn, msg, NULL);
        if (!ret) {
            SDL_SetError("Failed to send notification removal request");
        }
    } else {
        ret = SDL_OutOfMemory();
    }

    dbus->message_unref(msg);
    return ret;
}

SDL_NotificationID SDL_SYS_ShowNotification(SDL_PropertiesID props)
{
    SDL_DBusContext *dbus = SDL_DBus_GetContext();

    if (!dbus || !dbus->session_conn) {
        SDL_SetError("D-Bus not available");
        return 0;
    }

    // The portal is only used if inside a container, or the app association can be wrong.
    if (IsInContainer()) {
        return ShowPortalNotification(dbus, props);
    } else {
        return ShowCoreNotification(dbus, props);
    }
}

bool SDL_RemoveNotification(SDL_NotificationID notification)
{
    SDL_DBusContext *dbus = SDL_DBus_GetContext();

    if (!dbus || !dbus->session_conn) {
        return SDL_SetError("D-Bus not available");
    }

    // The portal is only used if inside a container, or the app association can be wrong.
    if (IsInContainer()) {
        return RemovePortalNotification(dbus, notification);
    } else {
        return RemoveCoreNotification(dbus, notification);
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
                                   "member='" NOTIFICATION_ACTION_SIGNAL_NAME "'",
                                   NULL);
        }
        if (core_listener_registered) {
            dbus->connection_remove_filter(conn, &CoreNotificationFilter, NULL);
            dbus->bus_remove_match(dbus->session_conn,
                                   "type='signal', interface='" NOTIFICATION_CORE_INTERFACE "',"
                                   "member='" NOTIFICATION_ACTION_SIGNAL_NAME "'",
                                   NULL);
            dbus->bus_remove_match(dbus->session_conn,
                                   "type='signal', interface='" NOTIFICATION_CORE_INTERFACE "',"
                                   "member='" NOTIFICATION_CLOSED_SIGNAL_NAME "'",
                                   NULL);
            dbus->bus_remove_match(dbus->session_conn,
                                   "type='signal', interface='" NOTIFICATION_CORE_INTERFACE "',"
                                   "member='" NOTIFICATION_ACTIVATION_TOKEN_SIGNAL_NAME "'",
                                   NULL);
        }
        dbus->connection_flush(conn);

        portal_listener_registered = false;
        core_listener_registered = false;
    }

    // Delete the temporary icon.
    if (icon_uri) {
        // Trim the "file://" prefix.
        unlink(icon_uri + 7);
        SDL_free(icon_uri);
        icon_uri = NULL;
    }
}

bool SDL_RequestNotificationPermission()
{
    SDL_DBusContext *dbus = SDL_DBus_GetContext();

    // No interface (yet) to ask permission; just make sure that notifications are available.
    if (!dbus || !dbus->session_conn) {
        return SDL_SetError("D-Bus not available");
    }

    if (IsInContainer()) {
        return InitPortalSignalListener(dbus);
    } else {
        return InitCoreSignalListener(dbus);
    }
}
