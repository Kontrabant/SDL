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

#ifdef SDL_TIME_N3DS

#include "../SDL_time_c.h"
#include <3ds.h>

/*
 * The 3DS clock is essentially a simple digital watch and provides
 * no timezone or DST functionality.
 */

/* 3DS epoch is Jan 1 1900 */
#define UNIX_EPOCH_OFFSET_SEC 2208988800LL

/* Returns year/month/day triple in civil calendar
 * Preconditions:  z is number of days since 1970-01-01 and is in the range:
 *                 [INT_MIN, INT_MAX-719468].
 *
 * http://howardhinnant.github.io/date_algorithms.html#civil_from_days
 */
static void civil_from_days(int days, int *year, int *month, int *day)
{
    days += 719468;
    const int era = (days >= 0 ? days : days - 146096) / 146097;
    const unsigned doe = (unsigned)(days - era * 146097);                       // [0, 146096]
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; // [0, 399]
    const int y = (int)(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100); // [0, 365]
    const unsigned mp = (5 * doy + 2) / 153;                      // [0, 11]
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;              // [1, 31]
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;                 // [1, 12]

    *year = y + (m <= 2);
    *month = (int)m;
    *day = (int)d;
}

void SDL_UpdateTZNames()
{
    /* No timezone info; NOP */
}

int SDL_GetRealtimeClock(SDL_TimeSpec *ts)
{
    if (!ts) {
        return SDL_SetError("Invalid parameter");
    }

    /* Returns milliseconds since the epoch. */
    const Uint64 ticks = osGetTime();
    ts->seconds = (Sint64)(ticks / SDL_MS_PER_SECOND) - UNIX_EPOCH_OFFSET_SEC;
    ts->nanoseconds = (int)SDL_MS_TO_NS(ticks % SDL_MS_PER_SECOND);
    return 0;
}

int SDL_TimeSpecToLocalDateTime(const SDL_TimeSpec *ts, SDL_DateTime *dt)
{
    if (!ts || !dt) {
        return SDL_SetError("Invalid parameter");
    }

    const int days = (int)(ts->seconds / SDL_SECONDS_PER_DAY);
    civil_from_days(days, &dt->year, &dt->month, &dt->day);

    int rem = (int)(ts->seconds - (days * SDL_SECONDS_PER_DAY));
    dt->hour = rem / (60 * 60);
    rem -= dt->hour * 60 * 60;
    dt->minute = rem / 60;
    rem -= dt->minute * 60;
    dt->second = rem;
    dt->nanosecond = ts->nanoseconds;
    dt->utc_offset = 0; /* Unknown */
    dt->dst_status = SDL_DST_STATUS_UNKNOWN;

    SDL_CivilToDays(dt->year, dt->month, dt->day, &dt->day_of_week, &dt->day_of_year);

    return 0;
}

int SDL_TimeSpecToUTCDateTime(const SDL_TimeSpec *ts, SDL_DateTime *dt)
{
    /* The 3DS doesn't have a concept of timezones, so just use the local time. */
    return SDL_TimeSpecToLocalDateTime(ts, dt);
}

#endif /* SDL_TIME_N3DS */
