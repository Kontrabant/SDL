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

#ifdef SDL_TIME_WINDOWS

#include "../../core/windows/SDL_windows.h"
#include <minwinbase.h>
#include <timezoneapi.h>

#include "../SDL_time_c.h"

#define NS_PER_WINDOWS_TICK   100ULL
#define WINDOWS_TICK          10000000ULL
#define UNIX_EPOCH_OFFSET_SEC 11644473600ULL

static void SDL_TimeSpecToFileTime(const SDL_TimeSpec *ts, FILETIME *ft)
{
    const Uint64 ticks =
        ((Uint64)(ts->seconds + UNIX_EPOCH_OFFSET_SEC) * WINDOWS_TICK) + (ts->nanoseconds / NS_PER_WINDOWS_TICK);
    ft->dwHighDateTime = (DWORD)(ticks >> 32);
    ft->dwLowDateTime = (DWORD)(ticks & 0xFFFFFFFF);
}

/* GetTimeZoneInformationForYear is Vista+, so dynamically load it so as to not break XP. */
typedef BOOL(WINAPI *GetTimeZoneInformationForYear_t)(USHORT, PDYNAMIC_TIME_ZONE_INFORMATION, LPTIME_ZONE_INFORMATION);

static GetTimeZoneInformationForYear_t LoadGetZoneInformationForYear()
{
    void *module = SDL_LoadObject("kernel32.dll");
    if (!module)
        return NULL;

    return (GetTimeZoneInformationForYear_t)SDL_LoadFunction(module, "GetTimeZoneInformationForYear");
}

static SDL_DST_STATUS IsDSTActiveForTime(USHORT year, Uint64 time)
{
    static GetTimeZoneInformationForYear_t pGetTimeZoneInformationForYear = NULL;

    if (!pGetTimeZoneInformationForYear) {
        pGetTimeZoneInformationForYear = LoadGetZoneInformationForYear();
    }

    TIME_ZONE_INFORMATION tzinfo;
    SDL_zero(tzinfo);

    /* Get the timezone info for the year in question. */
    if (pGetTimeZoneInformationForYear && pGetTimeZoneInformationForYear(year, NULL, &tzinfo)) {
        /* A zero month in standard means no DST changes. */
        if (tzinfo.StandardDate.wMonth != 0) {
            FILETIME dstTimeFT, stdTimeFT;

            /* If the year is zero, wDay refers to the week of the month, not the actual day of the change. */
            if (!tzinfo.StandardDate.wYear) {
                int day_of_week;
                SDL_CivilToDays(year, tzinfo.StandardDate.wMonth, 1, &day_of_week, NULL);
                tzinfo.StandardDate.wDay = (tzinfo.StandardDate.wDay * 7) - day_of_week + tzinfo.StandardDate.wDayOfWeek + 1;
            }
            if (!tzinfo.DaylightDate.wYear) {
                int day_of_week;
                SDL_CivilToDays(year, tzinfo.DaylightDate.wMonth, 1, &day_of_week, NULL);
                tzinfo.DaylightDate.wDay = (tzinfo.DaylightDate.wDay * 7) - day_of_week + tzinfo.DaylightDate.wDayOfWeek + 1;
            }

            tzinfo.StandardDate.wYear = year;
            tzinfo.DaylightDate.wYear = year;
            SystemTimeToFileTime(&tzinfo.StandardDate, &stdTimeFT);
            SystemTimeToFileTime(&tzinfo.DaylightDate, &dstTimeFT);

            const Uint64 stdTime = ((Uint64)stdTimeFT.dwHighDateTime << 32) | (Uint64)stdTimeFT.dwLowDateTime;
            const Uint64 dstTime = ((Uint64)dstTimeFT.dwHighDateTime << 32) | (Uint64)dstTimeFT.dwLowDateTime;

            /* See if the time is within the DST active boundaries. */
            if (dstTime < stdTime) {
                if (time >= dstTime && time < stdTime) {
                    return SDL_DST_STATUS_ACTIVE;
                }
            } else {
                if (time < stdTime || time >= dstTime) {
                    return SDL_DST_STATUS_ACTIVE;
                }
            }
        }

        return SDL_DST_STATUS_STANDARD;
    }

    return SDL_DST_STATUS_UNKNOWN;
}

