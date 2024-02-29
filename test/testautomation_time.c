/**
 * Timer test suite
 */
#include "testautomation_suites.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_test.h>

/* Test case functions */

/**
 * Call to SDL_GetRealtimeClock
 */
static int time_getRealtimeClock(void *arg)
{
    int result;
    SDL_TimeSpec ts;

    result = SDL_GetRealtimeClock(&ts);
    SDLTest_AssertPass("Call to SDL_GetRealtimeClock()");
    SDLTest_AssertCheck(result == 0, "Check result value, expected 0, got: %i", result);

    return TEST_COMPLETED;
}

static int time_dateTimeConversion(void *arg)
{
    int result;
    SDL_TimeSpec ts[2];
    SDL_DateTime dt;

    SDL_zero(ts);

    result = SDL_GetRealtimeClock(&ts[0]);
    SDLTest_AssertPass("Call to SDL_GetRealtimeClock()");
    SDLTest_AssertCheck(result == 0, "Check result value, expected 0, got: %i", result);

    result = SDL_TimeSpecToUTCDateTime(&ts[0], &dt);
    SDLTest_AssertPass("Call to SDL_TimeSpecToUTCDateTime()");
    SDLTest_AssertCheck(result == 0, "Check result value, expected 0, got: %i", result);

    result = SDL_DateTimeToTimeSpec(&dt, &ts[1]);
    SDLTest_AssertPass("Call to SDL_DateTimeToTimeSpec()");
    SDLTest_AssertCheck(result == 0, "Check result value, expected 0, got: %i", result);

    result = ts[0].seconds == ts[1].seconds && ts[0].nanoseconds == ts[1].nanoseconds;
    SDLTest_AssertCheck(result, "Check that original and converted SDL_TimeSpec values match: sec0 = %" SDL_PRIs64 ", sec1 = %" SDL_PRIs64 ", ns0 = %i, ns1 = %i", ts[0].seconds, ts[1].seconds, ts[0].nanoseconds, ts[1].nanoseconds);

    result = SDL_TimeSpecToLocalDateTime(&ts[0], &dt);
    SDLTest_AssertPass("Call to SDL_TimeSpecToLocalDateTime()");
    SDLTest_AssertCheck(result == 0, "Check result value, expected 0, got: %i", result);

    result = SDL_DateTimeToTimeSpec(&dt, &ts[1]);
    SDLTest_AssertPass("Call to SDL_DateTimeToTimeSpec()");
    SDLTest_AssertCheck(result == 0, "Check result value, expected 0, got: %i", result);

    result = ts[0].seconds == ts[1].seconds && ts[0].nanoseconds == ts[1].nanoseconds;
    SDLTest_AssertCheck(result, "Check that original and converted SDL_TimeSpec values match: TimeSpec[0] = { %" SDL_PRIs64 ", %i } TimeSpec[1] = { %" SDL_PRIs64 ", %i }", ts[0].seconds, ts[0].nanoseconds, ts[1].seconds, ts[1].nanoseconds);

    return TEST_COMPLETED;
}

static void CheckFormat(const char *fmt, const char *expected_str, SDL_DateTime *dt)
{
    char buf[256];
    int result;

    if (expected_str) {
        const int expected_result = SDL_strlen(expected_str) + 1;

        result = SDL_FormatDateTime(buf, sizeof(buf), fmt, dt);
        SDLTest_AssertPass("Call to SDL_FormatDateTime(\"%s\")", fmt);
        SDLTest_AssertCheck(result == expected_result, "Check return result, expected %i, got: %i", expected_result, result);
        SDLTest_AssertCheck(!SDL_strcmp(buf, expected_str), "Resulting string of formatter '%s', expected \"%s\", got: \"%s\"", fmt, expected_str, buf);
    } else {
        /* Locale dependent string, just check for success. */
        result = SDL_FormatDateTime(buf, sizeof(buf), fmt, dt);
        SDLTest_AssertPass("Call to SDL_FormatDateTime(\"%s\")", fmt);
        SDLTest_AssertCheck(result >= 0, "Check return result, expected >=0, got: %i", result);
        SDLTest_Log("Resulting string of formatter '%s': \"%s\"", fmt, buf);
    }
}

