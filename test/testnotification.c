/*
  Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely.
*/

/* Simple test of the SDL Notification API */
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_test.h>

/* This enables themed Windows dialogs when building with Visual Studio */
#if defined(SDL_PLATFORM_WINDOWS) && defined(_MSC_VER)
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0'  processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

static SDLTest_CommonState *state;
static SDL_Surface *icon;
static SDL_NotificationID last_id;
static SDL_PropertiesID props;
static SDL_NotificationAction actions[] = {
    { "action_1", "OK" },
    { "action_2", "Cancel" },
    {"action_url", "SDL Website" }
};
static SDL_NotificationAction *action_array[SDL_arraysize(actions) + 1];

static bool transient;
static bool silent;

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
    SDL_SetAppMetadata("SDL Notification Test", "0.0.1", "org.libsdl.testnotification");

    /* Initialize test framework */
    state = SDLTest_CommonCreateState(argv, 0);
    if (!state) {
        return SDL_APP_FAILURE;
    }

    /* Parse commandline */
    if (!SDLTest_CommonDefaultArgs(state, argc, argv)) {
        return SDL_APP_FAILURE;
    }

    state->flags |= SDL_INIT_VIDEO;
    if (!SDLTest_CommonInit(state)) {
        return SDL_APP_FAILURE;
    }

    icon = SDL_LoadPNG("sdl-test_round.png");

    props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_NOTIFICATION_TITLE_STRING, "Test Notification");
    SDL_SetStringProperty(props, SDL_PROP_NOTIFICATION_MESSAGE_STRING, "Hey, pay attention to me!");
    SDL_SetPointerProperty(props, SDL_PROP_NOTIFICATION_IMAGE_POINTER, icon);

    for (int i = 0; i < SDL_arraysize(actions); ++i) {
        action_array[i] = &actions[i];
    }
    action_array[SDL_arraysize(actions)] = NULL;
    SDL_SetPointerProperty(props, SDL_PROP_NOTIFICATION_ACTIONS_POINTER, action_array);

    SDL_RequestNotificationPermission();

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    if (event->type == SDL_EVENT_KEY_DOWN) {
        if (event->key.key == SDLK_SPACE) {
            if (event->key.mod & SDL_KMOD_CTRL) {
                SDL_SetNumberProperty(props, SDL_PROP_NOTIFICATION_REPLACES_NUMBER, last_id);
            } else {
                SDL_SetNumberProperty(props, SDL_PROP_NOTIFICATION_REPLACES_NUMBER, 0);
            }
            if (transient) {
                SDL_SetNumberProperty(props, SDL_PROP_NOTIFICATION_TRANSIENT_BOOLEAN, true);
            } else {
                SDL_SetNumberProperty(props, SDL_PROP_NOTIFICATION_TRANSIENT_BOOLEAN, false);
            }
            if (silent) {
                SDL_SetNumberProperty(props, SDL_PROP_NOTIFICATION_SILENT_BOOLEAN, true);
            } else {
                SDL_SetNumberProperty(props, SDL_PROP_NOTIFICATION_SILENT_BOOLEAN, false);
            }
            // Test showing a system notification message.
            const SDL_NotificationID new_id = SDL_ShowNotificationWithProperties(props);
            if (new_id) {
                SDL_Log("Notification successfully dispatched. ID: %" SDL_PRIu32, new_id);
                last_id = new_id;
            } else {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", SDL_GetError());
            }
        } else if (event->key.key == SDLK_H) {
            if (last_id) {
                SDL_RemoveNotification(last_id);
            }
        } else if (event->key.key == SDLK_T) {
            transient ^= true;
        } else if (event->key.key == SDLK_S) {
            silent ^= true;
        }
    } else if (event->type == SDL_EVENT_NOTIFICATION_ACTION_INVOKED) {
        SDL_Log("User responded to notification %" SDL_PRIu32 " with action \"%s\"", event->notification.which, event->notification.action_id);

        // Raise the window of the user clicked "OK".
        if (SDL_strcmp(event->notification.action_id, "action_1") == 0) {
            SDL_RaiseWindow(state->windows[0]);
        } else if (SDL_strcmp(event->notification.action_id, "action_url") == 0) {
            SDL_OpenURL("https://www.libsdl.org");
        }
    }

    return SDLTest_CommonEventMainCallbacks(state, event);
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    for (int i = 0; i < state->num_windows; ++i) {
        SDL_Renderer *renderer = state->renderers[i];
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

        float y = 16.0f;
        SDL_RenderDebugText(renderer, 8.f, y, "Press space to show a notification");
        y += SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE + 2;
        SDL_RenderDebugText(renderer, 8.f, y, "Press 'H' to hide the last notification");
        y += SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE + 2;
        SDL_RenderDebugTextFormat(renderer, 8.f, y, "Press 'T' to toggle the transient property (%s)", transient ? "ON" : "OFF");
        y += SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE + 2;
        SDL_RenderDebugTextFormat(renderer, 8.f, y, "Press 'S' to toggle the silent property (%s)", silent ? "ON" : "OFF");
        SDL_RenderPresent(renderer);
    }

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    SDL_DestroySurface(icon);
    SDLTest_CommonQuit(state);
}
