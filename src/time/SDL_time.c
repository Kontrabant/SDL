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

#include "SDL_time_c.h"
#include <time.h>

char sdl_tz_name[2][128];
SDL_Mutex *sdl_tz_lock;

int SDL_InitTime(void)
{
    if (!sdl_tz_lock) {
        SDL_zerop(sdl_tz_name);

        sdl_tz_lock = SDL_CreateMutex();
        if (!sdl_tz_lock) {
            return -1;
        }
    }

    return 0;
}

void SDL_QuitTime(void)
{
    if (sdl_tz_lock) {
        SDL_DestroyMutex(sdl_tz_lock);
        sdl_tz_lock = NULL;
    }

    SDL_zerop(sdl_tz_name);
}

int SDL_FileTimeToUTCDateTime(Sint64 file_time, SDL_DateTime *dt)
{
    if (!dt) {
        return SDL_SetError("Invalid parameter");
    }

    SDL_TimeSpec ts = {
        file_time,
        0
    };

    return SDL_TimeSpecToUTCDateTime(&ts, dt);
}

int SDL_FileTimeToLocalDateTime(Sint64 file_time, SDL_DateTime *dt)
{
    if (!dt) {
        return SDL_SetError("Invalid parameter");
    }

    SDL_TimeSpec ts = {
        file_time,
        0
    };

    return SDL_TimeSpecToLocalDateTime(&ts, dt);
}

int SDL_TimeSpecDifference(SDL_TimeSpec *res, const SDL_TimeSpec *a, const SDL_TimeSpec *b)
{
    if (!res || !a || !b) {
        return SDL_SetError("Invalid parameter");
    }

    res->seconds = a->seconds - b->seconds;
    res->nanoseconds = a->nanoseconds - b->nanoseconds;
    if (res->nanoseconds < 0) {
        res->seconds--;
        res->nanoseconds = SDL_NS_PER_SECOND + res->nanoseconds;
    }

    return 0;
}

int SDL_TimeSpecDiff(SDL_TimeSpec *res, const SDL_TimeSpec *a, const SDL_TimeSpec *b)
{
    if (!res || !a || !b) {
        return SDL_SetError("Invalid parameter");
    }

    res->seconds = a->seconds - b->seconds;
    res->nanoseconds = a->nanoseconds - b->nanoseconds;
    if (res->nanoseconds < 0) {
        res->seconds--;
        res->nanoseconds += SDL_NS_PER_SECOND;
    }

    return 0;
}

Sint64 SDLCALL SDL_TimeSpecDiffNS(const SDL_TimeSpec *a, const SDL_TimeSpec *b)
{
    SDL_TimeSpec res;

    SDL_zero(res);
    SDL_TimeSpecDiff(&res, a, b);

    return (res.seconds * SDL_NS_PER_SECOND) + res.nanoseconds;
}

/* The following algorithms are based on those of Howard Hinnant and are in the public domain.
 *
 * http://howardhinnant.github.io/date_algorithms.html
 */

/* Given a calendar date, returns days since Jan 1 1970, and optionally
 * the day of the week (0-6, 0 is Sunday) and day of the year (0-365).
 */
Sint64 SDL_CivilToDays(int year, int month, int day, int *day_of_week, int *day_of_year)
{

    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = (unsigned)(year - era * 400);                                  // [0, 399]
    const unsigned doy = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1; // [0, 365]
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;                         // [0, 146096]
    const Sint64 z = (Sint64)(era) * 146097 + (Sint64)(doe)-719468;

    if (day_of_week) {
        *day_of_week = (int)(z >= -4 ? (z + 4) % 7 : (z + 5) % 7 + 6);
    }
    if (day_of_year) {
        /* This algorithm considers March 1 to be the first day of the year, so offset by Jan + Feb. */
        if (doy > 305) {
            /* Day 0 is the first day of the year. */
            *day_of_year = doy - 306;
        } else {
            const int doy_offset = 59 + (!(year % 4) && ((year % 100) || !(year % 400)));
            *day_of_year = doy + doy_offset;
        }
    }

    return z;
}