static int time_dateTimeFormatting(void *arg)
{
    char buf[8];
    int result;

    SDL_DateTime dt = {
        2000,
        3,
        1,
        16,
        15,
        35,
        0,
        0,
        0,
        0,
        SDL_DST_STATUS_UTC
    };

    result = SDL_DateTimeSetDayOfWeekAndDayOfYear(&dt);
    SDLTest_AssertPass("Call to SDL_DateTimeCalculateDayOfWeekAndYear()");
    SDLTest_AssertCheck(result == 0, "Check return result, expected 0, got: %i", result);

    SDLTest_Log("Set test date set to 2000-03-01 16:15:35 (Wednesday, 59th day of the year)");

    /* These are locale dependent, so can't be verified against a reference. */
    CheckFormat("%a", NULL, &dt);
    CheckFormat("%A", NULL, &dt);
    CheckFormat("%b", NULL, &dt);
    CheckFormat("%B", NULL, &dt);
    CheckFormat("%c", NULL, &dt);
    CheckFormat("%^c", NULL, &dt);

    /* Known, verifiable results. */
    CheckFormat("%C", "20", &dt);
    CheckFormat("%5C", "00020", &dt);
    CheckFormat("%d", "01", &dt);
    CheckFormat("%-d", "1", &dt);
    CheckFormat("%e", " 1", &dt);
    CheckFormat("%-e", "1", &dt); /* Omit padding */
    CheckFormat("%F", "2000-03-01", &dt);
    CheckFormat("%G", "2000", &dt);
    CheckFormat("%g", "00", &dt);
    CheckFormat("%H", "16", &dt);
    CheckFormat("%I", "04", &dt);
    CheckFormat("%_4I", "   4", &dt); /* Replace zeroes with spaces with minimum width of 4. */
    CheckFormat("%j", "061", &dt);
    CheckFormat("%_j", " 61", &dt); /* Replace zeroes with spaces. */
    CheckFormat("%k", "16", &dt);
    CheckFormat("%l", " 4", &dt);
    CheckFormat("%m", "03", &dt);
    CheckFormat("%M", "15", &dt);
    /*CheckFormat("%n", NULL, &dt); Don't print the newline result, as it messes up the logs. */

    /* Locale dependent, just verify non-failure. */
    CheckFormat("%p", NULL, &dt);
    CheckFormat("%P", NULL, &dt);
    CheckFormat("%r", NULL, &dt);
    CheckFormat("%R", NULL, &dt);

    /* Seconds since the epoch. */
    CheckFormat("%s", "951927335", &dt);

    CheckFormat("%T", "16:15:35", &dt);
    CheckFormat("%u", "3", &dt);

    /* Week of year, by various rules. */
    CheckFormat("%U", "09", &dt);
    CheckFormat("%V", "09", &dt);
    CheckFormat("%W", "09", &dt);

    SDLTest_Log("Setting test date to 2000-01-01");

    /* Set to Jan 1 2000 and test ISO 8601 week of year.
     * Since it was a Saturday, it should be week 52 of 1999.
     */
    dt.month = 1;
    dt.day = 1;

    result = SDL_DateTimeSetDayOfWeekAndDayOfYear(&dt);
    SDLTest_AssertPass("Call to SDL_DateTimeCalculateDayOfWeekAndYear()");
    SDLTest_AssertCheck(result == 0, "Check return result, expected 0, got: %i", result);

    CheckFormat("%V", "52", &dt);
    CheckFormat("%G", "1999", &dt);

    /* The %W and %U formatters should be week zero (weeks start Sunday and Monday respectively). */
    CheckFormat("%U", "00", &dt);
    CheckFormat("%W", "00", &dt);

    /* The %j formatter should be 001 */
    CheckFormat("%j", "001", &dt);

    SDLTest_Log("Setting test date to 2000-01-02");

    /* The next day (Sunday)... */
    dt.month = 1;
    dt.day = 2;

    result = SDL_DateTimeSetDayOfWeekAndDayOfYear(&dt);
    SDLTest_AssertPass("Call to SDL_DateTimeCalculateDayOfWeekAndYear()");
    SDLTest_AssertCheck(result == 0, "Check return result, expected 0, got: %i", result);

    /* The %U formatter should be week one */
    CheckFormat("%U", "01", &dt);

    /* The ISO week date is still week 52 of 1999 */
    CheckFormat("%V %G", "52 1999", &dt);

    /* %W should be zero (week 1 starts on Monday) */
    CheckFormat("%W", "00", &dt);

    SDLTest_Log("Setting test date to 2000-01-03");

    /* The next day (Monday)... */
    dt.month = 1;
    dt.day = 3;

    result = SDL_DateTimeSetDayOfWeekAndDayOfYear(&dt);
    SDLTest_AssertPass("Call to SDL_DateTimeCalculateDayOfWeekAndYear()");
    SDLTest_AssertCheck(result == 0, "Check return result, expected 0, got: %i", result);

    /* The %W, %U, and %V formatters should be week one */
    CheckFormat("%U", "01", &dt);
    CheckFormat("%V %G", "01 2000", &dt);
    CheckFormat("%W", "01", &dt);

    /* The next day (Monday)... */
    dt.month = 12;
    dt.day = 31;

    result = SDL_DateTimeSetDayOfWeekAndDayOfYear(&dt);
    SDLTest_AssertPass("Call to SDL_DateTimeCalculateDayOfWeekAndYear()");
    SDLTest_AssertCheck(result == 0, "Check return result, expected 0, got: %i", result);

    SDLTest_Log("Setting test date to 2000-12-31");

    /* The %U formatter should be 53, others 52 */
    CheckFormat("%U", "53", &dt);
    CheckFormat("%V %G", "52 2000", &dt);
    CheckFormat("%W", "52", &dt);

    /* The %j formatter should be 366 */
    CheckFormat("%j", "366", &dt);

    SDLTest_Log("Setting test date to 2000-03-01");

    /* Reset to March 1 */
    dt.month = 3;
    dt.day = 1;

    result = SDL_DateTimeSetDayOfWeekAndDayOfYear(&dt);
    SDLTest_AssertPass("Call to SDL_DateTimeCalculateDayOfWeekAndYear()");
    SDLTest_AssertCheck(result == 0, "Check return result, expected 0, got: %i", result);

    /* Day of week. */
    CheckFormat("%w", "3", &dt);

    /* Locale dependent, just verify non-failure. */
    CheckFormat("%x", NULL, &dt);
    CheckFormat("%X", NULL, &dt);

    CheckFormat("%y", "00", &dt);
    CheckFormat("%Y", "2000", &dt);

    /* UTC offset */
    CheckFormat("%z", "+0000", &dt);
    /* Test %z with EST */
    dt.utc_offset = -18000;
    CheckFormat("%z", "-0500", &dt);
    /* Test %z with CET */
    dt.utc_offset = 3600;
    CheckFormat("%z", "+0100", &dt);

    dt.utc_offset = 0;

    /* Should always be "UTC" */
    CheckFormat("%Z", "UTC", &dt);

    /* Should output the current system timezone. */
    dt.dst_status = SDL_DST_STATUS_STANDARD;
    CheckFormat("%Z", NULL, &dt);

    /* Bad formatting specifiers should just output the formatting string directly. */
    CheckFormat("%33Q", "%33Q", &dt);

    /* Test a bad format with an unexpected terminator. */
    CheckFormat("%33", "%33", &dt);

    /* Multiple modifiers not allowed. */
    CheckFormat("%EOx", "%EOx", &dt);

    /* Rightmost flag takes precedence. */
    CheckFormat("%-_I", " 4", &dt);

    /* Flags after width specifiers not allowed. */
    CheckFormat("%8-I", "%8-I", &dt);

    /* Width specifiers after modifiers not allowed. */
    CheckFormat("%E8I", "%E8I", &dt);

    /* Null parameter check. */
    result = SDL_FormatDateTime(NULL, 0, NULL, NULL);
    SDLTest_AssertPass("Call to SDL_FormatDateTime() with NULL output buffer");
    SDLTest_AssertCheck(result == -1, "Check return result, expected -1, got: %i", result);

    /* Zero size buffer check. */
    result = SDL_FormatDateTime(buf, 0, "", &dt);
    SDLTest_AssertPass("Call to SDL_FormatDateTime() with zero-sized output buffer");
    SDLTest_AssertCheck(result == 0, "Check return result, expected 0, got: %i", result);

    /* Empty format string. */
    result = SDL_FormatDateTime(buf, sizeof(buf), "", &dt);
    SDLTest_AssertPass("Call to SDL_FormatDateTime() with empty format string");
    SDLTest_AssertCheck(result == 1, "Check return result, expected 1, got: %i", result);

    /* Leap year day in a non leap year. */
    dt.year = 2001;
    dt.month = 2;
    dt.day = 29;
    dt.day_of_week = 4;

    result = SDL_FormatDateTime(buf, sizeof(buf), "", &dt);
    SDLTest_AssertPass("Call to SDL_FormatDateTime() with a malformed SDL_DateTime (leap year day in non-leap year)");
    SDLTest_AssertCheck(result == -1, "Check return result, expected -1, got: %i", result);

    return TEST_COMPLETED;
}

/* ================= Test References ================== */

/* Time test cases */
static const SDLTest_TestCaseReference timeTest1 = {
    (SDLTest_TestCaseFp)time_getRealtimeClock, "time_getRealtimeClock", "Call to SDL_GetRealtimeClock", TEST_ENABLED
};

static const SDLTest_TestCaseReference timeTest2 = {
    (SDLTest_TestCaseFp)time_dateTimeConversion, "time_dateTimeConversion", "Call to SDL_TimeSpecToDateTime/SDL_DateTimeToTimeSpec", TEST_ENABLED
};

static const SDLTest_TestCaseReference timeTest3 = {
    (SDLTest_TestCaseFp)time_dateTimeFormatting, "time_dataTimeFormatting", "Call to SDL_FormatDateTime()", TEST_ENABLED
};

/* Sequence of Timer test cases */
static const SDLTest_TestCaseReference *timeTests[] = {
    &timeTest1, &timeTest2, &timeTest3, NULL
};

/* Time test suite (global) */
SDLTest_TestSuiteReference timeTestSuite = {
    "Time",
    NULL,
    timeTests,
    NULL
};
