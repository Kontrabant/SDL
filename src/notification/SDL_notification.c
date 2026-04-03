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
#include "SDL_internal.h"

#include "SDL_notification_c.h"
#include <SDL3/SDL_properties.h>

SDL_NotificationID SDL_ShowNotificationWithProperties(SDL_PropertiesID props)
{
    if (!props) {
        SDL_InvalidParamError("props");
        return 0;
    }

    CHECK_PARAM (true) {
        const char *title = SDL_GetStringProperty(props, SDL_PROP_NOTIFICATION_TITLE_STRING, NULL);
        if (!title) {
            SDL_SetError("Notifications must have a title");
            return 0;
        }
    }

    return SDL_SYS_ShowNotification(props);
}

SDL_NotificationID SDL_ShowSimpleNotification(const char *title, const char *message, SDL_Surface *image)
{
    SDL_PropertiesID props = SDL_CreateProperties();
    if (!props) {
        return 0;
    }

    if (title) {
        SDL_SetStringProperty(props, SDL_PROP_NOTIFICATION_TITLE_STRING, title);
    } else {
        SDL_SetError("Notifications must have a title");
        return 0;
    }
    if (message) {
        SDL_SetStringProperty(props, SDL_PROP_NOTIFICATION_MESSAGE_STRING, message);
    }
    if (image) {
        SDL_SetPointerProperty(props, SDL_PROP_NOTIFICATION_IMAGE_POINTER, image);
    }

    SDL_NotificationID id = SDL_ShowNotificationWithProperties(props);
    SDL_DestroyProperties(props);

    return id;
}
