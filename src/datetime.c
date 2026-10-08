#include "internal.h"

static int64_t days_before_year(int y) {
    int64_t n = y - 1;
    return n * 365 + n / 4 - n / 100 + n / 400;
}
static int64_t ordinal(const fpt_datetime *d) {
    int64_t n = days_before_year(d->year) + d->day - 1;
    for (int m = 1; m < d->month; m++)
        for (int day = 1; valid_calendar(d->year, m, day); day++)
            n++;
    return n;
}
static bool valid_datetime(const fpt_datetime *d) {
    return d && valid_calendar(d->year, d->month, d->day) && d->hour >= 0 && d->hour < 24 &&
           d->minute >= 0 && d->minute < 60 && d->second >= 0 && d->second < 60 &&
           d->microsecond >= 0 && d->microsecond < 1000000;
}
static int64_t clock_microseconds(const fpt_datetime *d) {
    return ((int64_t)d->hour * 3600 + d->minute * 60 + d->second) * 1000000 + d->microsecond;
}
fpt_status fpt_datetime_now(fpt_datetime *out) {
    if (!out)
        return FPT_INVALID_ARGUMENT;
    struct timespec ts;
    if (timespec_get(&ts, TIME_UTC) != TIME_UTC)
        return FPT_INVALID_ARGUMENT;
    struct tm tm;
#ifdef _WIN32
    if (localtime_s(&tm, &ts.tv_sec))
        return FPT_INVALID_ARGUMENT;
#else
    if (!localtime_r(&ts.tv_sec, &tm))
        return FPT_INVALID_ARGUMENT;
#endif
    *out = (fpt_datetime){
        tm.tm_year + 1900, tm.tm_mon + 1,           tm.tm_mday, tm.tm_hour, tm.tm_min,
        tm.tm_sec,         (int)(ts.tv_nsec / 1000)};
    return FPT_OK;
}
fpt_status fpt_relative_to_timedelta(const fpt_relative_time *r, fpt_timedelta *out) {
    if (!r || !out || !r->frame || !r->tense)
        return FPT_INVALID_ARGUMENT;
    if (r->cardinality_text)
        return FPT_NON_NUMERIC_CARDINALITY;
    double x = r->cardinality;
    if (!isfinite(x))
        return FPT_OVERFLOW;
    if (!strcmp(r->tense, "past"))
        x = -x;
    int64_t unit_seconds;
    if (!strcmp(r->frame, "year")) {
        x *= 365;
        unit_seconds = 86400;
    } else if (!strcmp(r->frame, "month")) {
        x *= 30;
        unit_seconds = 86400;
    } else if (!strcmp(r->frame, "week")) {
        x *= 7;
        unit_seconds = 86400;
    } else if (!strcmp(r->frame, "day"))
        unit_seconds = 86400;
    else if (!strcmp(r->frame, "hour"))
        unit_seconds = 3600;
    else if (!strcmp(r->frame, "minute"))
        unit_seconds = 60;
    else if (!strcmp(r->frame, "second"))
        unit_seconds = 1;
    else
        return FPT_UNSUPPORTED_FRAME;
    if (!isfinite(x) || fabs(x) > 86400000000001.0 / (double)unit_seconds)
        return FPT_OVERFLOW;
    /* Split before scaling, as timedelta does, to retain sub-microsecond
       information on negative durations and avoid cancellation against a day. */
    double whole;
    double fractional = modf(x, &whole);
    int64_t seconds = (int64_t)whole * unit_seconds;
    int64_t days = seconds / 86400;
    int64_t remainder = seconds % 86400;
    double micro = fractional * (double)(unit_seconds * 1000000);
    double rounded = floor(micro), fraction = micro - rounded;
    if (fraction > 0.5 || (fraction == 0.5 && fmod(rounded, 2.0) != 0))
        rounded++;
    int64_t us = remainder * 1000000 + (int64_t)rounded;
    if (us < 0) {
        days--;
        us += INT64_C(86400000000);
    }
    if (us >= INT64_C(86400000000)) {
        days++;
        us -= INT64_C(86400000000);
    }
    if (days < -999999999 || days > 999999999)
        return FPT_OVERFLOW;
    *out = (fpt_timedelta){(int64_t)days, (int)(us / 1000000), (int)(us % 1000000)};
    return FPT_OK;
}
double fpt_timedelta_total_seconds(const fpt_timedelta *d) {
    if (!d)
        return 0;
    /* Round the exact rational once, like Python's integer true division.
       Adding the fractional second in floating point loses precision through
       cancellation for negative durations. The binary expansion also avoids
       overflowing int64_t at timedelta's maximum (999999999 days). */
    int64_t seconds = d->days * 86400 + d->seconds;
    unsigned fraction = (unsigned)d->microseconds;
    bool negative = seconds < 0;
    if (negative) {
        seconds = -seconds;
        if (fraction) {
            seconds--;
            fraction = 1000000 - fraction;
        }
    }
    if (!seconds)
        return (negative ? -1.0 : 1.0) * fraction / 1000000.0;
    int bits = 0;
    for (uint64_t n = (uint64_t)seconds; n; n >>= 1)
        bits++;
    int shift = 53 - bits;
    uint64_t mantissa = (uint64_t)seconds;
    for (int i = 0; i < shift; i++) {
        fraction *= 2;
        mantissa = (mantissa << 1) + fraction / 1000000;
        fraction %= 1000000;
    }
    if (fraction > 500000 || (fraction == 500000 && (mantissa & 1)))
        mantissa++;
    double value = ldexp((double)mantissa, -shift);
    return negative ? -value : value;
}
fpt_status fpt_relative_to_datetime(const fpt_relative_time *r, const fpt_datetime *reference,
                                    fpt_datetime *out) {
    if (!out)
        return FPT_INVALID_ARGUMENT;
    fpt_datetime now;
    if (!reference) {
        fpt_status status = fpt_datetime_now(&now);
        if (status)
            return status;
        reference = &now;
    }
    if (!valid_datetime(reference))
        return FPT_INVALID_ARGUMENT;
    fpt_timedelta delta;
    fpt_status status = fpt_relative_to_timedelta(r, &delta);
    if (status)
        return status;
    int64_t us =
        clock_microseconds(reference) + (int64_t)delta.seconds * 1000000 + delta.microseconds;
    int64_t day = ordinal(reference) + delta.days + us / INT64_C(86400000000);
    us %= INT64_C(86400000000);
    if (day < 0 || day >= days_before_year(10000))
        return FPT_OVERFLOW;
    int a = 1, b = 10000;
    while (a + 1 < b) {
        int m = a + (b - a) / 2;
        if (days_before_year(m) <= day)
            a = m;
        else
            b = m;
    }
    day -= days_before_year(a);
    int month = 1;
    for (; month < 12; month++) {
        int n = 28;
        while (valid_calendar(a, month, n + 1))
            n++;
        if (day < n)
            break;
        day -= n;
    }
    *out = (fpt_datetime){a,
                          month,
                          (int)day + 1,
                          (int)(us / INT64_C(3600000000)),
                          (int)((us / 60000000) % 60),
                          (int)((us / 1000000) % 60),
                          (int)(us % 1000000)};
    return FPT_OK;
}
void fpt_resolved_result_free(fpt_resolved_result *r) {
    if (!r)
        return;
    fpt_result_free(&r->parsed);
    free(r->datetimes);
    free(r->timedeltas);
    memset(r, 0, sizeof(*r));
}
static fpt_status resolve(fpt_context *ctx, const char *s, size_t n, const fpt_datetime *reference,
                          fpt_resolved_result *out, bool explicit_dates, bool dates) {
    if (!out)
        return FPT_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    fpt_status status =
        fpt_run(ctx, explicit_dates ? FPT_OP_PARSE : FPT_OP_RELATIVE, s, n, &out->parsed);
    if (status)
        return status;
    out->count = out->parsed.relative_count;
    if (out->count) {
        if (dates)
            out->datetimes = calloc(out->count, sizeof(fpt_datetime));
        else
            out->timedeltas = calloc(out->count, sizeof(fpt_timedelta));
        if (!out->datetimes && !out->timedeltas)
            status = FPT_OUT_OF_MEMORY;
    }
    for (size_t i = 0; !status && i < out->count; i++)
        status =
            dates ? fpt_relative_to_datetime(out->parsed.relative_times + i, reference,
                                             out->datetimes + i)
                  : fpt_relative_to_timedelta(out->parsed.relative_times + i, out->timedeltas + i);
    if (status)
        fpt_resolved_result_free(out);
    return status;
}
fpt_status fpt_resolve_to_timedelta(fpt_context *c, const char *s, size_t n,
                                    fpt_resolved_result *r) {
    return resolve(c, s, n, NULL, r, false, false);
}
fpt_status fpt_resolve_to_datetime(fpt_context *c, const char *s, size_t n, const fpt_datetime *ref,
                                   fpt_resolved_result *r) {
    return resolve(c, s, n, ref, r, false, true);
}
fpt_status fpt_parse_and_resolve(fpt_context *c, const char *s, size_t n, const fpt_datetime *ref,
                                 fpt_resolved_result *r) {
    return resolve(c, s, n, ref, r, true, true);
}
fpt_status fpt_get_date_range(fpt_context *c, const char *s, size_t n, const fpt_datetime *ref,
                              fpt_datetime *start, fpt_datetime *end, bool *found) {
    if (!start || !end || !found)
        return FPT_INVALID_ARGUMENT;
    *found = false;
    fpt_result r = {0};
    fpt_status status = fpt_extract_relative_times(c, s, n, &r);
    if (status)
        return status;
    if (r.relative_count == 2) {
        fpt_datetime a, b;
        status = fpt_relative_to_datetime(r.relative_times, ref, &a);
        if (!status)
            status = fpt_relative_to_datetime(r.relative_times + 1, ref, &b);
        if (!status) {
            bool forward =
                ordinal(&a) < ordinal(&b) ||
                (ordinal(&a) == ordinal(&b) && clock_microseconds(&a) <= clock_microseconds(&b));
            *start = forward ? a : b;
            *end = forward ? b : a;
            *found = true;
        }
    }
    fpt_result_free(&r);
    return status;
}
