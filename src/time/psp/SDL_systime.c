/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>

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

#ifdef SDL_TIME_PSP

#include <psprtc.h>
#include <psputility_sysparam.h>

#include "../SDL_time_c.h"

/* Sony seems to use 0001-01-01T00:00:00 as an epoch. */
#define UNIX_EPOCH_OFFSET_SEC 62135596800ULL

void SDL_UpdateTZNames()
{
    int tzOffset = 0;

    SDL_LockMutex(sdl_tz_lock);

    if (sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_TIMEZONE, &tzOffset) == 0) {

        const int hours = tzOffset / 60;
        const int fmt = (hours * 100) + (tzOffset - (hours * 60));

        SDL_snprintf(sdl_tz_name[0], sizeof(sdl_tz_name[0]), "GMT%+05d", fmt);
        SDL_snprintf(sdl_tz_name[1], sizeof(sdl_tz_name[1]), "GMT%+05d DST", fmt);
    }

    SDL_UnlockMutex(sdl_tz_lock);
}

int SDL_GetRealtimeClock(SDL_TimeSpec *ts)
{
    u64 ticks;

    if (!ts) {
        return SDL_SetError("Invalid parameter");
    }

    const int ret = sceRtcGetCurrentTick(&ticks);
    if (!ret) {
        const u32 res = sceRtcGetTickResolution();
        ts->seconds = (Sint64)(ticks / res) - UNIX_EPOCH_OFFSET_SEC;
        ts->nanoseconds = (int)(ticks / res) * (SDL_NS_PER_SECOND / res);
        return 0;
    }

    return SDL_SetError("Failed to retrieve system time (%i)", ret);
}

int SDL_TimeSpecToUTCDateTime(const SDL_TimeSpec *ts, SDL_DateTime *dt)
{
    ScePspDateTime t;

    if (!ts || !dt) {
        return SDL_SetError("Invalid parameter");
    }

    const u32 res = sceRtcGetTickResolution();
    const Uint64 ticks = ((ts->seconds + UNIX_EPOCH_OFFSET_SEC) * res) + (ts->nanoseconds * (SDL_NS_PER_SECOND / res));

    const int ret = sceRtcSetTick(&t, &ticks);
    if (!ret) {
        dt->year = t.year;
        dt->month = t.month;
        dt->day = t.day;
        dt->hour = t.hour;
        dt->minute = t.minute;
        dt->second = t.second;
        dt->nanosecond = t.microsecond * 1000;
        dt->utc_offset = 0;
        dt->dst_status = SDL_DST_STATUS_UTC;

        SDL_CivilToDays(dt->year, dt->month, dt->day, &dt->day_of_week, &dt->day_of_year);

        return 0;
    }

    return SDL_SetError("UTC time conversion failed (%i)", ret);
}

int SDL_TimeSpecToLocalDateTime(const SDL_TimeSpec *ts, SDL_DateTime *dt)
{
    ScePspDateTime t;
    u64 local;

    if (!ts || !dt) {
        return SDL_SetError("Invalid parameter");
    }

    const u32 res = sceRtcGetTickResolution();
    const Uint64 ticks = ((ts->seconds + UNIX_EPOCH_OFFSET_SEC) * res) + (ts->nanoseconds * (SDL_NS_PER_SECOND / res));

    int ret = sceRtcConvertUtcToLocalTime(&ticks, &local);
    if (!ret) {
        ret = sceRtcSetTick(&t, &local);
        if (!ret) {
            dt->year = t.year;
            dt->month = t.month;
            dt->day = t.day;
            dt->hour = t.hour;
            dt->minute = t.minute;
            dt->second = t.second;
            dt->nanosecond = t.microsecond * 1000;

            const Sint64 utc_sec = ticks / res;
            const Sint64 local_sec = local / res;
            dt->utc_offset = (int)(local_sec - utc_sec);

            int dst = 0;
            if (sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_DAYLIGHTSAVINGS, &dst) == 0) {
                dt->dst_status = (SDL_DST_STATUS)dst;
            } else {
                dt->dst_status = SDL_DST_STATUS_UNKNOWN;
            }

            SDL_CivilToDays(dt->year, dt->month, dt->day, &dt->day_of_week, &dt->day_of_year);

            return 0;
        }
    }

    return SDL_SetError("Local time conversion failed (%i)", ret);
}

#endif /* SDL_TIME_PSP */
