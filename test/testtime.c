/*
  Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely.
*/

/* Test program to check the resolution of the SDL timer on the current
   platform
*/
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_test.h>

static void PrintInfo(SDL_Renderer *r, SDL_DateTime *dt, int y_offset, SDL_bool local)
{
    int cnt = 0;
    char str[256];

    if (local) {
        SDL_FormatDateTime(str, sizeof(str), "Local:              %c %Z %z", dt);
    } else {
        SDL_FormatDateTime(str, sizeof(str), "UTC:                %c %Z %z", dt);
    }

    SDLTest_DrawString(r, 10, y_offset + (cnt++ * 15), str);
    SDL_FormatDateTime(str, sizeof(str), "ISO-8601:           %F %X", dt);
    SDLTest_DrawString(r, 10, y_offset + (cnt++ * 15), str);
    SDL_FormatDateTime(str, sizeof(str), "ISO-8601 week:      %V", dt);
    SDLTest_DrawString(r, 10, y_offset + (cnt++ * 15), str);
    SDL_FormatDateTime(str, sizeof(str), "12-hour:            %r", dt);
    SDLTest_DrawString(r, 10, y_offset + (cnt++ * 15), str);
    SDL_FormatDateTime(str, sizeof(str), "Month:              %m (%B)", dt);
    SDLTest_DrawString(r, 10, y_offset + (cnt++ * 15), str);
    SDL_FormatDateTime(str, sizeof(str), "Day of year:        %j", dt);
    SDLTest_DrawString(r, 10, y_offset + (cnt++ * 15), str);
    SDL_FormatDateTime(str, sizeof(str), "Day of week:        %w (%A)", dt);
    SDLTest_DrawString(r, 10, y_offset + (cnt++ * 15), str);
    SDL_FormatDateTime(str, sizeof(str), "Seconds since 1970: %s", dt);
    SDLTest_DrawString(r, 10, y_offset + (cnt++ * 15), str);
}

int main(int argc, char *argv[])
{
    SDLTest_CommonState *state;
    SDL_Event event;
    int i, done;

    /* Initialize test framework */
    state = SDLTest_CommonCreateState(argv, SDL_INIT_VIDEO);
    if (!state) {
        return 1;
    }

    /* Enable standard application logging */
    SDL_LogSetPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO);

    /* Parse commandline */
    if (!SDLTest_CommonDefaultArgs(state, argc, argv)) {
        return 1;
    }

    if (!SDLTest_CommonInit(state)) {
        goto quit;
    }

    for (i = 0; i < state->num_windows; ++i) {
        SDL_Renderer *renderer = state->renderers[i];
        SDL_SetRenderDrawColor(renderer, 0x00, 0x00, 0x00, 0xFF);
        SDL_RenderClear(renderer);
    }

    /* Main render loop */
    done = 0;

    while (!done) {
        /* Check for events */
        while (SDL_PollEvent(&event)) {
            SDLTest_CommonEvent(state, &event, &done);
        }

        SDL_SetRenderDrawColor(state->renderers[0], 0x00, 0x00, 0x00, 0xFF);
        SDL_RenderClear(state->renderers[0]);

        SDL_SetRenderDrawColor(state->renderers[0], 0xFF, 0xFF, 0xFF, 0xFF);

        SDL_TimeSpec ts;
        SDL_DateTime dt;
        SDL_GetRealtimeClock(&ts);
        SDL_TimeSpecToUTCDateTime(&ts, &dt);
        PrintInfo(state->renderers[0], &dt, 10, SDL_FALSE);

        SDL_TimeSpecToLocalDateTime(&ts, &dt);
        PrintInfo(state->renderers[0], &dt, 240, SDL_TRUE);

        SDL_RenderPresent(state->renderers[0]);
    }

quit:
    SDLTest_CommonQuit(state);
    return 0;
}