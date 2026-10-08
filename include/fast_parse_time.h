#ifndef FAST_PARSE_TIME_H
#define FAST_PARSE_TIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) && defined(FPT_SHARED)
#ifdef FPT_BUILD
#define FPT_API __declspec(dllexport)
#else
#define FPT_API __declspec(dllimport)
#endif
#else
#define FPT_API
#endif
#ifdef __cplusplus
extern "C" {
#endif

#define FPT_VERSION "1.5.0-c17.1"
#define FPT_SOURCE_COMMIT "4841982b791fd0c3a2da8b48e28c7c22e7d86a86"

typedef enum {
    FPT_OK = 0,
    FPT_INVALID_ARGUMENT,
    FPT_OUT_OF_MEMORY,
    FPT_INVALID_UTF8,
    FPT_REGEX_ERROR,
    FPT_OVERFLOW,
    FPT_UNSUPPORTED_FRAME,
    FPT_NON_NUMERIC_CARDINALITY,
    FPT_SOURCE_ERROR
} fpt_status;

typedef enum {
    FPT_FULL_EXPLICIT_DATE = 1,
    FPT_YEAR_ONLY,
    FPT_DAY_MONTH,
    FPT_MONTH_DAY,
    FPT_DAY_MONTH_AMBIGUOUS,
    FPT_MONTH_YEAR,
    FPT_YEAR_MONTH,
    FPT_YEAR_RANGE,
    FPT_SEASON_YEAR,
    FPT_TIMEFRAME_RELATIVE_TO_NOW,
    FPT_NON_SPECIFIC_FUTURE_PAST,
    FPT_EVENT_BASED_RELATIVE_DATE,
    FPT_SEASONAL_OR_QUARTERLY,
    FPT_RECURRENT_DATE,
    FPT_FUZZY_DATE,
    FPT_NO_DATE
} fpt_date_type;

typedef struct {
    const char *text;
    size_t length;
    fpt_date_type date_type;
} fpt_explicit_date;
/* cardinality_text is non-NULL for original symbolic cardinalities such as
 * "2+". */
typedef struct {
    double cardinality;
    const char *cardinality_text;
    const char *frame;
    const char *tense;
} fpt_relative_time;
typedef struct {
    const char *text;
    size_t length;
} fpt_text;
typedef struct {
    fpt_explicit_date *explicit_dates;
    size_t explicit_count;
    fpt_relative_time *relative_times;
    size_t relative_count;
    fpt_text *tokens;
    size_t token_count;
    bool has_value;
    void *_storage;
} fpt_result;

typedef struct fpt_context fpt_context;
/* Zero selects the local current year. weekday=-1 selects the local current
   day. Explicit overrides make testing and reproducible processing independent
   of the clock. */
typedef struct {
    int current_year;
    int weekday;
} fpt_options;
#define FPT_OPTIONS_INIT {0, -1}
#define FPT_RESULT_INIT {0}

FPT_API fpt_status fpt_context_create(const fpt_options *options, fpt_context **out);
FPT_API void fpt_context_destroy(fpt_context *context);
FPT_API void fpt_result_free(fpt_result *result);
FPT_API const char *fpt_status_string(fpt_status status);
FPT_API const char *fpt_date_type_name(fpt_date_type type);
FPT_API fpt_date_type fpt_date_type_find(const char *name);
FPT_API bool fpt_result_has_dates(const fpt_result *result);

/* UTF-8 input; lengths are bytes and may include embedded NULs.
   Every returned allocation belongs to result and is released by
   fpt_result_free. Initialize result to zero; free it before reuse. A context
   may be shared across threads. */
FPT_API fpt_status fpt_parse_dates(fpt_context *, const char *, size_t, fpt_result *);
FPT_API fpt_status fpt_extract_explicit_dates(fpt_context *, const char *, size_t, fpt_result *);
FPT_API fpt_status fpt_extract_relative_times(fpt_context *, const char *, size_t, fpt_result *);
FPT_API fpt_status fpt_parse_time_references(fpt_context *, const char *, size_t, fpt_result *);
FPT_API fpt_status fpt_extract_numeric_dates(fpt_context *, const char *, size_t, fpt_result *);
FPT_API fpt_status fpt_parse_dates_with_type(fpt_context *, const char *, size_t, const char *,
                                             fpt_result *);
FPT_API fpt_status fpt_extract_ambiguous_dates(fpt_context *, const char *, size_t, fpt_result *);
FPT_API fpt_status fpt_extract_full_dates_only(fpt_context *, const char *, size_t, fpt_result *);
FPT_API fpt_status fpt_extract_past_references(fpt_context *, const char *, size_t, fpt_result *);
FPT_API fpt_status fpt_extract_future_references(fpt_context *, const char *, size_t, fpt_result *);
FPT_API fpt_status fpt_has_temporal_info(fpt_context *, const char *, size_t, bool *);

/* Low-level extractor and normalization stages, without implicit normalization.
 */
typedef enum {
    FPT_OP_PARSE,
    FPT_OP_EXPLICIT,
    FPT_OP_RELATIVE,
    FPT_OP_NUMERIC,
    FPT_OP_WRITTEN,
    FPT_OP_HYPHEN_MONTH_YEAR,
    FPT_OP_PROSE_YEAR,
    FPT_OP_ISO8601,
    FPT_OP_ORDINAL,
    FPT_OP_SPACE_MONTH_NUMBER,
    FPT_OP_NORMALIZE,
    FPT_OP_TOKENIZE_NUMERIC,
    FPT_OP_PRECLASSIFY_NUMERIC,
    FPT_OP_VALIDATE_DATE,
    FPT_OP_REPLACE_DIGITS,
    FPT_OP_EXPAND_COMPACT,
    FPT_OP_CLASSIFY_SLASH,
    FPT_OP_CLASSIFY_DOT,
    FPT_OP_CLASSIFY_HYPHEN
} fpt_operation;
FPT_API fpt_status fpt_run(fpt_context *, fpt_operation, const char *, size_t, fpt_result *);
FPT_API fpt_status fpt_classify_delimited(fpt_context *, const char *, size_t, char delimiter,
                                          fpt_date_type *);

typedef struct {
    int64_t days;
    int seconds;
    int microseconds;
} fpt_timedelta;
typedef struct {
    int year, month, day, hour, minute, second, microsecond;
} fpt_datetime;
typedef struct {
    fpt_result parsed;
    fpt_datetime *datetimes;
    fpt_timedelta *timedeltas;
    size_t count;
} fpt_resolved_result;
#define FPT_RESOLVED_RESULT_INIT {0}
FPT_API fpt_status fpt_relative_to_timedelta(const fpt_relative_time *, fpt_timedelta *);
/* Accepts a normalized timedelta: days within +/-999999999, seconds 0..86399,
   microseconds 0..999999, as returned by fpt_relative_to_timedelta. */
FPT_API double fpt_timedelta_total_seconds(const fpt_timedelta *);
FPT_API fpt_status fpt_datetime_now(fpt_datetime *);
FPT_API fpt_status fpt_relative_to_datetime(const fpt_relative_time *, const fpt_datetime *,
                                            fpt_datetime *);
FPT_API fpt_status fpt_resolve_to_timedelta(fpt_context *, const char *, size_t,
                                            fpt_resolved_result *);
FPT_API fpt_status fpt_resolve_to_datetime(fpt_context *, const char *, size_t,
                                           const fpt_datetime *, fpt_resolved_result *);
FPT_API fpt_status fpt_parse_and_resolve(fpt_context *, const char *, size_t, const fpt_datetime *,
                                         fpt_resolved_result *);
FPT_API fpt_status fpt_get_date_range(fpt_context *, const char *, size_t, const fpt_datetime *,
                                      fpt_datetime *, fpt_datetime *, bool *);
FPT_API void fpt_resolved_result_free(fpt_resolved_result *);

/* Calendar occurrence API (1.4.0). Zero components mean unknown/ambiguous.
   Unlike explicit extraction, this API preserves occurrences and duplicates. */
typedef struct {
    int year, month, day;
} fpt_calendar_date;
typedef enum { FPT_DATE_ORDER_AUTO, FPT_DATE_ORDER_MDY, FPT_DATE_ORDER_DMY } fpt_date_order;
typedef struct {
    fpt_date_order date_order;
    bool has_range;
    fpt_calendar_date start, end;
} fpt_calendar_options;
#define FPT_CALENDAR_OPTIONS_INIT {FPT_DATE_ORDER_AUTO, false, {0, 0, 0}, {0, 0, 0}}
typedef struct {
    const char *text;
    size_t length;
    int month, day, year;
    fpt_calendar_date date;
    fpt_calendar_date *candidates;
    size_t candidate_count;
} fpt_parsed_date;
typedef struct {
    fpt_parsed_date *dates;
    size_t count;
    void *_storage;
} fpt_calendar_result;
#define FPT_CALENDAR_RESULT_INIT {0}
FPT_API fpt_status fpt_extract_date_strings(fpt_context *, const fpt_text *, size_t,
                                            fpt_calendar_result *);
FPT_API fpt_status fpt_parse_date_strings(fpt_context *, const fpt_text *, size_t,
                                          const fpt_calendar_options *, fpt_calendar_result *);
FPT_API void fpt_calendar_result_free(fpt_calendar_result *);

#ifdef __cplusplus
}
#endif
#endif