SDL_bool SDL_DateTimeIsValid(const SDL_DateTime *dt)
{
    static const int DAYS_IN_MONTH[] = {
        30, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };

    if (!dt) {
        return SDL_FALSE;
    }
    if (dt->month < 1 || dt->month > 12) {
        SDL_SetError("Malformed SDL_DateTime: month out of range [1-12], current: %i", dt->month);
        return SDL_FALSE;
    }

    const int isLeapYear = !(dt->year % 4) && ((dt->year % 100) || !(dt->year % 400));

    const int days_in_month = DAYS_IN_MONTH[dt->month - 1] + (dt->month == 2 && isLeapYear);
    if (dt->day < 1 || dt->day > days_in_month) {
        SDL_SetError("Malformed SDL_DateTime: day of month out of range [1-%i], current: %i", days_in_month, dt->month);
        return SDL_FALSE;
    }
    if (dt->hour < 0 || dt->hour > 23) {
        SDL_SetError("Malformed SDL_DateTime: hour out of range [0-23], current: %i", dt->hour);
        return SDL_FALSE;
    }
    if (dt->minute < 0 || dt->minute > 59) {
        SDL_SetError("Malformed SDL_DateTime: minute out of range [0-59], current: %i", dt->minute);
        return SDL_FALSE;
    }
    if (dt->second < 0 || dt->second > 60) {
        SDL_SetError("Malformed SDL_DateTime: second out of range [0-60], current: %i", dt->second);
        return SDL_FALSE; /* 60 accounts for a possible leap second. */
    }
    if (dt->nanosecond < 0 || dt->nanosecond >= SDL_NS_PER_SECOND) {
        SDL_SetError("Malformed SDL_DateTime: nanosecond out of range [0-999999999], current: %i", dt->nanosecond);
        return SDL_FALSE;
    }
    if (dt->day_of_week < 0 || dt->day_of_week > 6) {
        SDL_SetError("Malformed SDL_DateTime: day of week out of range [0-6], current: %i", dt->day_of_week);
        return SDL_FALSE;
    }

    const int days_in_year = 364 + isLeapYear;
    if (dt->day_of_year < 0 || dt->day_of_year > days_in_year) {
        SDL_SetError("Malformed SDL_DateTime: day of year out of range [0-%i], current: %i", days_in_year, dt->day_of_year);
        return SDL_FALSE;
    }

    return SDL_TRUE;
}

int SDL_DateTimeCalculateDayOfWeekAndYear(SDL_DateTime *dt)
{
    if (!dt) {
        return SDL_SetError("Invalid parameter");
    }

    dt->day_of_week = 0;
    dt->day_of_year = 0;

    if (!SDL_DateTimeIsValid(dt)) {
        return -1;
    }

    SDL_CivilToDays(dt->year, dt->month, dt->day, &dt->day_of_week, &dt->day_of_year);

    return 0;
}

int SDL_DateTimeToTimeSpec(const SDL_DateTime *dt, SDL_TimeSpec *ts)
{
    if (!dt || !ts) {
        return SDL_SetError("Invalid parameter");
    }
    if (!SDL_DateTimeIsValid(dt)) {
        /* The validation function set the error string. */
        return -1;
    }

    ts->seconds = SDL_CivilToDays(dt->year, dt->month, dt->day, NULL, NULL) * SDL_SECONDS_PER_DAY;
    ts->seconds += (((dt->hour * 60) + dt->minute) * 60) + dt->second - dt->utc_offset;
    ts->nanoseconds = dt->nanosecond;

    return 0;
}

static int weekday_difference(int x, int y)
{
    x -= y;
    return x <= 6 ? x : x + 7;
}

static int iso_week_start_from_year(int y)
{
    int wd;
    const int Monday = 1;
    const int Jan = 1;
    const int tp = SDL_CivilToDays(y, Jan, 4, &wd, NULL);
    return tp - weekday_difference(wd, Monday);
}

