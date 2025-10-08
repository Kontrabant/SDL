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
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_test.h>

/* This enables themed Windows dialogs when building with Visual Studio */
#if defined(SDL_PLATFORM_WINDOWS) && defined(_MSC_VER)
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0'  processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

int main(int argc, char *argv[])
{
    SDLTest_CommonState *state;

    /* Initialize test framework */
    state = SDLTest_CommonCreateState(argv, 0);
    if (!state) {
        return 1;
    }

    /* Parse commandline */
    if (!SDLTest_CommonDefaultArgs(state, argc, argv)) {
        return 1;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't initialize SDL video subsystem: %s", SDL_GetError());
        return 1;
    }

    /* Test showing a message box with a parent window */
    {
        SDL_Event event;
        SDL_Window *window = SDL_CreateWindow("Test", 640, 480, SDL_WINDOW_RESIZABLE);
        SDL_Renderer *renderer = SDL_CreateRenderer(window, NULL);

        SDL_NotificationAction actions[2] = {
            { "button_action_1", "Button 1" },
            { "button_action_2", "Button 2" }
        };

        SDL_NotificationData notification_data;
        SDL_zero(notification_data);

        notification_data.title = "Test Notification";
        notification_data.message = "Hey, pay attention to me!";
        notification_data.icon = SDL_LoadPNG("sdl-test_round.png");
        notification_data.actions = actions;
        notification_data.num_actions = SDL_arraysize(actions);
        notification_data.flags = SDL_NOTIFICATION_PRIORITY_NORMAL;

        while (1) {
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_SPACE) {
                    /* Test showing a system notification message with a parent window */
                    if (!SDL_ShowNotification(&notification_data)) {
                        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error Presenting Notification: %s\n", SDL_GetError());
                    }
                } else if (event.type == SDL_EVENT_NOTIFICATION_ACTION) {
                    SDL_Log("User responded to notification %u with action %s", event.notification.which, event.notification.button_id);
                } else if (event.type == SDL_EVENT_QUIT) {
                    goto breakout;
                }
            }

            /* On wayland, no window will actually show until something has
             * actually been displayed.
             */
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_RenderDebugText(renderer, 0, 16, "Press space to show a notification");
            SDL_RenderPresent(renderer);
        }

    breakout:
        SDL_DestroyWindow(window);
    }

    SDL_Quit();
    SDLTest_CommonDestroyState(state);
    return 0;
}
