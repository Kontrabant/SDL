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

#ifndef SDL_time_h_
#define SDL_time_h_

/**
 *  \file SDL_time.h
 *
 *  Header for the SDL realtime clock and date/time routines.
 */

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>

#include <SDL3/SDL_begin_code.h>
/* Set up for C function definitions, even when using C++ */
#ifdef __cplusplus
extern "C" {
#endif

/**
 *
 * A structure holding a time interval broken down into seconds and nanoseconds.
 *
 * \sa SDL_GetRealtimeClock()
 * \sa SDL_TimeSpecToUTCDateTime()
 * \sa SDL_TimeSpecToLocalDateTime()
 * \sa SDL_DateTimeToTimeSpec()
 */
typedef struct SDL_TimeSpec
{
    Sint64 seconds;  /**< Seconds */
    int nanoseconds; /**< Nanoseconds */
} SDL_TimeSpec;

/**
 *
 * The daylight savings time status for a given time.
 *
 * \sa SDL_DateTime
 */
typedef enum SDL_DST_STATUS
{
    SDL_DST_STATUS_UNKNOWN = -1, /**< DST status is unknown */
    SDL_DST_STATUS_STANDARD = 0, /**< DST is not in effect */
    SDL_DST_STATUS_ACTIVE = 1,   /**< DST is in effect */
    SDL_DST_STATUS_UTC = 2       /**< Time is in UTC (DST not applicable) */
} SDL_DST_STATUS;

/**
 *
 * A structure holding a calendar date and time broken down into its components.
 *
 * \sa SDL_GetRealtimeClock()
 * \sa SDL_TimeSpecToUTCDateTime()
 * \sa SDL_TimeSpecToLocalDateTime()
 * \sa SDL_DateTimeToTimeSpec()
 * \sa SDL_FileTimeToUTCDateTime()
 * \sa SDL_FileTimeToLocalDateTime
 */
typedef struct SDL_DateTime
{
    int year;                  /**< Year */
    int month;                 /**< Month [01-12] */
    int day;                   /**< Day of the month [01-31] */
    int hour;                  /**< Hour */
    int minute;                /**< Minute */
    int second;                /**< Seconds */
    int nanosecond;            /**< Nanoseconds */
    int day_of_year;           /**< Day of the year [0-365] */
    int day_of_week;           /**< Day of the week [0-6] (0 being Sunday) */
    int utc_offset;            /**< Seconds east of UTC */
    SDL_DST_STATUS dst_status; /**< DST status of date/time */
} SDL_DateTime;

/**
 *
 * Gets the current value of the system realtime clock in seconds and nanoseconds since Jan 1, 1970 UTC.
 *
 * \param ts the SDL_TimeSpec to hold the returned time
 * \returns 0 on success or -1 on error; call SDL_GetError() for more information.
 *
 * \since This function is available since SDL 3.0.0 
 */
extern DECLSPEC int SDLCALL SDL_GetRealtimeClock(SDL_TimeSpec *ts);

/**
 *
 * Converts a given SDL_TimeSpec since the epoch to a calendar time, expressed in Universal
 * Coordinated Time (UTC), in the SDL_DateTime format.
 *
 * \param ts the SDL_TimeSpec to be converted
 * \param dt the resulting SDL_DateTime
 * \returns 0 on success or -1 on error; call SDL_GetError() for more information.
 *
 * \since This function is available since SDL 3.0.0
 */
extern DECLSPEC int SDLCALL SDL_TimeSpecToUTCDateTime(const SDL_TimeSpec *ts, SDL_DateTime *dt);

/**
 *
 * Converts a given SDL_TimeSpec since the epoch to a calendar time, expressed in local time,
 * in the SDL_DateTime format.
 *
 * \param ts the SDL_TimeSpec to be converted
 * \param dt the resulting SDL_DateTime
 * \returns 0 on success or -1 on error; call SDL_GetError() for more information.
 *
 * \since This function is available since SDL 3.0.0
 */
extern DECLSPEC int SDLCALL SDL_TimeSpecToLocalDateTime(const SDL_TimeSpec *ts, SDL_DateTime *dt);

/**
 *
 * Converts seconds since the epoch to a calendar time, expressed as Universal Coordinated Time (UTC),
 * in the SDL_DateTime format.
 *
 * \param file_time the time in seconds since Jan 1 1970
 * \param dt the resulting SDL_DateTime
 * \returns 0 on success or -1 on error; call SDL_GetError() for more information.
 *
 * \since This function is available since SDL 3.0.0
 */
extern DECLSPEC int SDLCALL SDL_FileTimeToUTCDateTime(Sint64 file_time, SDL_DateTime *dt);

/**
 *
 * Converts seconds since the epoch to a calendar time, expressed as local time, in the SDL_DateTime format.
 *
 * \param file_time the time in seconds since Jan 1 1970
 * \param dt the resulting SDL_DateTime
 * \returns 0 on success or -1 on error; call SDL_GetError() for more information.
 *
 * \since This function is available since SDL 3.0.0
 */
extern DECLSPEC int SDLCALL SDL_FileTimeToLocalDateTime(Sint64 file_time, SDL_DateTime *dt);

/**
 *
 * Converts seconds since the epoch to a calendar time, expressed as local time, in the SDL_DateTime format.
 *
 * \param dt the source SDL_DateTime
 * \param ts the resulting SDL_TimeSpec
 * \returns 0 on success or -1 on error; call SDL_GetError() for more information.
 *
 * \since This function is available since SDL 3.0.0
 */
extern DECLSPEC int SDLCALL SDL_DateTimeToTimeSpec(const SDL_DateTime *dt, SDL_TimeSpec *ts);

/**
 *
 * Given an SDL_DateTime struct with a valid year, month, and day, automatically populate the day_of_week
 * and day_of_year struct members.
 *
 * \param dt the SDL_DateTime
 * \returns 0 on success or -1 on error; call SDL_GetError() for more information.
 *
 * \since This function is available since SDL 3.0.0
 */
extern DECLSPEC int SDLCALL SDL_DateTimeSetDayOfWeekAndDayOfYear(SDL_DateTime *dt);

/**
 *
 * Given an SDL_DateTime struct, check if the members are within valid ranges to represent a real date
 * and time.
 *
 * \param dt the SDL_DateTime
 * \returns SDL_TRUE if all struct members are within valid ranges, otherwise SDL_FALSE; call SDL_GetError()
 *          for more information.
 *
 * \since This function is available since SDL 3.0.0
 */
extern SDL_bool SDLCALL SDL_DateTimeIsValid(const SDL_DateTime *dt);

/**
 * Formats the broken-down SDL_DateTime according to the format specification
 * format and places the result in the character array strp of size max.
 *
 * The following format specifiers may be used to format the date and time.
 * Note that not all specifiers are supported on all platforms, and some may
 * generate locale-dependent output.
 *
 * %a - The abbreviated name of the day of the week.
 *
 * %A - The full name of the day of the week.
 *
 * %b - The abbreviated month name.
 *
 * %B - The full month name.
 *
 * %c - The preferred date and time representation for the current locale.
 *
 * %C - The century number as a 2-digit integer.
 *
 * %d - The day of the month as a decimal number [01-31].
 *
 * %D - Equivalent to %m/%d/%y.
 *
 * %e - Like %d, the day of the month as a decimal number, but a leading zero is replaced by a space.
 *
 * %F - Equivalent to %Y-%m-%d (the ISO 8601 date format).
 *
 * %G - The ISO 8601 week-based year with century as a decimal number.
 *
 * %g - Like %G, but without century, that is, with a 2-digit year [00–99].
 *
 * %h - Equivalent to %b.
 *
 * %H - The hour as a decimal number using a 24-hour clock [00-23].
 *
 * %I - The hour as a decimal number using a 12-hour clock [01-12].
 *
 * %j - The day of the year as a decimal number [001-366].
 *
 * %k - The hour (24-hour clock) as a decimal number [0-23]; single digits are preceded by a blank.
 *
 * %l - The hour (12-hour clock) as a decimal number [1-12]; single digits are preceded by a blank.
 *
 * %m - The month as a decimal number [01-12].
 *
 * %M - The minute as a decimal number [00-59].
 *
 * %n - A newline character.
 *
 * %p - Either "AM" or "PM" according to the given time value, or the corresponding strings for the
 *      current locale.  Noon is treated as "PM" and midnight as "AM".
 *
 * %P - Like %p but in lowercase: "am" or "pm" or a corresponding string for the current locale.
 *
 * %r - The time in a.m. or p.m. notation.
 *
 * %R - The time in 24-hour notation (%H:%M).
 *
 * %s - The number of seconds since the Epoch, 1970-01-01 00:00:00 +0000 (UTC).
 *
 * %S - The second as a decimal number [00-60].
 *
 * %t - A tab character.
 *
 * %T - The time in 24-hour notation (%H:%M:%S).
 *
 * %u - The day of the week as a decimal, range 1 to 7, with Monday being 1.
 *
 * %U - The week number of the current year as a decimal number, range 00 to 53, starting with the first
 *      Sunday as the first day of week 01.
 *
 * %V - The ISO 8601 week number of the current year as a decimal number, range 01 to 53, where week 1 is
 *      the first week, starting on Monday, that has at least 4 days in the new year.
 *
 * %w - The day of the week as a decimal, range 0 to 6, with Sunday being 0.
 *
 * %W - The week number of the current year as a decimal number, range 00 to 53, starting with the first
 *      Monday as the first day of week 01.
 *
 * %x - The preferred date representation for the current locale without the time.
 *
 * %X - The preferred time representation for the current locale without the date.
 *
 * %y - The year as a decimal number without a century [00-99].
 *
 * %Y - The year as a decimal number including the century.
 *
 * %z - The +hhmm or -hhmm numeric timezone (the hour and minute offset from UTC).
 *
 * %Z - The timezone name or abbreviation, if available.
 *
 * %% - A literal '%' character.
 *
 * Some conversion specifications can be modified by preceding the conversion specifier character
 * with a modifier. The result of using these modifiers is platform and locale dependent, and will
 * be ignored if unsupported.
 *
 * The following modifier characters are permitted:
 *
 * 'E' - Use a locale-dependent alternative representation (e.g. Japanese era names).
 *
 * 'O' - Use alternative numeric symbols (e.g. roman numerals).
 *
 * Between the '%' character and the conversion specifier character, an optional flag and field width may
 * be specified. These precede the E or O modifiers, if present.
 *
 * The following flag characters are permitted:
 *
 * '_' - (underscore) Pad a result string with spaces, even if the conversion specifier character uses
*        zero-padding by default.
 *
 * '-' - (dash) Do not pad a result string, even if the conversion specifier character uses padding by
 *       default.
 *
 * '0' - Pad a result string with zeros, even if the conversion specifier character uses space-padding by
 *       default.
 *
 * '^' - Convert ASCII characters in the result string to uppercase.
 *
 * An optional decimal width specifier may follow the (possibly absent) flag.  If the natural size of the
 * field is smaller than this width, then the result string is padded (on the left) to the specified width
 * (e.g. "%4H" outputs "0008", "%_4H" outputs "   8").
 *
 * \param buffer the buffer to hold the resulting formatted string. Guaranteed to be null terminated
 * \param buffer_size the size of the output buffer
 * \param format the formatting string
 * \param dt the SDL_DateTime structure with the date and time to be formatted
 *
 * \returns an int indicating the number of characters used by the resulting string, including the terminating
 *          '\0', or -1 on an error; call SDL_GetError() for more information.
 *
 * \since This function is available since SDL 3.0.0
 */
extern DECLSPEC int SDLCALL SDL_FormatDateTime(char *buffer, size_t buffer_size, const char *format, const SDL_DateTime *dt);

/**
 *
 * Subtracts two SDL_TimeSpec values (a - b) and returns the difference as an SDL_TimeSpec.
 *
 * \param res the SDL_TimeSpec where the result of the operation will be stored
 * \param a the minuend
 * \param b the subtrahend
 * \returns 0 on success or -1 on error; call SDL_GetError() for more information.
 *
 * \since This function is available since SDL 3.0.0 
 */
extern DECLSPEC int SDLCALL SDL_TimeSpecDiff(SDL_TimeSpec *res, const SDL_TimeSpec *a, const SDL_TimeSpec *b);

/**
 *
 * Subtracts two SDL_TimeSpec values (a - b) and returns the difference in nanoseconds.
 *
 * Will not give accurate results if the difference between the time values is greater than
 * approximately 292 years.
 *
 * \param a the minuend
 * \param b the subtrahend
 * \returns the time difference in nanoseconds
 *
 * \since This function is available since SDL 3.0.0
 */
extern DECLSPEC Sint64 SDLCALL SDL_TimeSpecDiffNS(const SDL_TimeSpec *a, const SDL_TimeSpec *b);

/* Ends C function definitions when using C++ */
#ifdef __cplusplus
}
#endif
#include <SDL3/SDL_close_code.h>

#endif /* SDL_time_h_ */