static int iso_week_from_civil(int y, int m, int d, Sint64 *iso_year)
{
    const int tp = SDL_CivilToDays(y, m, d, NULL, NULL);
    int iso_week_start = iso_week_start_from_year(y);
    if (tp < iso_week_start) {
        iso_week_start = iso_week_start_from_year(y - 1);
        if (iso_year) {
            *iso_year = y - 1;
        }
    } else {
        const int iso_week_next_year_start = iso_week_start_from_year(y + 1);
        if (tp >= iso_week_next_year_start) {
            iso_week_start = iso_week_next_year_start;
        }
        if (iso_year) {
            *iso_year = y;
        }
    }
    return (tp - iso_week_start) / 7 + 1;
}

#ifdef HAVE_STRFTIME
#ifndef SDL_TIME_WINDOWS
#define STRFTIME_POSIX
#else
#define STRFTIME_WIN
#endif
#endif

#define PAD_CHAR_NONE -1

#define NUM_FMT_ZP(value, default_prec) \
    do {                                \
        val = value;                    \
        if (!fmt_prec) {                \
            fmt_prec = default_prec;    \
        }                               \
        if (!pad_char) {                \
            pad_char = '0';             \
        }                               \
        goto format_number;             \
    } while (0)

#define NUM_FMT_SP(value, default_prec) \
    do {                                \
        val = value;                    \
        if (!fmt_prec) {                \
            fmt_prec = default_prec;    \
        }                               \
        if (!pad_char) {                \
            pad_char = ' ';             \
        }                               \
        goto format_number;             \
    } while (0)

