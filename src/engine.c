#include "generated/patterns.inc"
#include "internal.h"

const char *const fpt_type_names[] = {"",
                                      "FULL_EXPLICIT_DATE",
                                      "YEAR_ONLY",
                                      "DAY_MONTH",
                                      "MONTH_DAY",
                                      "DAY_MONTH_AMBIGUOUS",
                                      "MONTH_YEAR",
                                      "YEAR_MONTH",
                                      "YEAR_RANGE",
                                      "SEASON_YEAR",
                                      "TIMEFRAME_RELATIVE_TO_NOW",
                                      "NON_SPECIFIC_FUTURE_PAST",
                                      "EVENT_BASED_RELATIVE_DATE",
                                      "SEASONAL_OR_QUARTERLY",
                                      "RECURRENT_DATE",
                                      "FUZZY_DATE",
                                      "NO_DATE"};
const char *fpt_date_type_name(fpt_date_type t) {
    return t >= 1 && t <= FPT_NO_DATE ? fpt_type_names[t] : "";
}
fpt_date_type fpt_date_type_find(const char *name) {
    if (!name)
        return 0;
    str input = literal(name);
    if (!valid_utf8(input))
        return 0;
    input = strip_text(input);
    /* These are all non-ASCII Unicode upper mappings consisting only of ASCII
       letters; other non-ASCII characters cannot match an enum name. */
    char upper[64];
    size_t length = 0, pos = 0;
    while (pos < input.n) {
        uint32_t cp = utf8_next(input, &pos);
        const char *expansion = NULL;
        if (cp == 0x131)
            cp = 'I';
        if (cp == 0x17f)
            cp = 'S';
        if (cp == 0xdf)
            expansion = "SS";
        if (cp >= 0xfb00 && cp <= 0xfb06) {
            static const char *const ligatures[] = {"FF", "FI", "FL", "FFI", "FFL", "ST", "ST"};
            expansion = ligatures[cp - 0xfb00];
        }
        if (expansion) {
            size_t n = strlen(expansion);
            if (n >= sizeof(upper) - length)
                return 0;
            memcpy(upper + length, expansion, n);
            length += n;
        } else {
            if (cp >= 128 || length == sizeof(upper) - 1)
                return 0;
            upper[length++] = (char)cp;
        }
    }
    str s = {upper, length};
    for (int i = 1; i <= FPT_NO_DATE; i++)
        if (strlen(fpt_type_names[i]) == s.n) {
            size_t j = 0;
            for (; j < s.n; j++) {
                unsigned char c = (unsigned char)s.p[j];
                if (c >= 'a' && c <= 'z')
                    c = (unsigned char)(c - 32);
                if (c != (unsigned char)fpt_type_names[i][j])
                    break;
            }
            if (j == s.n)
                return (fpt_date_type)i;
        }
    return 0;
}
const char *fpt_status_string(fpt_status status) {
    static const char *const names[] = {"success",
                                        "invalid argument",
                                        "out of memory",
                                        "invalid UTF-8",
                                        "regular expression error",
                                        "overflow",
                                        "unsupported time frame",
                                        "non-numeric cardinality",
                                        "source parser error"};
    return (unsigned)status < COUNT(names) ? names[status] : "unknown error";
}
static bool local_clock(struct tm *out) {
    time_t t = time(NULL);
#ifdef _WIN32
    return localtime_s(out, &t) == 0;
#else
    return localtime_r(&t, out) != NULL;
#endif
}
fpt_status fpt_context_create(const fpt_options *options, fpt_context **out) {
    if (!out)
        return FPT_INVALID_ARGUMENT;
    *out = NULL;
    fpt_options opt = FPT_OPTIONS_INIT;
    if (options)
        opt = *options;
    if (opt.current_year < 0 || opt.current_year > 9999 || opt.weekday < -1 || opt.weekday > 6)
        return FPT_INVALID_ARGUMENT;
    struct tm now;
    if (!local_clock(&now))
        return FPT_INVALID_ARGUMENT;
    fpt_context *ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
        return FPT_OUT_OF_MEMORY;
#ifdef _WIN32
    ctx->number_locale = _create_locale(LC_NUMERIC, "C");
#else
    ctx->number_locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
#endif
    if (!ctx->number_locale) {
        free(ctx);
        return FPT_OUT_OF_MEMORY;
    }
    ctx->year = opt.current_year ? opt.current_year : now.tm_year + 1900;
    ctx->weekday = opt.weekday;
    ctx->regex_count = RX_COUNT;
    ctx->regex = calloc(RX_COUNT, sizeof(pcre2_code *));
    if (!ctx->regex) {
        ctx->regex_count = 0;
        fpt_context_destroy(ctx);
        return FPT_OUT_OF_MEMORY;
    }
    for (int i = 0; i < RX_COUNT; i++) {
        int error;
        PCRE2_SIZE pos;
        ctx->regex[i] = pcre2_compile((PCRE2_SPTR)regex_patterns[i], PCRE2_ZERO_TERMINATED,
                                      PCRE2_UTF, &error, &pos, NULL);
        if (!ctx->regex[i]) {
            fpt_context_destroy(ctx);
            return error == PCRE2_ERROR_NOMEMORY ? FPT_OUT_OF_MEMORY : FPT_REGEX_ERROR;
        }
    }
    *out = ctx;
    return FPT_OK;
}
void fpt_context_destroy(fpt_context *ctx) {
    if (!ctx)
        return;
    for (size_t i = 0; i < ctx->regex_count; i++)
        pcre2_code_free(ctx->regex[i]);
    free(ctx->regex);
#ifdef _WIN32
    _free_locale(ctx->number_locale);
#else
    freelocale(ctx->number_locale);
#endif
    free(ctx);
}
bool regex_find(work *w, int id, str s, size_t start, match *m) {
    int rc = pcre2_match(w->ctx->regex[id], (PCRE2_SPTR)s.p, s.n, start, PCRE2_NO_UTF_CHECK,
                         w->match, NULL);
    if (rc == PCRE2_ERROR_NOMATCH)
        return false;
    if (rc < 0)
        fail(w, rc == PCRE2_ERROR_NOMEMORY ? FPT_OUT_OF_MEMORY : FPT_REGEX_ERROR);
    PCRE2_SIZE *o = pcre2_get_ovector_pointer(w->match);
    for (int i = 0; i < 10; i++) {
        m->start[i] = i < rc ? o[2 * i] : SIZE_MAX;
        m->end[i] = i < rc ? o[2 * i + 1] : SIZE_MAX;
    }
    return true;
}
str group(str s, match *m, int n) {
    if (m->start[n] == SIZE_MAX)
        return (str){"", 0};
    return (str){s.p + m->start[n], m->end[n] - m->start[n]};
}
int group_number(work *w, int id, const char *name) {
    return pcre2_substring_number_from_name(w->ctx->regex[id], (PCRE2_SPTR)name);
}
bool valid_utf8(str s) {
    size_t i = 0;
    while (i < s.n) {
        unsigned c = (unsigned char)s.p[i++];
        if (c < 128)
            continue;
        unsigned n, cp, min;
        if (c >= 194 && c <= 223) {
            n = 1;
            cp = c & 31;
            min = 128;
        } else if (c >= 224 && c <= 239) {
            n = 2;
            cp = c & 15;
            min = 2048;
        } else if (c >= 240 && c <= 244) {
            n = 3;
            cp = c & 7;
            min = 65536;
        } else
            return false;
        if (s.n - i < n)
            return false;
        while (n--) {
            c = (unsigned char)s.p[i++];
            if ((c & 192) != 128)
                return false;
            cp = (cp << 6) | (c & 63);
        }
        if (cp < min || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
            return false;
    }
    return true;
}
static void output_tokens(work *w, words tokens) {
    w->result.tokens = alloc_mem(w, tokens.n * sizeof(fpt_text));
    w->result.token_count = tokens.n;
    for (size_t i = 0; i < tokens.n; i++) {
        str s = copy_str(w, tokens.v[i].p, tokens.v[i].n);
        w->result.tokens[i] = (fpt_text){s.p, s.n};
    }
    w->result.has_value = tokens.n > 0;
}
fpt_status fpt_run(fpt_context *ctx, fpt_operation op, const char *text, size_t length,
                   fpt_result *out) {
    if (!ctx || !out || (!text && length) || op < FPT_OP_PARSE || op > FPT_OP_CLASSIFY_HYPHEN)
        return FPT_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (!valid_utf8((str){text ? text : "", length}))
        return FPT_INVALID_UTF8;
    /* Heap work keeps all arena fields defined after longjmp (C17 7.13.2.1). */
    work *w = calloc(1, sizeof(*w));
    if (!w)
        return FPT_OUT_OF_MEMORY;
    w->ctx = ctx;
    w->input = (str){text ? text : "", length};
    int error = setjmp(w->failure);
    if (!error) {
        w->match = pcre2_match_data_create(10, NULL);
        if (!w->match)
            fail(w, FPT_OUT_OF_MEMORY);
        struct tm now;
        if (!local_clock(&now))
            fail(w, FPT_INVALID_ARGUMENT);
        w->weekday = ctx->weekday < 0 ? (now.tm_wday + 6) % 7 : ctx->weekday;
        str input = copy_str(w, w->input.p, w->input.n);
        if (op == FPT_OP_NORMALIZE || op == FPT_OP_EXPAND_COMPACT) {
            str s = op == FPT_OP_NORMALIZE ? normalize(w, input) : expand_compact(w, input);
            words v = {0};
            push_word(w, &v, s);
            output_tokens(w, v);
        } else if (op == FPT_OP_TOKENIZE_NUMERIC)
            output_tokens(w, tokenize_numeric(w, input));
        else if (op == FPT_OP_PRECLASSIFY_NUMERIC)
            w->result.has_value = preclassify_numeric(w, input);
        else if (op == FPT_OP_VALIDATE_DATE)
            w->result.has_value = validate_date(w, input);
        else if (op == FPT_OP_REPLACE_DIGITS)
            output_tokens(w, replace_digits(w, split_words(w, input)));
        else if (op >= FPT_OP_CLASSIFY_SLASH) {
            fpt_date_type t = classify_delimited(w, input,
                                                 op == FPT_OP_CLASSIFY_SLASH ? '/'
                                                 : op == FPT_OP_CLASSIFY_DOT ? '.'
                                                                             : '-');
            if (t)
                add_date(w, input, t, true);
            w->result.has_value = t != 0;
        } else {
            if (op != FPT_OP_RELATIVE)
                extract_explicit(w, input, op);
            if (op == FPT_OP_PARSE || op == FPT_OP_RELATIVE)
                extract_relative(w, input);
            w->result.has_value = w->result.explicit_count || w->result.relative_count;
        }
    }
    pcre2_match_data_free(w->match);
    w->result._storage = w->memory;
    if (error)
        fpt_result_free(&w->result);
    else
        *out = w->result;
    free(w);
    return (fpt_status)error;
}
bool fpt_result_has_dates(const fpt_result *r) {
    return r && (r->explicit_count || r->relative_count);
}
#define WRAPPER(name, op)                                                                          \
    fpt_status name(fpt_context *c, const char *s, size_t n, fpt_result *r) {                      \
        return fpt_run(c, op, s, n, r);                                                            \
    }
WRAPPER(fpt_parse_dates, FPT_OP_PARSE)
WRAPPER(fpt_extract_explicit_dates, FPT_OP_EXPLICIT)
WRAPPER(fpt_extract_relative_times, FPT_OP_RELATIVE)
WRAPPER(fpt_parse_time_references, FPT_OP_RELATIVE)
WRAPPER(fpt_extract_numeric_dates, FPT_OP_NUMERIC)
fpt_status fpt_classify_delimited(fpt_context *c, const char *s, size_t n, char delimiter,
                                  fpt_date_type *out) {
    if (!out)
        return FPT_INVALID_ARGUMENT;
    *out = 0;
    if (delimiter != '/' && delimiter != '.' && delimiter != '-')
        return FPT_INVALID_ARGUMENT;
    fpt_result r = {0};
    fpt_status status = fpt_run(c,
                                delimiter == '/'   ? FPT_OP_CLASSIFY_SLASH
                                : delimiter == '.' ? FPT_OP_CLASSIFY_DOT
                                                   : FPT_OP_CLASSIFY_HYPHEN,
                                s, n, &r);
    if (!status && r.explicit_count)
        *out = r.explicit_dates[0].date_type;
    fpt_result_free(&r);
    return status;
}
fpt_status fpt_parse_dates_with_type(fpt_context *c, const char *s, size_t n, const char *type,
                                     fpt_result *r) {
    fpt_status status = fpt_extract_explicit_dates(c, s, n, r);
    if (status || !type)
        return status;
    size_t j = 0;
    for (size_t i = 0; i < r->explicit_count; i++)
        if (!strcmp(fpt_date_type_name(r->explicit_dates[i].date_type), type))
            r->explicit_dates[j++] = r->explicit_dates[i];
    r->explicit_count = j;
    r->has_value = j > 0;
    return FPT_OK;
}
fpt_status fpt_extract_ambiguous_dates(fpt_context *c, const char *s, size_t n, fpt_result *r) {
    return fpt_parse_dates_with_type(c, s, n, "DAY_MONTH_AMBIGUOUS", r);
}
fpt_status fpt_extract_full_dates_only(fpt_context *c, const char *s, size_t n, fpt_result *r) {
    return fpt_parse_dates_with_type(c, s, n, "FULL_EXPLICIT_DATE", r);
}
static fpt_status by_tense(fpt_context *c, const char *s, size_t n, fpt_result *r,
                           const char *tense) {
    fpt_status status = fpt_extract_relative_times(c, s, n, r);
    if (status)
        return status;
    size_t j = 0;
    for (size_t i = 0; i < r->relative_count; i++)
        if (!strcmp(r->relative_times[i].tense, tense))
            r->relative_times[j++] = r->relative_times[i];
    r->relative_count = j;
    r->has_value = j > 0;
    return FPT_OK;
}
fpt_status fpt_extract_past_references(fpt_context *c, const char *s, size_t n, fpt_result *r) {
    return by_tense(c, s, n, r, "past");
}
fpt_status fpt_extract_future_references(fpt_context *c, const char *s, size_t n, fpt_result *r) {
    return by_tense(c, s, n, r, "future");
}
fpt_status fpt_has_temporal_info(fpt_context *c, const char *s, size_t n, bool *out) {
    if (!out)
        return FPT_INVALID_ARGUMENT;
    *out = false;
    fpt_result r = {0};
    fpt_status status = fpt_parse_dates(c, s, n, &r);
    if (!status)
        *out = fpt_result_has_dates(&r);
    fpt_result_free(&r);
    return status;
}
