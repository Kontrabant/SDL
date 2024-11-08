/*
  Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely.
*/
/* Sample program:  Create and test dockable windows */

#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_test.h>
#include <stdlib.h>

static void DrawDockable(SDL_Renderer *renderer, float x, float y)
{
    SDL_SetRenderDrawColor(renderer, 128, 200, 128, SDL_ALPHA_OPAQUE);
    const SDL_FRect rect = {x, y, 320.f, 240.f};
    SDL_RenderFillRect(renderer, &rect);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDLTest_DrawString(renderer, rect.x + rect.w / 2, rect.y + rect.h / 2, "Drag Me");
}

static SDL_HitTestResult DragHitCallback(SDL_Window *win, const SDL_Point *area, void *data)
{
    return SDL_HITTEST_DRAGGABLE;
}

static SDL_Window *mainWindow = NULL, *dragWindow = NULL;
static SDL_Renderer *mainRenderer = NULL, *dragRenderer = NULL;
static SDLTest_CommonState *state = NULL;
static SDL_FRect highlight;
static int i;
static int exit_code = 0;
static bool render_highlight = false;

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv)
{
    /* Initialize test framework */
    state = SDLTest_CommonCreateState(argv, 0);
    if (state == NULL) {
        return 1;
    }

    /* Parse commandline */
    for (i = 1; i < argc;) {
        int consumed;

        consumed = SDLTest_CommonArg(state, i);

        if (consumed <= 0) {
            static const char *options[] = { NULL };
            SDLTest_CommonLogUsage(state, argv[0], options);
            return 1;
        }

        i += consumed;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed (%s)", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!SDL_CreateWindowAndRenderer("Main Window", 640, 480, 0, &mainWindow, &mainRenderer)) {
        SDL_Log("Failed to create main window and/or renderer: %s\n", SDL_GetError());
        exit_code = 1;
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    /* Main window is gray */
    if (mainRenderer) {
        SDL_SetRenderDrawColor(mainRenderer, 128, 128, 128, SDL_ALPHA_OPAQUE);
        SDL_RenderClear(mainRenderer);
        if (render_highlight) {
            SDL_SetRenderDrawColor(mainRenderer, 255, 255, 0, 128);
            SDL_RenderFillRect(mainRenderer, &highlight);
        }
        if (!dragRenderer) {
            DrawDockable(mainRenderer, highlight.x, highlight.y);
        }
        SDL_RenderPresent(mainRenderer);
    }
    if (dragRenderer) {
        DrawDockable(dragRenderer, 0.f, 0.f);
        SDL_RenderPresent(dragRenderer);
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    switch (event->type) {
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        if (event->window.windowID == SDL_GetWindowID(mainWindow)) {
            return SDL_APP_SUCCESS;
        }
        break;

    case SDL_EVENT_DROP_BEGIN:
        break;

    case SDL_EVENT_DROP_POSITION:
        if (event->drop.dropWindowID) {
            highlight.x = event->drop.x < 320.f ? 0.f : 320.f;
            highlight.y = event->drop.y < 240.f ? 0.f : 240.f;
            highlight.w = 320.f;
            highlight.h = 240.f;
            render_highlight = true;
        }
        break;

    case SDL_EVENT_DROP_WINDOW:
        SDL_DestroyWindow(dragWindow);
        dragWindow = NULL;
        dragRenderer = NULL;
        break;

    case SDL_EVENT_DROP_COMPLETE:
        render_highlight = false;
        break;

    case SDL_EVENT_MOUSE_MOTION:
    {
        if (!dragWindow && SDL_GetMouseState(NULL, NULL) & SDL_BUTTON_LEFT) {
            int wx, wy;
            SDL_GetWindowPosition(mainWindow, &wx, &wy);
            SDL_PropertiesID props = SDL_CreateProperties();
            SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_BORDERLESS_BOOLEAN, true);
            SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_DOCKABLE_BOOLEAN, true);
            SDL_SetPointerProperty(props, SDL_PROP_WINDOW_CREATE_PARENT_POINTER, mainWindow);
            SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, 320);
            SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, 240);
            SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, wx + (int)highlight.x);
            SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, wy + (int)highlight.y);
            dragWindow = SDL_CreateWindowWithProperties(props);
            SDL_DestroyProperties(props);
            dragRenderer = SDL_CreateRenderer(dragWindow, NULL);
            SDL_SetWindowHitTest(dragWindow, DragHitCallback, NULL);
        }
    }
        break;
    default:
        break;
    }

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    if (mainWindow) {
        /* The child window and renderer will be cleaned up automatically. */
        SDL_DestroyWindow(mainWindow);
    }

    SDL_Quit();
    SDLTest_CommonDestroyState(state);
}
