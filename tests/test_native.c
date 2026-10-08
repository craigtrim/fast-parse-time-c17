#include "fast_parse_time.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x);                           \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
int main(void) {
    fpt_context *c = NULL;
    fpt_options opt = {2026, 2};
    CHECK(fpt_context_create(&opt, &c) == FPT_OK);
    fpt_result r = {0};
    const char *s = "Meeting on March 15, 2024 about 5 days ago";
    CHECK(fpt_parse_dates(c, s, strlen(s), &r) == FPT_OK);
    CHECK(r.explicit_count == 1 && r.relative_count == 1);
    CHECK(!strcmp(r.explicit_dates[0].text, "March 15, 2024"));
    CHECK(r.relative_times[0].cardinality == 5);
    fpt_timedelta d;
    CHECK(fpt_relative_to_timedelta(r.relative_times, &d) == FPT_OK);
    CHECK(d.days == -5 && d.seconds == 0);
    fpt_result_free(&r);
    s = "in 1 year and 2 months";
    CHECK(fpt_parse_dates(c, s, strlen(s), &r) == FPT_OK);
    CHECK(r.relative_count == 2);
    CHECK(!strcmp(r.relative_times[0].tense, "future"));
    fpt_result_free(&r);
    fpt_datetime ref = {2024, 3, 1, 12, 0, 0, 123456}, out;
    fpt_relative_time rt = {1, NULL, "day", "past"};
    CHECK(fpt_relative_to_datetime(&rt, &ref, &out) == FPT_OK);
    CHECK(out.year == 2024 && out.month == 2 && out.day == 29 && out.microsecond == 123456);
    rt.frame = "decade";
    CHECK(fpt_relative_to_timedelta(&rt, &d) == FPT_UNSUPPORTED_FRAME);
    rt.cardinality_text = "2+";
    CHECK(fpt_relative_to_timedelta(&rt, &d) == FPT_NON_NUMERIC_CARDINALITY);
    s = "next Monday";
    CHECK(fpt_parse_dates(c, s, strlen(s), &r) == FPT_OK);
    CHECK(r.relative_count == 1 && r.relative_times[0].cardinality == 5);
    fpt_result_free(&r);
    s = "5 days ago\0next week";
    CHECK(fpt_parse_dates(c, s, 21, &r) == FPT_OK);
    fpt_result_free(&r);
    CHECK(fpt_parse_dates(c, "\xff", 1, &r) == FPT_INVALID_UTF8);

    /* Exercise exported wrappers, ownership, and error paths directly in C. */
    s = "4/8 and 04/08/2024";
    CHECK(fpt_extract_ambiguous_dates(c, s, strlen(s), &r) == FPT_OK);
    CHECK(r.explicit_count == 1 && r.explicit_dates[0].date_type == FPT_DAY_MONTH_AMBIGUOUS);
    fpt_result_free(&r);
    CHECK(fpt_extract_full_dates_only(c, s, strlen(s), &r) == FPT_OK);
    CHECK(r.explicit_count == 1 && r.explicit_dates[0].date_type == FPT_FULL_EXPLICIT_DATE);
    fpt_result_free(&r);
    CHECK(fpt_parse_dates_with_type(c, s, strlen(s), "unknown", &r) == FPT_OK);
    CHECK(r.explicit_count == 0);
    fpt_result_free(&r);
    s = "5 days ago and next week";
    CHECK(fpt_extract_past_references(c, s, strlen(s), &r) == FPT_OK);
    CHECK(r.relative_count == 1 && r.relative_times[0].cardinality == 5);
    fpt_result_free(&r);
    CHECK(fpt_extract_future_references(c, s, strlen(s), &r) == FPT_OK);
    CHECK(r.relative_count == 1 && !strcmp(r.relative_times[0].frame, "week"));
    fpt_result_free(&r);
    bool found = false;
    CHECK(fpt_has_temporal_info(c, s, strlen(s), &found) == FPT_OK && found);
    CHECK(fpt_has_temporal_info(c, "", 0, &found) == FPT_OK && !found);
    fpt_datetime start, end;
    CHECK(fpt_get_date_range(c, s, strlen(s), &ref, &start, &end, &found) == FPT_OK);
    CHECK(found && start.month == 2 && start.day == 25 && end.month == 3 && end.day == 8);
    fpt_resolved_result resolved = FPT_RESOLVED_RESULT_INIT;
    CHECK(fpt_resolve_to_timedelta(c, s, strlen(s), &resolved) == FPT_OK);
    CHECK(resolved.count == 2 && resolved.timedeltas[0].days == -5 &&
          resolved.timedeltas[1].days == 7);
    fpt_resolved_result_free(&resolved);
    CHECK(fpt_resolve_to_datetime(c, s, strlen(s), &ref, &resolved) == FPT_OK);
    CHECK(resolved.count == 2 && resolved.datetimes[0].day == 25);
    fpt_resolved_result_free(&resolved);
    s = "March 15, 2024 about five days ago";
    CHECK(fpt_parse_and_resolve(c, s, strlen(s), &ref, &resolved) == FPT_OK);
    CHECK(resolved.count == 1 && resolved.parsed.explicit_count == 1);
    fpt_resolved_result_free(&resolved);
    rt = (fpt_relative_time){1.0000005, NULL, "second", "past"};
    CHECK(fpt_relative_to_timedelta(&rt, &d) == FPT_OK);
    CHECK(d.days == -1 && d.seconds == 86398 && d.microseconds == 999999);
    CHECK(fpt_timedelta_total_seconds(&d) == -1.000001);
    rt = (fpt_relative_time){1, NULL, "day", "past"};
    fpt_datetime minimum = {1, 1, 1, 0, 0, 0, 0};
    CHECK(fpt_relative_to_datetime(&rt, &minimum, &out) == FPT_OVERFLOW);
    fpt_date_type type;
    CHECK(fpt_classify_delimited(c, "30/2", 4, '/', &type) == FPT_OK && type == FPT_DAY_MONTH);
    CHECK(fpt_date_type_find("  full_explicit_date ") == FPT_FULL_EXPLICIT_DATE);
    CHECK(fpt_date_type_find("\xc5\xbf"
                             "EASON_YEAR") == FPT_SEASON_YEAR);
    CHECK(fpt_date_type_find("NON_SPECIFIC_FUTURE_PA\xef\xac\x86") == FPT_NON_SPECIFIC_FUTURE_PAST);
    /* The host's decimal separator must not change Python-style float input. */
    if (setlocale(LC_NUMERIC, "German_Germany.1252") || setlocale(LC_NUMERIC, "de_DE.UTF-8")) {
        s = "2.5 hours ago";
        CHECK(fpt_extract_relative_times(c, s, strlen(s), &r) == FPT_OK);
        CHECK(r.relative_count == 1 && r.relative_times[0].cardinality == 2);
        fpt_result_free(&r);
    }
    CHECK(setlocale(LC_NUMERIC, "C") != NULL);
    s = "zero-point-one-two-three one-point-one two-billion-point-zero";
    CHECK(fpt_run(c, FPT_OP_REPLACE_DIGITS, s, strlen(s), &r) == FPT_OK);
    CHECK(r.token_count == 3 && !strcmp(r.tokens[0].text, "0.123") &&
          !strcmp(r.tokens[1].text, "1.1") && !strcmp(r.tokens[2].text, "2000000000.0"));
    fpt_result_free(&r);
    s = "2\xc5\xbf";
    CHECK(fpt_run(c, FPT_OP_EXPAND_COMPACT, s, strlen(s), &r) == FPT_OK);
    CHECK(r.token_count == 1 && !strcmp(r.tokens[0].text, s));
    fpt_result_free(&r);

    const char *calendar_text = "11/28/2025 4/14/2026 March 13 March 13";
    fpt_text text = {calendar_text, strlen(calendar_text)};
    fpt_calendar_result calendar = FPT_CALENDAR_RESULT_INIT;
    CHECK(fpt_parse_date_strings(c, &text, 1, NULL, &calendar) == FPT_OK);
    CHECK(calendar.count == 4 && calendar.dates[2].year == 2026);
    CHECK(calendar.dates[2].date.month == 3 && calendar.dates[2].date.day == 13);
    CHECK(calendar.dates[3].candidate_count == 1);
    fpt_calendar_result_free(&calendar);
    text = (fpt_text){"3/4", 3};
    fpt_calendar_options bounds = {FPT_DATE_ORDER_AUTO, true, {2011, 1, 1}, {2011, 12, 31}};
    CHECK(fpt_parse_date_strings(c, &text, 1, &bounds, &calendar) == FPT_OK);
    CHECK(calendar.count == 1 && calendar.dates[0].candidate_count == 2);
    CHECK(calendar.dates[0].month == 0 && calendar.dates[0].day == 0 &&
          calendar.dates[0].year == 2011);
    CHECK(calendar.dates[0].date.year == 0);
    fpt_calendar_result_free(&calendar);
    bounds.start.year = 2012;
    CHECK(fpt_parse_date_strings(c, &text, 1, &bounds, &calendar) == FPT_INVALID_ARGUMENT);
    CHECK(fpt_parse_date_strings(c, NULL, 0, NULL, &calendar) == FPT_OK && calendar.count == 0);
    fpt_calendar_result_free(&calendar);
    text = (fpt_text){"February 30, 2024", 17};
    CHECK(fpt_extract_date_strings(c, &text, 1, &calendar) == FPT_OK && calendar.count == 0);
    fpt_calendar_result_free(&calendar);

    /* Repeated independent requests must not retain earlier results or state. */
    for (int i = 0; i < 1000; ++i) {
        CHECK(fpt_parse_dates(c, i % 2 ? "yesterday" : "", i % 2 ? 9 : 0, &r) == FPT_OK);
        CHECK(r.relative_count == (size_t)(i % 2));
        fpt_result_free(&r);
    }
    fpt_context_destroy(c);
    puts("Native API checks passed");
    return 0;
}
