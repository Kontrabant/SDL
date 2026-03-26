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
 *  \file SDL_notification.h
 *
 *  \brief Header file for notification API
 */

#ifndef SDL_notification_h_
#define SDL_notification_h_

#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_properties.h>
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

typedef Uint32 SDL_NotificationID;

#define SDL_NOTIFICATION_TRANSIENT SDL_UINT64_C(0x0000000000000010) /**< Request that the notification not persist in any notification logs. */
#define SDL_NOTIFICATION_SILENT    SDL_UINT64_C(0x0000000000000020) /**< Request that the notification not play a sound when shown. */

typedef enum SDL_NotificationPriority
{
    SDL_NOTIFICATION_PRIORITY_LOW = -1,   /**< Lowest priority. */
    SDL_NOTIFICATION_PRIORITY_NORMAL = 0, /**< Normal/medium priority. */
    SDL_NOTIFICATION_PRIORITY_HIGH = 1,   /**< High/important priority. */
    SDL_NOTIFICATION_PRIORITY_URGENT = 2  /**< Highest/critical priority. Note that this may override any "Do Not Disturb" settings. */
} SDL_NotificationPriority;
/**
 * Notification structure containing button IDs and labels
 */
typedef struct SDL_NotificationAction
{
    const char *button_id;    /**< The identifier string for the button. */
    const char *button_label; /**< The localized label string for the button. */
} SDL_NotificationAction;

#if 0
/**
 * Notification structure containing title, message, icon, etc.
 */
typedef struct SDL_NotificationData
{
    SDL_NotificationFlags flags;     /**< ::SDL_NotificationFlags */
    const char *title;               /**< UTF-8 title text */
    const char *message;             /**< UTF-8 message text */
    SDL_Surface *icon;               /**< Icon data */
    SDL_NotificationAction *actions; /**< Array of notification actions */
    int num_actions;                 /**< Length of the actions array */
    SDL_NotificationID replaces;     /**< ID of an existing notification to replace */
} SDL_NotificationData;
#endif

#define SDL_PROP_NOTIFICATION_TITLE_STRING      "SDL.notification.title"
#define SDL_PROP_NOTIFICATION_MESSAGE_STRING    "SDL.notification.message"
#define SDL_PROP_NOTIFICATION_ICON_POINTER      "SDL.notification.icon"
#define SDL_PROP_NOTIFICATION_ACTIONS_POINTER   "SDL.notification.actions"
#define SDL_PROP_NOTIFICATION_PRIORITY_NUMBER   "SDL.notification.priority"
#define SDL_PROP_NOTIFICATION_TRANSIENT_BOOLEAN "SDL.notification.transient"
#define SDL_PROP_NOTIFICATION_SILENT_BOOLEAN    "SDL.notification.silent"
#define SDL_PROP_NOTIFICATION_REPLACES_NUMBER   "SDL.notification.replaces"

/**
 *  \brief Show a system notification.
 *
 *  \param props the properties to be used when creating this notification
 *  \returns A non-zero SDL_NotificationID on success or 0 on failure; call
 *           SDL_GetError() for more information.
 *
 *  \since This function is available since SDL 3.6.0
 *
 *  \sa SDL_ShowSimpleNotification
 *  \sa SDL_NotificationData
 */
extern SDL_DECLSPEC SDL_NotificationID SDLCALL SDL_ShowNotificationWithProperties(SDL_PropertiesID props);

/**
 *  \brief Show a simple system notification.
 *
 *  \param title    UTF-8 title text
 *  \param message  UTF-8 message text
 *  \param icon The image associated with is notification
 *  \returns A non-zero SDL_NotificationID on success or 0 on failure; call
 *           SDL_GetError() for more information.
 *
 *  \since This function is available since SDL 3.6.0
 *
 *  \sa SDL_ShowNotification
 *  \sa SDL_NotificationData
 */
extern SDL_DECLSPEC SDL_NotificationID SDLCALL SDL_ShowSimpleNotification(const char *title, const char *message, SDL_Surface *icon);

// Ends C function definitions when using C++
#ifdef __cplusplus
}
#endif
#include <SDL3/SDL_close_code.h>

#endif // SDL_notification_h_
