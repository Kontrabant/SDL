/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2023 Sam Lantinga <slouken@libsdl.org>

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

/**
 * # CategoryNotifications
 *
 * Notifications are temporary popup dialogs that passively present
 * information to the user, or prompt user action. They are managed
 * and presented by the system, and can present simple options for
 * user feedback, usually in the form of buttons.
 *
 * The capabilities of notifications, and how they are displayed,
 * vary between systems, but they generally allow for a title,
 * body text, an associated image, and buttons to allow the user
 * to provide feedback.
 *
 * How notifications are presented and handled are subject to system
 * policy, and it should not be assumed that showing a notification
 * means that the user will see it immediately, if at all. The
 * user may disable notifications for certain applications, and most
 * systems provide a "do not disturb" mode that universally silences
 * notifications when activated.
 *
 * There is both a customizable function (SDL_ShowNotificationWithProperties())
 * that offers many options for what is displayed, and also a much-simplified
 * version (SDL_ShowSimpleNotification()), that simply takes a header (required),
 * body (optional), and image (optional).
 */

#ifndef SDL_notification_h_
#define SDL_notification_h_

#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>

#include <SDL3/SDL_begin_code.h>
/* Set up for C function definitions, even when using C++ */
#ifdef __cplusplus
extern "C" {
#endif

/**
 * The pointer to a global SDL_Surface object used as the header icon on some
 * platforms for system notifications.
 *
 * Can be set before calling SDL_ShowNotification() or SDL_ShowSimpleNotification()
 * for the first time. After showing the first notification, the surface can be
 * destroyed.
 *
 * \since This macro is available since SDL 3.6.0.
 */
#define SDL_PROP_GLOBAL_NOTIFICATION_HEADER_ICON_POINTER "SDL.notification.header_icon"

typedef Uint32 SDL_NotificationID; /**< The identifier for a system notification. */

typedef enum SDL_NotificationPriority
{
    SDL_NOTIFICATION_PRIORITY_LOW = -1,    /**< Lowest priority. */
    SDL_NOTIFICATION_PRIORITY_NORMAL = 0,  /**< Normal/medium priority. */
    SDL_NOTIFICATION_PRIORITY_HIGH = 1,    /**< High/important priority. */
    SDL_NOTIFICATION_PRIORITY_CRITICAL = 2 /**< Highest/critical priority. Note that this may override any "Do Not Disturb" settings. */
} SDL_NotificationPriority;

/**
 * Notification structure containing action IDs and labels
 *
 * These typically take the form of buttons or menu items
 * placed within the notification dialog.
 */
typedef struct SDL_NotificationAction
{
    const char *action_id;    /**< The identifier string for the button. 'default' is a reserved identifier. */
    const char *action_label; /**< The localized label string for the widget associated with the action. */
} SDL_NotificationAction;

#define SDL_PROP_NOTIFICATION_TITLE_STRING      "SDL.notification.title"
#define SDL_PROP_NOTIFICATION_MESSAGE_STRING    "SDL.notification.message"
#define SDL_PROP_NOTIFICATION_IMAGE_POINTER     "SDL.notification.icon"
#define SDL_PROP_NOTIFICATION_ACTIONS_POINTER   "SDL.notification.actions"
#define SDL_PROP_NOTIFICATION_PRIORITY_NUMBER   "SDL.notification.priority"
#define SDL_PROP_NOTIFICATION_TRANSIENT_BOOLEAN "SDL.notification.transient"
#define SDL_PROP_NOTIFICATION_SILENT_BOOLEAN    "SDL.notification.silent"
#define SDL_PROP_NOTIFICATION_REPLACES_NUMBER   "SDL.notification.replaces"

/**
 *  Requests permission to display notifications.
 *
 *  \returns True on success or false on failure; call
 *           SDL_GetError() for more information.
 *
 *  \since This function is available since SDL 3.6.0
 *
 *  \sa SDL_ShowSimpleNotification
 *  \sa SDL_NotificationData
 */
extern SDL_DECLSPEC bool SDLCALL SDL_RequestNotificationPermission();

/**
 *  Show a system notification.
 *
 *  System notifications are small, asynchronous popup windows that notify the user
 *  of some information. How they are displayed is system dependent.
 *
 *  These are the supported properties:
 *
 * - `SDL_PROP_NOTIFICATION_TITLE_STRING`: the title of the notification, in
 *   UTF-8 encoding (required)
 * - `SDL_PROP_NOTIFICATION_MESSAGE_STRING`: the message body of the notification,
 *   in UTF-8 encoding
 * - `SDL_PROP_NOTIFICATION_IMAGE_POINTER`: a pointer to an `SDL_Surface` containing
 *   an image that will be attached to the notification
 * - `SDL_PROP_NOTIFICATION_ACTIONS_POINTER`: An array of pointers to `SDL_NotificationAction`
 *   structs that will add buttons to the notification. The last element in the array must be
 *   a null pointer. Note that systems may have a limit on the maximum number of buttons a
 *   notification can have
 * - `SDL_PROP_NOTIFICATION_PRIORITY_NUMBER`: an `SDL_NotificationPriority` value representing
 *   the notification priority
 * - `SDL_PROP_NOTIFICATION_TRANSIENT_BOOLEAN`: true if the notification should not persist
 *   in the system notification center
 * - `SDL_PROP_NOTIFICATION_SILENT_BOOLEAN`: true if the system should not play any sound
 *   when displaying the notification
 * - `SDL_PROP_NOTIFICATION_REPLACES_NUMBER`: the `SDL_NotificationID` of a previously
 *   shown notification that this notification should replace
 *
 * Not all properties are supported by all platforms.
 *
 * Notifications are available on:
 *  - Windows 10 or higher
 *  - macOS 10.14 or higher
 *  - iOS 11 or higher
 *  - *nix platforms that support the org.freedesktop.Notifications, or
 *    org.freedesktop.portal.Notification interfaces
 *
 *  \param props the properties to be used when creating this notification
 *  \returns A non-zero SDL_NotificationID on success or 0 on failure; call
 *           SDL_GetError() for more information.
 *
 *  \since This function is available since SDL 3.6.0
 *
 *  \sa SDL_ShowSimpleNotification
 *  \sa SDL_NotificationAction
 *  \sa SDL_NotificationPriority
 */
extern SDL_DECLSPEC SDL_NotificationID SDLCALL SDL_ShowNotificationWithProperties(SDL_PropertiesID props);

/**
 *  Show a simple system notification.
 *
 *  \param title    UTF-8 title text (required)
 *  \param message  UTF-8 message text (optional)
 *  \param image The image associated with is notification (optional)
 *  \returns A non-zero SDL_NotificationID on success or 0 on failure; call
 *           SDL_GetError() for more information.
 *
 *  \since This function is available since SDL 3.6.0
 *
 *  \sa SDL_ShowNotification
 *  \sa SDL_NotificationData
 */
extern SDL_DECLSPEC SDL_NotificationID SDLCALL SDL_ShowSimpleNotification(const char *title, const char *message, SDL_Surface *image);

/**
 *  \brief Remove a notification
 *
 *  \param notification the ID of the notification to remove
 *  \returns True on success or false on failure; call
 *           SDL_GetError() for more information.
 *
 *  \since This function is available since SDL 3.6.0
 *
 *  \sa SDL_ShowNotification
 */
extern SDL_DECLSPEC bool SDLCALL SDL_RemoveNotification(SDL_NotificationID notification);

// Ends C function definitions when using C++
#ifdef __cplusplus
}
#endif
#include <SDL3/SDL_close_code.h>

#endif // SDL_notification_h_
