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

int main(int argc, char *argv[])
{
    SDL_Window *mainWindow = NULL, *dragWindow = NULL;
    SDL_Renderer *mainRenderer = NULL, *dragRenderer = NULL;
    SDLTest_CommonState *state = NULL;
    SDL_FRect highlight;
    int i;
    int exit_code = 0;
    bool render_highlight = false;

    SDL_zero(highlight);

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
        return 1;
    }

    if (!SDL_CreateWindowAndRenderer("Main Window", 640, 480, 0, &mainWindow, &mainRenderer)) {
        SDL_Log("Failed to create main window and/or renderer: %s\n", SDL_GetError());
        exit_code = 1;
        goto sdl_quit;
    }

    while (1) {
        int quit = 0;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                if (e.window.windowID == SDL_GetWindowID(mainWindow)) {
                    quit = 1;
                }
                break;

            case SDL_EVENT_DROP_BEGIN:
                break;

            case SDL_EVENT_DROP_POSITION:
                if (e.drop.dropWindowID) {
                    highlight.x = e.drop.x < 320.f ? 0.f : 320.f;
                    highlight.y = e.drop.y < 240.f ? 0.f : 240.f;
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
                if (!dragWindow && SDL_GetMouseState(NULL, NULL) & SDL_BUTTON_LEFT) {
                    SDL_PropertiesID props = SDL_CreateProperties();
                    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_BORDERLESS_BOOLEAN, true);
                    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_DOCKABLE_BOOLEAN, true);
                    SDL_SetPointerProperty(props, SDL_PROP_WINDOW_CREATE_PARENT_POINTER, mainWindow);
                    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, 320);
                    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, 240);
                    dragWindow = SDL_CreateWindowWithProperties(props);
                    SDL_DestroyProperties(props);
                    dragRenderer = SDL_CreateRenderer(dragWindow, NULL);
                    SDL_SetWindowHitTest(dragWindow, DragHitCallback, NULL);
                }
                break;
            default:
                break;
            }
        }

        if (quit) {
            break;
        }

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
    }

sdl_quit:
    if (mainWindow) {
        /* The child window and renderer will be cleaned up automatically. */
        SDL_DestroyWindow(mainWindow);
    }

    SDL_Quit();
    SDLTest_CommonDestroyState(state);
    return exit_code;
}