int SDL_FormatDateTime(char *buffer, size_t buffer_size, const char *format, const SDL_DateTime *dt)
{
#ifndef HAVE_STRFTIME
    static const char *const DAY[] = {
        "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
    };
    static const char *const ABDAY[] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    static const char *const MONTH[] = {
        "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    };
    static const char *const ABMONTH[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
#endif

    char *out = buffer;
    const char *p = format;
    const char *fmt_start;
    const char *item;
    size_t n = 0;
    Sint64 val;
    char fmt_str[32];
    int ret;
    int pad_char;
    int fmt_prec;
    int modifier;
    SDL_bool make_uppercase;

#ifdef HAVE_STRFTIME
    struct tm tm;
#else
    SDL_bool tz_updated = SDL_FALSE;
#endif

    if (!buffer || !format || !dt) {
        return SDL_SetError("Invalid parameter");
    }
    if (!SDL_DateTimeIsValid(dt)) {
        /* The validation function set the error string. */
        return -1;
    }
    if (!buffer_size) {
        return 0;
    }

#ifdef HAVE_STRFTIME
    SDL_zero(tm);
    tm.tm_sec = dt->second;
    tm.tm_min = dt->minute;
    tm.tm_hour = dt->hour;
    tm.tm_mday = dt->day;
    tm.tm_mon = dt->month - 1;
    tm.tm_year = dt->year - 1900;
    tm.tm_wday = dt->day_of_week;
    tm.tm_yday = dt->day_of_year;
    tm.tm_isdst = dt->dst_status != SDL_DST_STATUS_UTC ? (int)dt->dst_status : 0;

#ifdef HAVE_TM_GMTOFF
    tm.tm_gmtoff = dt->utc_offset;
#endif
#endif

    for (; *p != '\0' && n < buffer_size; ++p) {
        if (*p == '%') {
            fmt_start = p;
            pad_char = 0;
            fmt_prec = 0;
            modifier = 0;
            make_uppercase = SDL_FALSE;

        handled_modifier:
            switch (*++p) {
            case '-':
                if (!fmt_prec && !modifier) {
                    pad_char = PAD_CHAR_NONE;
                    goto handled_modifier;
                } else {
                    goto bad_fmt;
                }
            case '_':
                if (!fmt_prec && !modifier) {
                    pad_char = ' ';
                    goto handled_modifier;
                } else {
                    goto bad_fmt;
                }
            case '^':
                if (!modifier) {
                    make_uppercase = SDL_TRUE;
                } else {
                    goto bad_fmt;
                }
            case 'E':
            case 'O':
                if (!modifier) {
                    modifier = *p;
                    goto handled_modifier;
                } else {
                    goto bad_fmt;
                }
#ifdef HAVE_STRFTIME
            case 'a':
                item = "%a";
                goto append_strftime;
            case 'A':
                item = "%A";
                goto append_strftime;
            case 'b':
            case 'h':
                item = "%b";
                goto append_strftime;
            case 'B':
                item = "%B";
                goto append_strftime;
            case 'c':
#ifdef STRFTIME_POSIX
                if (modifier == 'E') {
                    item = "%Ec";
                    goto append_strftime;
                }
#elif defined(STRFTIME_WIN)
                if (modifier == 'E') {
                    item = "%#c";
                    goto append_strftime;
                }
#endif
                item = "%c";
                goto append_strftime;
#else
            case 'a':
                item = ABDAY[dt->day_of_week];
                goto append_str;
            case 'A':
                item = DAY[dt->day_of_week];
                goto append_str;
            case 'b':
            case 'h':
                item = ABMONTH[dt->month - 1];
                goto append_str;
            case 'B':
                item = MONTH[dt->month - 1];
                goto append_str;
            case 'c':
                item = "%a %b %e %H:%M:%S %Y";
                goto recurse_fmt;
#endif
            case 'C':
#ifdef STRFTIME_POSIX
                if (modifier == 'E') {
                    item = "%EC";
                    goto append_strftime;
                }
#endif
                NUM_FMT_ZP(dt->year / 100, 2);
            case 'd':
#ifdef STRFTIME_POSIX
                if (modifier == 'O') {
                    item = "%Od";
                    goto append_strftime;
                }
#endif
                NUM_FMT_ZP(dt->day, 2);
            case 'D':
                item = "%m/%d/%y";
                goto recurse_fmt;
            case 'e':
#ifdef STRFTIME_POSIX
                if (modifier == 'O') {
                    item = "%Oe";
                    goto append_strftime;
                }
#endif
                NUM_FMT_SP(dt->day, 2);
            case 'F':
                item = "%Y-%m-%d";
                goto recurse_fmt;
            case 'g':
                iso_week_from_civil(dt->year, dt->month, dt->day, &val);
                NUM_FMT_ZP(val % 100, 2);
            case 'G':
                iso_week_from_civil(dt->year, dt->month, dt->day, &val);
                NUM_FMT_ZP(val, 4);
            case 'H':
#ifdef STRFTIME_POSIX
                if (modifier == 'O') {
                    item = "%OH";
                    goto append_strftime;
                }
#endif
                NUM_FMT_ZP(dt->hour, 2);
            case 'I':
#ifdef STRFTIME_POSIX
                if (modifier == 'O') {
                    item = "%OI";
                    goto append_strftime;
                }
#endif
                val = dt->hour;
                if (!val) {
                    val = 12;
                } else if (val > 12) {
                    val -= 12;
                }
                NUM_FMT_ZP(val, 2);
            case 'j':
                NUM_FMT_ZP(dt->day_of_year + 1, 3);
            case 'k':
                NUM_FMT_SP(dt->hour, 2);
            case 'l':
                val = dt->hour;
                if (!val) {
                    val = 12;
                } else if (val > 12) {
                    val -= 12;
                }
                NUM_FMT_SP(val, 2);
            case 'm':
#ifdef STRFTIME_POSIX
                if (modifier == 'O') {
                    item = "%Om";
                    goto append_strftime;
                }
#endif
                NUM_FMT_ZP(dt->month, 2);
            case 'M':
#ifdef STRFTIME_POSIX
                if (modifier == 'O') {
                    item = "%OM";
                    goto append_strftime;
                }
#endif
                NUM_FMT_ZP(dt->minute, 2);
            case 'n':
                item = "\n";
                goto append_str;
#ifdef HAVE_STRFTIME
            case 'p':
                item = "%p";
                goto append_strftime;
            case 'P':
                /* Windows strftime doesn't do '%P', and trying to use it causes a crash. */
#ifdef STRFTIME_POSIX
                item = "%P";
#else
                item = "%p";
#endif
                goto append_strftime;
            case 'r':
                item = "%r";
                goto append_strftime;
#else
            case 'p':
                item = dt->hour < 12 ? "AM" : "PM";
                goto append_str;
            case 'P':
                item = dt->hour < 12 ? "am" : "pm";
                goto append_str;
            case 'r':
                item = "%I:%M:%S %p";
                goto recurse_fmt;
#endif
            case 'R':
                item = "%H:%M";
                goto recurse_fmt;
            case 'S':
#ifdef STRFTIME_POSIX
                if (modifier == 'O') {
                    item = "%OS";
                    goto append_strftime;
                }
#endif
                NUM_FMT_ZP(dt->second, 2);
            case 's':
                val = SDL_CivilToDays(dt->year, dt->month, dt->day, NULL, NULL) * SDL_SECONDS_PER_DAY;
                val += (((dt->hour * 60) + dt->minute) * 60) + dt->second - dt->utc_offset;
                NUM_FMT_ZP(val, 0);
            case 't':
                out[n++] = '\t';
                continue;
            case 'T':
                item = "%H:%M:%S";
                goto recurse_fmt;
            case 'u':
#ifdef STRFTIME_POSIX
                if (modifier == 'O') {
                    item = "%Ou";
                    goto append_strftime;
                }
#endif
                /* %u formatter considers Monday to be the first day of the week with Sunday being 7. */
                val = dt->day_of_week ? dt->day_of_week : 7;
                NUM_FMT_ZP(val, 1);
            case 'U':
#ifdef STRFTIME_POSIX
                if (modifier == 'O') {
                    item = "%OU";
                    goto append_strftime;
                }
#endif
                /* Week of year with the week following the first Sunday being week 01. */
                NUM_FMT_ZP((dt->day_of_year + 7 - dt->day_of_week) / 7, 2);

            case 'V':
#ifdef STRFTIME_POSIX
                if (modifier == 'O') {
                    item = "%OV";
                    goto append_strftime;
                }
#endif
                NUM_FMT_ZP(iso_week_from_civil(dt->year, dt->month, dt->day, NULL), 2);
            case 'w':
#ifdef STRFTIME_POSIX
                if (modifier == 'O') {
                    item = "%Ow";
                    goto append_strftime;
                }
#endif
                /* %w formatter considers Sunday the first day of the week. */
                NUM_FMT_ZP(dt->day_of_week, 1);
            case 'W':
#ifdef STRFTIME_POSIX
                if (modifier == 'O') {
                    item = "%OW";
                    goto append_strftime;
                }
#endif
                /* Week of year with the week following the first Monday being week 01. */
                NUM_FMT_ZP((dt->day_of_year + 7 - (dt->day_of_week ? dt->day_of_week - 1 : 6)) / 7, 2);
#ifdef HAVE_STRFTIME
            case 'x':
#ifdef STRFTIME_POSIX
                if (modifier == 'E') {
                    item = "%Ex";
                    goto append_strftime;
                }
#endif
                item = "%x";
                goto append_strftime;
            case 'X':
                item = "%X";
                goto append_strftime;
#else
            case 'x':
                item = "%m/%d/%y";
                goto recurse_fmt;
            case 'X':
                item = "%H:%M:%S";
                goto recurse_fmt;
#endif
            case 'y':
#ifdef STRFTIME_POSIX
                if (modifier == 'E') {
                    item = "%Ey";
                    goto append_strftime;
                } else if (modifier == 'O') {
                    item = "%Oy";
                    goto append_strftime;
                }
#endif
                NUM_FMT_ZP(dt->year % 100, 2);
            case 'Y':
#ifdef STRFTIME_POSIX
                if (modifier == 'E') {
                    item = "%EY";
                    goto append_strftime;
                }
#endif
                NUM_FMT_ZP(dt->year, 4);
            case 'z':
                /* Convert offset in seconds to HHMM */
                val = ((dt->utc_offset / 3600) * 100) + (dt->utc_offset % 3600) / 60;
                item = "%+05d";
                goto append_number;
            case 'Z':
                /* UTC = "GMT", always. strftime will often print the local timezone, which is wrong. */
                if (dt->dst_status == SDL_DST_STATUS_UTC) {
                    item = "UTC";
                    goto append_str;
                }
#ifdef HAVE_STRFTIME
                item = "%Z";
                goto append_strftime;
#else
                if (dt->dst_status != SDL_DST_STATUS_UNKNOWN) {

                    /* The timezone may have changed since the last call. */
                    if (!tz_updated) {
                        SDL_UpdateTZNames();
                        tz_updated = SDL_TRUE; /* Update once per call. */
                    }

                    item = sdl_tz_name[dt->dst_status];
                    if (*item != '\0') {
                        goto append_str;
                    }
                }
                continue;
#endif
            case '%':
                out[n++] = *p;
                continue;
            default:
                if (*p >= '0' && *p <= '9') {
                    if (*p == '0') {
                        if (!fmt_prec && !modifier) {
                            pad_char = '0';
                        } else {
                            goto bad_fmt;
                        }
                    } else if (!modifier) {
                        fmt_prec = (fmt_prec * 10) + (*p - '0');
                    } else {
                        goto bad_fmt;
                    }
                    goto handled_modifier;
                }

                /* Check for unexpected termination. */
                if (*p == '\0') {
                    --p;
                }
                goto bad_fmt;
            }
        } else {
            out[n++] = *p;
            continue;
        }

#ifdef HAVE_STRFTIME
    append_strftime:
        ret = (int)strftime(&out[n], buffer_size - n, item, &tm);
        n += ret;
        goto process_output;
#endif

    append_str:
    {
        const int prev = n;
        ret = SDL_strlcpy(&out[n], item, buffer_size - n);
        n = n + ret <= buffer_size ? n + ret : buffer_size;
        ret = n - prev;
        goto process_output;
    }

    recurse_fmt:
        ret = SDL_FormatDateTime(&out[n], buffer_size - n, item, dt) - 1;
        n += ret;
        /* fall through */

    process_output:
        if (make_uppercase) {
            for (int i = n - ret; i < n; ++i) {
                out[i] = SDL_toupper(out[i]);
            }
        }

        if (pad_char != PAD_CHAR_NONE && ret < fmt_prec) {
            int to_fill = fmt_prec - ret;
            const int start = n - ret;
            const int new_end = n + to_fill;

            if (!pad_char) {
                pad_char = ' ';
            }

            if (new_end < buffer_size) {
                /* Move the entire string */
                SDL_memmove(out + start + to_fill, out + start, ret);
            } else if (new_end - buffer_size < ret) {
                /* Move the partial string */
                SDL_memmove(out + start + to_fill, out + start, ret - (new_end - buffer_size));
            }

            if (to_fill > buffer_size - start) {
                to_fill = buffer_size - start;
            }
            SDL_memset(out + start, pad_char, to_fill);
            n = SDL_min(n + to_fill, buffer_size);
        }
        continue;

    format_number:
    {
        int i = 0;
        fmt_str[i++] = '%';

        if (pad_char != PAD_CHAR_NONE && fmt_prec) {
            if (pad_char == '0') {
                fmt_str[i++] = pad_char;
            }

            SDL_itoa(fmt_prec, &fmt_str[i], 10);
            for (; fmt_str[i] != '\0'; ++i) {
            }
        }

        i += SDL_strlcpy(&fmt_str[i], SDL_PRIs64, sizeof(fmt_str) - i);
        fmt_str[i] = '\0';

        item = fmt_str;
    }

    append_number:
        ret = SDL_snprintf(&out[n], buffer_size - n, item, val);
        n += ret;
        continue;

    bad_fmt:
    {
        /* In the case of a bad format string, copy it directly */
        const size_t cpylen = SDL_min(p - fmt_start + 1, buffer_size - n);
        SDL_memcpy(&out[n], fmt_start, cpylen);
        n += cpylen;
    }
    }

    /* Ensure null termination. */
    if (n < buffer_size) {
        out[n++] = '\0';
    } else {
        n = buffer_size;
        out[n - 1] = '\0';
    }
    return (int)n;
}