void SDL_UpdateTZNames()
{
    TIME_ZONE_INFORMATION tzinfo;
    SDL_zero(tzinfo);

    SDL_LockMutex(sdl_tz_lock);

    if (GetTimeZoneInformation(&tzinfo) != TIME_ZONE_ID_INVALID) {
        WideCharToMultiByte(CP_UTF8, 0, tzinfo.StandardName, -1, sdl_tz_name[0], sizeof(sdl_tz_name[0]), NULL, NULL);
        WideCharToMultiByte(CP_UTF8, 0, tzinfo.DaylightName, -1, sdl_tz_name[1], sizeof(sdl_tz_name[1]), NULL, NULL);
    }

    SDL_UnlockMutex(sdl_tz_lock);
}

int SDL_GetRealtimeClock(SDL_TimeSpec *ts)
{
    FILETIME ft;

    if (!ts) {
        return SDL_SetError("Invalid parameter");
    }

    SDL_zero(ft);
    GetSystemTimePreciseAsFileTime(&ft);

    const Uint64 ticks = ((((Uint64)ft.dwHighDateTime << 32) | (Uint64)ft.dwLowDateTime));
    ts->seconds = (Sint64)(ticks / WINDOWS_TICK) - UNIX_EPOCH_OFFSET_SEC;
    ts->nanoseconds = (int)((ticks % WINDOWS_TICK) * NS_PER_WINDOWS_TICK);
    return 0;
}

int SDL_TimeSpecToUTCDateTime(const SDL_TimeSpec *ts, SDL_DateTime *dt)
{
    FILETIME ft;
    SYSTEMTIME st;
    if (!ts || !dt) {
        return SDL_SetError("Invalid parameter");
    }

    SDL_TimeSpecToFileTime(ts, &ft);

    if (FileTimeToSystemTime(&ft, &st)) {
        dt->year = st.wYear;
        dt->month = st.wMonth;
        dt->day = st.wDay;
        dt->hour = st.wHour;
        dt->minute = st.wMinute;
        dt->second = st.wSecond;
        dt->nanosecond = ts->nanoseconds;
        dt->day_of_week = st.wDayOfWeek;
        dt->utc_offset = 0;

        /* Calculate the day of the year. */
        SDL_CivilToDays(dt->year, dt->month, dt->day, NULL, &dt->day_of_year);

        /* GMT is always standard */
        dt->dst_status = SDL_DST_STATUS_UTC;

        return 0;
    }

    return SDL_SetError("UTC time conversion failed (%lu)", GetLastError());
}

int SDL_TimeSpecToLocalDateTime(const SDL_TimeSpec *ts, SDL_DateTime *dt)
{
    FILETIME ft, local_ft;
    SYSTEMTIME utc_st, local_st;

    if (!ts || !dt) {
        return SDL_SetError("Invalid parameter");
    }

    SDL_TimeSpecToFileTime(ts, &ft);

    if (FileTimeToSystemTime(&ft, &utc_st)) {
        if (SystemTimeToTzSpecificLocalTime(NULL, &utc_st, &local_st)) {
            dt->year = local_st.wYear;
            dt->month = local_st.wMonth;
            dt->day = local_st.wDay;
            dt->hour = local_st.wHour;
            dt->minute = local_st.wMinute;
            dt->second = local_st.wSecond;
            dt->nanosecond = ts->nanoseconds;
            dt->day_of_week = local_st.wDayOfWeek;

            /* Calculate the difference for the UTC offset. */
            SystemTimeToFileTime(&local_st, &local_ft);
            const Sint64 utc_sec = (Sint64)((((Uint64)ft.dwHighDateTime << 32) | (Uint64)ft.dwLowDateTime) /
                                            WINDOWS_TICK);
            const Uint64 local_ticks = ((Uint64)local_ft.dwHighDateTime << 32) | (Uint64)local_ft.dwLowDateTime;
            const Sint64 local_sec = (Sint64)(local_ticks / WINDOWS_TICK);
            dt->utc_offset = (int)(local_sec - utc_sec);

            dt->dst_status = IsDSTActiveForTime(local_st.wYear, local_ticks);

            /* Calculate the day of the year. */
            SDL_CivilToDays(dt->year, dt->month, dt->day, NULL, &dt->day_of_year);

            return 0;
        }
    }

    return SDL_SetError("Local time conversion failed (%lu)", GetLastError());
}

#endif /* SDL_TIME_WINDOWS */
