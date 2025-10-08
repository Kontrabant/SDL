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
#include <SDL3/SDL_surface.h>

#include <SDL3/SDL_begin_code.h>
/* Set up for C function definitions, even when using C++ */
#ifdef __cplusplus
extern "C" {
#endif

typedef Uint32 SDL_NotificationID;

/**
 * \brief SDL_Notification flags.
 *
 * Note that some system implementations may not support all flags.
 */
typedef Uint64 SDL_NotificationFlags;

#define SDL_NOTIFICATION_PRIORITY_LOW    SDL_UINT64_C(0x0000000000000001) /**< Lowest priority. */
#define SDL_NOTIFICATION_PRIORITY_NORMAL SDL_UINT64_C(0x0000000000000002) /**< Normal/medium priority. */
#define SDL_NOTIFICATION_PRIORITY_HIGH   SDL_UINT64_C(0x0000000000000004) /**< High/important priority. */
#define SDL_NOTIFICATION_PRIORITY_URGENT SDL_UINT64_C(0x0000000000000008) /**< Highest/critical priority. Note that this may override any "Do Not Disturb" settings. */
#define SDL_NOTIFICATION_TRANSIENT       SDL_UINT64_C(0x0000000000000010) /**< Request that the notification not persist in any notification logs. */

/**
 * Notification structure containing button IDs and labels
 */
typedef struct SDL_NotificationAction
{
    const char *button_id; /**< The identifier string for the button. */
    const char *button_label; /**< The localized label string for the button. */
} SDL_NotificationAction;

/**
 * Notification structure containing title, message, icon, etc.
 */
typedef struct SDL_NotificationData
{
    SDL_NotificationFlags flags;     /**< ::SDL_NotificationFlags */
    const char *title;               /**< UTF-8 title */
    const char *message;             /**< UTF-8 message text */
    SDL_Surface *icon;               /**< Icon data */
    SDL_NotificationAction *actions; /**< Array of notification actions */
    int num_actions;                 /**< Length of the actions array */
} SDL_NotificationData;

/**
 *  \brief Create a system notification.
 *
 *  \param notification_data the SDL_NotificationData structure with title, text and other options
 *  \returns A non-zero SDL_NotificationID on success or 0 on failure; call
 *           SDL_GetError() for more information.
 *
 *  \since This function is available since SDL 3.6.0
 *
 *  \sa SDL_ShowSimpleNotification
 *  \sa SDL_NotificationData
 */
extern SDL_DECLSPEC SDL_NotificationID SDLCALL SDL_ShowNotification(const SDL_NotificationData *notification_data);

/**
 *  \brief Create a simple system notification.
 *
 *  \param title    UTF-8 title text
 *  \param message  UTF-8 message text
 *  \returns A non-zero SDL_NotificationID on success or 0 on failure; call
 *           SDL_GetError() for more information.
 *
 *  \since This function is available since SDL 3.6.0
 *
 *  \sa SDL_ShowNotification
 *  \sa SDL_NotificationData
 */
extern SDL_DECLSPEC SDL_NotificationID SDLCALL SDL_ShowSimpleNotification(const char *title, const char *message);

/* Ends C function definitions when using C++ */
#ifdef __cplusplus
}
#endif
#include <SDL3/SDL_close_code.h>

#endif /* SDL_notification_h_ */

