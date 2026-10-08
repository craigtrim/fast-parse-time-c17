#include "internal.h"

typedef struct {
    int month, day;
} month_day;
typedef struct {
    str text;
    month_day pairs[2];
    size_t pair_count;
    int year; /* -1 means absent; 0000 is an invalid explicit year. */
} calendar_token;
typedef struct {
    calendar_token *v;
    size_t n;
} calendar_tokens;
typedef struct {
    int pattern;
    match m;
    size_t sequence;
} calendar_match;

static int date_cmp_value(fpt_calendar_date a, fpt_calendar_date b) {
    if (a.year != b.year)
        return a.year < b.year ? -1 : 1;
    if (a.month != b.month)
        return a.month < b.month ? -1 : 1;
    return a.day < b.day ? -1 : a.day > b.day ? 1 : 0;
}
static int date_cmp(const void *a, const void *b) {
    return date_cmp_value(*(const fpt_calendar_date *)a, *(const fpt_calendar_date *)b);
}
static bool within(fpt_calendar_date d, fpt_calendar_date start, fpt_calendar_date end) {
    return date_cmp_value(d, start) >= 0 && date_cmp_value(d, end) <= 0;
}
static int match_cmp(const void *ap, const void *bp) {
    const calendar_match *a = ap, *b = bp;
    if (a->m.start[0] != b->m.start[0])
        return a->m.start[0] < b->m.start[0] ? -1 : 1;
    if (a->m.end[0] != b->m.end[0])
        return a->m.end[0] > b->m.end[0] ? -1 : 1;
    return a->sequence < b->sequence ? -1 : a->sequence > b->sequence ? 1 : 0;
}
static void token_push(work *w, calendar_tokens *v, calendar_token value) {
    size_t n = v->n;
    if ((n & (n - 1)) == 0) {
        size_t cap = n ? n * 2 : 1;
        if (cap > SIZE_MAX / sizeof(value))
            fail(w, FPT_OUT_OF_MEMORY);
        calendar_token *p = alloc_mem(w, cap * sizeof(value));
        if (n)
            memcpy(p, v->v, n * sizeof(value));
        v->v = p;
    }
    v->v[v->n++] = value;
}
static str named(work *w, str s, calendar_match *m, const char *name) {
    int n = group_number(w, m->pattern, name);
    return n > 0 ? group(s, &m->m, n) : (str){"", 0};
}
static int number(str s) {
    int64_t n;
    if (!integer(s, &n) || n > INT_MAX)
        return -1;
    return (int)n;
}
static void add_pair(calendar_token *t, int month, int day, int year) {
    if (!valid_calendar(year, month, day))
        return;
    if (t->pair_count && t->pairs[0].month == month && t->pairs[0].day == day)
        return;
    t->pairs[t->pair_count++] = (month_day){month, day};
    if (t->pair_count == 2 &&
        (t->pairs[0].month > t->pairs[1].month ||
         (t->pairs[0].month == t->pairs[1].month && t->pairs[0].day > t->pairs[1].day))) {
        month_day temp = t->pairs[0];
        t->pairs[0] = t->pairs[1];
        t->pairs[1] = temp;
    }
}
static void numeric_token_calendar(work *w, str s, calendar_match *m, fpt_date_order order,
                                   calendar_token *t) {
    str a = named(w, s, m, "a"), b = named(w, s, m, "b"), c = named(w, s, m, "c");
    int av = number(a), bv = number(b), cv = number(c);
    if (!c.n) {
        if (a.n > 2 || b.n > 2)
            return;
    } else if (a.n == 4 && b.n <= 2 && c.n <= 2) {
        t->year = av;
        add_pair(t, bv, cv, av);
        return;
    } else if (c.n == 4 && a.n <= 2 && b.n <= 2)
        t->year = cv;
    else
        return;
    int year = t->year < 0 ? 2000 : t->year;
    if (order != FPT_DATE_ORDER_DMY)
        add_pair(t, av, bv, year);
    if (order != FPT_DATE_ORDER_MDY)
        add_pair(t, bv, av, year);
}
static void written_token_calendar(work *w, str s, calendar_match *m, calendar_token *t) {
    static const char *const names[] = {"january",   "february", "march",    "april",
                                        "may",       "june",     "july",     "august",
                                        "september", "october",  "november", "december"};
    str month = lower_text(w, named(w, s, m, "month"));
    int mo = 0;
    bool full = false;
    for (size_t i = 0; i < COUNT(names); i++) {
        if (eq(month, names[i])) {
            mo = (int)i + 1;
            full = true;
            break;
        }
        if (month.n == 3 && !memcmp(month.p, names[i], 3)) {
            mo = (int)i + 1;
            break;
        }
    }
    if (eq(month, "sept"))
        mo = 9;
    if (!mo)
        return;
    int day = number(named(w, s, m, "day"));
    str suffix = lower_text(w, named(w, s, m, "ordinal"));
    const char *expected = day % 100 >= 10 && day % 100 <= 20 ? "th"
                           : day % 10 == 1                    ? "st"
                           : day % 10 == 2                    ? "nd"
                           : day % 10 == 3                    ? "rd"
                                                              : "th";
    if (suffix.n && !eq(suffix, expected))
        return;
    str year = named(w, s, m, "year");
    if (year.n) {
        if (year.n != 4)
            return;
        t->year = number(year);
    }
    add_pair(t, mo, day, t->year < 0 ? 2000 : t->year);
    if (t->year < 0 && t->text.n && t->text.p[t->text.n - 1] == '.' && full)
        t->text.n--;
}
static calendar_tokens scan(work *w, const fpt_text *texts, size_t count, fpt_date_order order) {
    calendar_tokens tokens = {0};
    for (size_t index = 0; index < count; index++) {
        if (!texts[index].text && texts[index].length)
            fail(w, FPT_INVALID_ARGUMENT);
        str s = {texts[index].text ? texts[index].text : "", texts[index].length};
        if (!valid_utf8(s))
            fail(w, FPT_INVALID_UTF8);
        s = copy_str(w, s.p, s.n);
        calendar_match *matches = NULL;
        size_t n = 0;
        for (int id = RX_CALENDAR_NUMERIC; id <= RX_CALENDAR_DAY_FIRST; id++) {
            size_t pos = 0;
            match m;
            while (regex_find(w, id, s, pos, &m)) {
                if ((n & (n - 1)) == 0) {
                    size_t cap = n ? n * 2 : 1;
                    if (cap > SIZE_MAX / sizeof(*matches))
                        fail(w, FPT_OUT_OF_MEMORY);
                    calendar_match *p = alloc_mem(w, cap * sizeof(*p));
                    if (n)
                        memcpy(p, matches, n * sizeof(*p));
                    matches = p;
                }
                matches[n] = (calendar_match){id, m, n};
                n++;
                pos = m.end[0];
            }
        }
        if (n > 1)
            qsort(matches, n, sizeof(*matches), match_cmp);
        size_t covered = 0;
        for (size_t i = 0; i < n; i++) {
            calendar_match *m = matches + i;
            if (m->m.start[0] < covered)
                continue;
            covered = m->m.end[0];
            calendar_token t = {0};
            t.year = -1;
            t.text = group(s, &m->m, 0);
            if (m->pattern == RX_CALENDAR_NUMERIC)
                numeric_token_calendar(w, s, m, order, &t);
            else
                written_token_calendar(w, s, m, &t);
            if (t.pair_count)
                token_push(w, &tokens, t);
        }
    }
    return tokens;
}
static void add_candidate(work *w, fpt_parsed_date *out, fpt_calendar_date date) {
    size_t n = out->candidate_count;
    if ((n & (n - 1)) == 0) {
        size_t cap = n ? n * 2 : 1;
        if (cap > SIZE_MAX / sizeof(date))
            fail(w, FPT_OUT_OF_MEMORY);
        fpt_calendar_date *p = alloc_mem(w, cap * sizeof(date));
        if (n)
            memcpy(p, out->candidates, n * sizeof(date));
        out->candidates = p;
    }
    out->candidates[out->candidate_count++] = date;
}
static void infer(work *w, calendar_tokens tokens, fpt_calendar_options opt,
                  fpt_calendar_result *result, bool extract_only) {
    if (!opt.has_range && !extract_only) {
        bool found = false;
        for (size_t i = 0; i < tokens.n; i++) {
            calendar_token *t = tokens.v + i;
            if (t->year < 0 || t->pair_count != 1)
                continue;
            fpt_calendar_date d = {t->year, t->pairs[0].month, t->pairs[0].day};
            if (!found) {
                opt.start = opt.end = d;
                found = true;
            } else {
                if (date_cmp_value(d, opt.start) < 0)
                    opt.start = d;
                if (date_cmp_value(d, opt.end) > 0)
                    opt.end = d;
            }
        }
        opt.has_range = found && date_cmp_value(opt.start, opt.end) != 0;
    }
    if (tokens.n > SIZE_MAX / sizeof(fpt_parsed_date))
        fail(w, FPT_OUT_OF_MEMORY);
    result->dates = alloc_mem(w, tokens.n * sizeof(fpt_parsed_date));
    memset(result->dates, 0, tokens.n * sizeof(fpt_parsed_date));
    result->count = tokens.n;
    for (size_t i = 0; i < tokens.n; i++) {
        calendar_token *t = tokens.v + i;
        fpt_parsed_date *out = result->dates + i;
        str text = copy_str(w, t->text.p, t->text.n);
        out->text = text.p;
        out->length = text.n;
        if (extract_only)
            continue;
        if (t->year >= 0) {
            for (size_t k = 0; k < t->pair_count; k++)
                add_candidate(w, out,
                              (fpt_calendar_date){t->year, t->pairs[k].month, t->pairs[k].day});
            if (opt.has_range && out->candidate_count > 1) {
                size_t inside = 0;
                for (size_t k = 0; k < out->candidate_count; k++)
                    inside += within(out->candidates[k], opt.start, opt.end);
                if (inside) {
                    size_t n = 0;
                    for (size_t k = 0; k < out->candidate_count; k++)
                        if (within(out->candidates[k], opt.start, opt.end))
                            out->candidates[n++] = out->candidates[k];
                    out->candidate_count = n;
                }
            }
        } else if (opt.has_range) {
            for (int year = opt.start.year; year <= opt.end.year; year++)
                for (size_t k = 0; k < t->pair_count; k++) {
                    fpt_calendar_date d = {year, t->pairs[k].month, t->pairs[k].day};
                    if (valid_calendar(d.year, d.month, d.day) && within(d, opt.start, opt.end))
                        add_candidate(w, out, d);
                }
        }
        if (out->candidate_count > 1)
            qsort(out->candidates, out->candidate_count, sizeof(fpt_calendar_date), date_cmp);
        if (out->candidate_count) {
            out->month = out->candidates[0].month;
            out->day = out->candidates[0].day;
            out->year = out->candidates[0].year;
            for (size_t k = 1; k < out->candidate_count; k++) {
                if (out->month != out->candidates[k].month)
                    out->month = 0;
                if (out->day != out->candidates[k].day)
                    out->day = 0;
                if (out->year != out->candidates[k].year)
                    out->year = 0;
            }
            if (out->candidate_count == 1)
                out->date = out->candidates[0];
        } else {
            out->month = t->pairs[0].month;
            out->day = t->pairs[0].day;
            for (size_t k = 1; k < t->pair_count; k++) {
                if (out->month != t->pairs[k].month)
                    out->month = 0;
                if (out->day != t->pairs[k].day)
                    out->day = 0;
            }
        }
    }
}
static fpt_status calendar_run(fpt_context *ctx, const fpt_text *texts, size_t count,
                               const fpt_calendar_options *options, fpt_calendar_result *out,
                               bool extract_only) {
    if (!ctx || !out || (!texts && count))
        return FPT_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    fpt_calendar_options opt = FPT_CALENDAR_OPTIONS_INIT;
    if (options)
        opt = *options;
    if (opt.date_order < FPT_DATE_ORDER_AUTO || opt.date_order > FPT_DATE_ORDER_DMY)
        return FPT_INVALID_ARGUMENT;
    if (opt.has_range && (!valid_calendar(opt.start.year, opt.start.month, opt.start.day) ||
                          !valid_calendar(opt.end.year, opt.end.month, opt.end.day) ||
                          date_cmp_value(opt.start, opt.end) > 0))
        return FPT_INVALID_ARGUMENT;
    work *w = calloc(1, sizeof(*w));
    if (!w)
        return FPT_OUT_OF_MEMORY;
    w->ctx = ctx;
    int error = setjmp(w->failure);
    if (!error) {
        w->match = pcre2_match_data_create(10, NULL);
        if (!w->match)
            fail(w, FPT_OUT_OF_MEMORY);
        calendar_tokens tokens = scan(w, texts, count, opt.date_order);
        infer(w, tokens, opt, out, extract_only);
    }
    pcre2_match_data_free(w->match);
    out->_storage = w->memory;
    if (error)
        fpt_calendar_result_free(out);
    free(w);
    return (fpt_status)error;
}
fpt_status fpt_extract_date_strings(fpt_context *c, const fpt_text *texts, size_t n,
                                    fpt_calendar_result *out) {
    return calendar_run(c, texts, n, NULL, out, true);
}
fpt_status fpt_parse_date_strings(fpt_context *c, const fpt_text *texts, size_t n,
                                  const fpt_calendar_options *opt, fpt_calendar_result *out) {
    return calendar_run(c, texts, n, opt, out, false);
}
void fpt_calendar_result_free(fpt_calendar_result *r) {
    if (!r)
        return;
    fpt_result owner = {0};
    owner._storage = r->_storage;
    fpt_result_free(&owner);
    memset(r, 0, sizeof(*r));
}
