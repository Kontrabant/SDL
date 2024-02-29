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

#ifdef SDL_TIME_UNIX

#include "../SDL_time_c.h"
#include <sys/time.h>
#include <time.h>
#include <errno.h>

#if !defined(HAVE_CLOCK_GETTIME) && defined(SDL_PLATFORM_APPLE)
#include <mach/clock.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#endif

void SDL_UpdateTZNames()
{
    SDL_LockMutex(sdl_tz_lock);

    tzset();
    if (tzname[0]) {
        SDL_strlcpy(sdl_tz_name[0], tzname[0], sizeof(sdl_tz_name[0]));
    }
    if (tzname[1]) {
        SDL_strlcpy(sdl_tz_name[1], tzname[1], sizeof(sdl_tz_name[1]));
    }

    SDL_UnlockMutex(sdl_tz_lock);
}

int SDL_GetRealtimeClock(SDL_TimeSpec *ts)
{
    if (!ts) {
        return SDL_SetError("Invalid parameter");
    }
#ifdef HAVE_CLOCK_GETTIME
    struct timespec tp;

    if (clock_gettime(CLOCK_REALTIME, &tp) == 0) {
        ts->seconds = tp.tv_sec;
        ts->nanoseconds = tp.tv_nsec;
        return 0;
    }

    SDL_SetError("Failed to retrieve system time (%i)", errno);

#elif defined(SDL_PLATFORM_APPLE)
    clock_serv_t cclock;
    int ret = host_get_clock_service(mach_host_self(), CALENDAR_CLOCK, &cclock);
    if (ret == 0) {
        mach_timespec_t mts;

        SDL_zero(mts);
        ret = clock_get_time(cclock, &mts);
        if (ret == 0) {
            ts->seconds = mts.tv_sec;
            ts->nanoseconds = mts.tv_nsec;
        }
        mach_port_deallocate(mach_task_self(), cclock);

        if (!ret) {
            return 0;
        }
    }

    SDL_SetError("Failed to retrieve system time (%i)", ret);

#elif defined(HAVE_LIBC)
    struct timeval tv;
    SDL_zero(tv);
    if (gettimeofday(&tv, NULL) == 0) {
        ts->seconds = tv.tv_sec;
        ts->nanoseconds = SDL_US_TO_NS(tv.tv_usec);

        return 0;
    }

    SDL_SetError("Failed to retrieve system time (%i)", errno);

#else
    /* No libc means no time functions, just return something. */
    ts->seconds = 946684800; /* Y2K */
    ts->nanoseconds = 0;
#endif

    return -1;
}

int SDL_TimeSpecToUTCDateTime(const SDL_TimeSpec *ts, SDL_DateTime *dt)
{
    if (!ts || !dt) {
        return SDL_SetError("Invalid parameter");
    }

    const time_t tval = (time_t)ts->seconds;

#ifdef HAVE_GMTIME_R
    struct tm tm;
    SDL_zero(tm);

    if (gmtime_r(&tval, &tm)) {
        dt->year = tm.tm_year + 1900;
        dt->month = tm.tm_mon + 1;
        dt->day = tm.tm_mday;
        dt->hour = tm.tm_hour;
        dt->minute = tm.tm_min;
        dt->second = tm.tm_sec;
        dt->nanosecond = ts->nanoseconds;
        dt->day_of_year = tm.tm_yday;
        dt->day_of_week = tm.tm_wday;
        dt->utc_offset = tm.tm_gmtoff;
        dt->dst_status = SDL_DST_STATUS_UTC;

        return 0;
    }
#else
    struct tm *tm;
    if ((tm = gmtime(&tval))) {
        dt->year = tm->tm_year + 1900;
        dt->month = tm->tm_mon + 1;
        dt->day = tm->tm_mday;
        dt->hour = tm->tm_hour;
        dt->minute = tm->tm_min;
        dt->second = tm->tm_sec;
        dt->nanosecond = ts->nanoseconds;
        dt->day_of_year = tm->tm_yday;
        dt->day_of_week = tm->tm_wday;
        dt->utc_offset = tm->tm_gmtoff;
        dt->dst_status = (SDL_DST_STATUS)tm.tm_isdst;

        return 0;
    }
#endif

    return SDL_SetError("UTC time conversion failed (%i)", errno);
}

int SDL_TimeSpecToLocalDateTime(const SDL_TimeSpec *ts, SDL_DateTime *dt)
{
    if (!ts || !dt) {
        return SDL_SetError("Invalid parameter");
    }

    const time_t tval = (time_t)ts->seconds;

#ifdef HAVE_LOCALTIME_R
    struct tm tm;
    SDL_zero(tm);

    if (localtime_r(&tval, &tm)) {
        dt->year = tm.tm_year + 1900;
        dt->month = tm.tm_mon + 1;
        dt->day = tm.tm_mday;
        dt->hour = tm.tm_hour;
        dt->minute = tm.tm_min;
        dt->second = tm.tm_sec;
        dt->nanosecond = ts->nanoseconds;
        dt->day_of_year = tm.tm_yday;
        dt->day_of_week = tm.tm_wday;
        dt->utc_offset = tm.tm_gmtoff;
        dt->dst_status = (SDL_DST_STATUS)tm.tm_isdst;

        return 0;
    }
#else
    struct tm *tm;

    if ((tm = localtime(&tval))) {
        dt->year = tm->tm_year + 1900;
        dt->month = tm->tm_mon + 1;
        dt->day = tm->tm_mday;
        dt->hour = tm->tm_hour;
        dt->minute = tm->tm_min;
        dt->second = tm->tm_sec;
        dt->nanosecond = ts->nanoseconds;
        dt->day_of_year = tm->tm_yday;
        dt->day_of_week = tm->tm_wday;
        dt->utc_offset = tm->tm_gmtoff;
        dt->dst_status = (SDL_DST_STATUS)tm.tm_isdst;

        return 0;
    }
#endif

    return SDL_SetError("Local time conversion failed (%i)", errno);
}

#endif /* SDL_TIME_UNIX */
