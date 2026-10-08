#include "internal.h"

typedef struct {
    const char *key;
    fpt_relative_time value;
} kb_entry;
#include "generated/kb.inc"

static int compare_key(str s, const char *key) {
    size_t n = strlen(key), min = s.n < n ? s.n : n;
    int cmp = memcmp(s.p, key, min);
    return cmp ? cmp : s.n < n ? -1 : s.n > n ? 1 : 0;
}
static const kb_entry *lookup(str s) {
    size_t a = 0, b = COUNT(kb);
    while (a < b) {
        size_t m = a + (b - a) / 2;
        int c = compare_key(s, kb[m].key);
        if (c < 0)
            b = m;
        else if (c > 0)
            a = m + 1;
        else
            return kb + m;
    }
    return NULL;
}
static bool keyterm(str s) {
    size_t a = 0, b = COUNT(keyterms);
    while (a < b) {
        size_t m = a + (b - a) / 2;
        int c = compare_key(s, keyterms[m]);
        if (c < 0)
            b = m;
        else if (c > 0)
            a = m + 1;
        else
            return true;
    }
    return false;
}
static bool unit(str s) {
    static const char *const units[] = {
        "second", "seconds", "sec",   "secs", "minute", "minutes", "min",
        "mins",   "hour",    "hours", "hr",   "hrs",    "day",     "days",
        "week",   "weeks",   "wk",    "wks",  "month",  "months",  "mo",
        "mos",    "year",    "years", "yr",   "yrs",    "decade",  "decades"};
    for (size_t i = 0; i < COUNT(units); i++)
        if (eq(s, units[i]))
            return true;
    return false;
}
static bool in_list(str s, const char *const *items, size_t count) {
    for (size_t i = 0; i < count; i++)
        if (eq(s, items[i]))
            return true;
    return false;
}

/* Python float() accepts Unicode decimal digits and underscores between digits.
 */
static bool parse_float(work *w, str s, double *out) {
    s = strip_text(s);
    char *buf = alloc_mem(w, s.n + 1);
    size_t i = 0, n = 0;
    bool prev_digit = false;
    while (i < s.n) {
        uint32_t cp = utf8_next(s, &i);
        int d = decimal_value(cp);
        if (d >= 0) {
            buf[n++] = (char)('0' + d);
            prev_digit = true;
        } else if (cp == '_') {
            size_t next = i;
            if (!prev_digit || next == s.n || decimal_value(utf8_next(s, &next)) < 0)
                return false;
            prev_digit = false;
        } else if (cp < 128) {
            buf[n++] = (char)cp;
            prev_digit = false;
        } else
            return false;
    }
    buf[n] = 0;
    if (!n || memchr(buf, 0, n))
        return false;
    /* strtod also accepts hex floats, which Python float() does not. */
    for (size_t j = 0; j < n; j++)
        if (buf[j] == 'x' || buf[j] == 'X' || buf[j] == 'p' || buf[j] == 'P')
            return false;
    char *end;
    errno = 0;
#ifdef _WIN32
    double value = _strtod_l(buf, &end, w->ctx->number_locale);
#else
    double value = strtod_l(buf, &end, w->ctx->number_locale);
#endif
    if (end != buf + n || end == buf)
        return false;
    *out = value;
    return true;
}
static str float_text(work *w, double value) {
    /* Find the shortest decimal that round-trips, then apply Python's fixed
       versus scientific notation threshold. Formatting uses a private locale. */
    char scientific[64];
    int precision;
    for (precision = 1; precision <= 17; precision++) {
#ifdef _WIN32
        int n = _snprintf_l(scientific, sizeof(scientific), "%.*e", w->ctx->number_locale,
                            precision - 1, value);
#else
        locale_t previous = uselocale(w->ctx->number_locale);
        int n = snprintf(scientific, sizeof(scientific), "%.*e", precision - 1, value);
        uselocale(previous);
#endif
        if (n < 0 || (size_t)n >= sizeof(scientific))
            fail(w, FPT_OVERFLOW);
        double roundtrip;
        if (parse_float(w, (str){scientific, (size_t)n}, &roundtrip) && roundtrip == value)
            break;
    }
    char digits[18], output[64];
    size_t count = 0, length = 0;
    const char *p = scientific;
    while (*p && *p != 'e') {
        if (*p != '.') {
            if (count == 17 || *p < '0' || *p > '9')
                fail(w, FPT_SOURCE_ERROR);
            digits[count++] = *p;
        }
        p++;
    }
    if (!count || *p != 'e')
        fail(w, FPT_SOURCE_ERROR);
    int exponent = atoi(p + 1);
    if (exponent >= -4 && exponent < 16) {
        if (exponent < 0) {
            output[length++] = '0';
            output[length++] = '.';
            for (int i = -1; i > exponent; i--)
                output[length++] = '0';
            memcpy(output + length, digits, count);
            length += count;
        } else {
            size_t whole = (size_t)exponent + 1;
            for (size_t i = 0; i < whole; i++)
                output[i] = i < count ? digits[i] : '0';
            length = whole;
            output[length++] = '.';
            if (whole < count) {
                memcpy(output + length, digits + whole, count - whole);
                length += count - whole;
            } else
                output[length++] = '0';
        }
    } else {
        output[length++] = digits[0];
        if (count > 1) {
            output[length++] = '.';
            memcpy(output + length, digits + 1, count - 1);
            length += count - 1;
        }
        int n = snprintf(output + length, sizeof(output) - length, "e%+03d", exponent);
        if (n < 0 || (size_t)n >= sizeof(output) - length)
            fail(w, FPT_OVERFLOW);
        length += (size_t)n;
    }
    return copy_str(w, output, length);
}
str expand_compact(work *w, str s) {
    words parts = {0};
    size_t pos = 0;
    match m;
    while (regex_find(w, RX_COMPACT, s, pos, &m)) {
        push_word(w, &parts, (str){s.p + pos, m.start[0] - pos});
        str cardinality = group(s, &m, 1), letter = lower_text(w, group(s, &m, 2));
        double value;
        if (!parse_float(w, cardinality, &value) || !isfinite(value))
            fail(w, FPT_OVERFLOW);
        value = trunc(value);
        if (value == 0)
            push_word(w, &parts, group(s, &m, 0));
        else {
            const char *name = eq(letter, "d")                       ? "day"
                               : eq(letter, "w")                     ? "week"
                               : eq(letter, "mo") || eq(letter, "m") ? "month"
                               : eq(letter, "y")                     ? "year"
                               : eq(letter, "h")                     ? "hour"
                               : eq(letter, "min")                   ? "minute"
                               : eq(letter, "s")                     ? "second"
                                                                     : NULL;
            if (!name) {
                push_word(w, &parts, group(s, &m, 0));
                pos = m.end[0];
                continue;
            }
            push_word(w, &parts, cardinality);
            push_word(w, &parts, literal(" "));
            push_word(w, &parts, literal(name));
            if (value > 1)
                push_word(w, &parts, literal("s"));
            str rest = {s.p + m.end[0], s.n - m.end[0]};
            match marker;
            if (!regex_find(w, RX_TENSE_SUFFIX, rest, 0, &marker))
                push_word(w, &parts, literal(" ago"));
        }
        pos = m.end[0];
    }
    push_word(w, &parts, (str){s.p + pos, s.n - pos});
    return join_words(w, parts, "");
}
static int weekday_number(str s) {
    static const char *const names[] = {
        "monday", "mon",   "tuesday", "tue", "tues",     "wednesday", "wed",    "weds", "thursday",
        "thu",    "thurs", "friday",  "fri", "saturday", "sat",       "sunday", "sun"};
    static const int day[] = {0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 5, 5, 6, 6};
    for (size_t i = 0; i < COUNT(names); i++)
        if (eq(s, names[i]))
            return day[i];
    return -1;
}
static words replace_weekdays(work *w, words t) {
    words out = {0};
    static const char *const future[] = {"next", "this", "coming"}, *const past[] = {"last",
                                                                                     "past"};
    for (size_t i = 0; i < t.n; i++) {
        if (i + 1 < t.n) {
            int target = weekday_number(t.v[i + 1]);
            bool f = in_list(t.v[i], future, COUNT(future)), p = in_list(t.v[i], past, COUNT(past));
            if (target >= 0 && (f || p)) {
                int days = (f ? target - w->weekday : w->weekday - target) + 7;
                days %= 7;
                if (!days)
                    days = 7;
                push_word(w, &out, integer_text(w, days));
                push_word(w, &out, literal("days"));
                if (f) {
                    push_word(w, &out, literal("from"));
                    push_word(w, &out, literal("now"));
                } else
                    push_word(w, &out, literal("ago"));
                i++;
                continue;
            }
        }
        push_word(w, &out, t.v[i]);
    }
    return out;
}
static const char *const number_words[] = {
    "zero",     "one",      "two",     "three",     "four",     "five",     "six",
    "seven",    "eight",    "nine",    "ten",       "eleven",   "twelve",   "thirteen",
    "fourteen", "fifteen",  "sixteen", "seventeen", "eighteen", "nineteen", "twenty",
    "thirty",   "forty",    "fifty",   "sixty",     "seventy",  "eighty",   "ninety",
    "hundred",  "thousand", "million", "billion",   "point"};
static const int number_values[] = {0,  1,  2,  3,  4,  5,  6,   7,    8,       9,          10,
                                    11, 12, 13, 14, 15, 16, 17,  18,   19,      20,         30,
                                    40, 50, 60, 70, 80, 90, 100, 1000, 1000000, 1000000000, -1};
static int number_index(str s) {
    for (size_t i = 0; i < COUNT(number_words); i++)
        if (eq(s, number_words[i]))
            return (int)i;
    return -1;
}
static double formation(work *w, const int *v, size_t n) {
    if (!n)
        fail(w, FPT_SOURCE_ERROR);
    if (n == 4)
        return (double)v[0] * v[1] + v[2] + v[3];
    if (n == 3)
        return (double)v[0] * v[1] + v[2];
    if (n == 2)
        return v[0] == 100 || v[1] == 100 ? (double)v[0] * v[1] : (double)v[0] + v[1];
    return v[0];
}
static str canonical_digits(work *w, str s) {
    char *p = alloc_mem(w, s.n + 1);
    size_t i = 0, n = 0;
    while (i < s.n) {
        int d = decimal_value(utf8_next(s, &i));
        if (d < 0)
            return s;
        p[n++] = (char)('0' + d);
    }
    p[n] = 0;
    size_t start = 0;
    while (start + 1 < n && p[start] == '0')
        start++;
    return (str){p + start, n - start};
}
static str word_to_number(work *w, str original) {
    str s = lower_text(w, replace_text(w, original, literal("-"), literal(" ")));
    if (digit_string(s))
        return canonical_digits(w, s);
    words words_in = split_words(w, s);
    int *values = alloc_mem(w, (words_in.n + 1) * sizeof(int));
    size_t n = 0;
    int counts[4] = {0};
    for (size_t i = 0; i < words_in.n; i++) {
        int idx = number_index(words_in.v[i]);
        if (idx >= 0) {
            values[n++] = number_values[idx];
            if (idx >= 29)
                counts[idx - 29]++;
        }
    }
    if (!n)
        return original;
    for (int i = 0; i < 4; i++)
        if (counts[i] > 1)
            return original;
    size_t end = n, decimal_start = n;
    for (size_t i = 0; i < n; i++)
        if (values[i] == -1) {
            end = i;
            decimal_start = i + 1;
            break;
        }
    ptrdiff_t bi = -1, mi = -1, ti = -1;
    for (size_t i = 0; i < end; i++) {
        if (values[i] == 1000000000)
            bi = (ptrdiff_t)i;
        if (values[i] == 1000000)
            mi = (ptrdiff_t)i;
        if (values[i] == 1000)
            ti = (ptrdiff_t)i;
    }
    if ((ti >= 0 && (ti < mi || ti < bi)) || (mi >= 0 && mi < bi))
        return original;
    double total = 0;
    if (end == 1)
        total = values[0];
    else if (end > 1) {
        if (bi >= 0)
            total += formation(w, values, (size_t)bi) * 1000000000.0;
        if (mi >= 0) {
            size_t start = bi >= 0 ? (size_t)bi + 1 : 0;
            total += formation(w, values + start, (size_t)mi - start) * 1000000.0;
        }
        if (ti >= 0) {
            size_t start = mi >= 0 ? (size_t)mi + 1 : bi >= 0 ? (size_t)bi + 1 : 0;
            total += formation(w, values + start, (size_t)ti - start) * 1000.0;
        }
        if (ti >= 0 && (size_t)ti != end - 1)
            total += formation(w, values + ti + 1, end - (size_t)ti - 1);
        else if (mi >= 0 && (size_t)mi != end - 1)
            total += formation(w, values + mi + 1, end - (size_t)mi - 1);
        else if (bi >= 0 && (size_t)bi != end - 1)
            total += formation(w, values + bi + 1, end - (size_t)bi - 1);
        else if (ti < 0 && mi < 0 && bi < 0)
            total += formation(w, values, end);
    }
    bool has_decimal = decimal_start < n;
    if (has_decimal) {
        char *decimal = alloc_mem(w, n - decimal_start + 3);
        decimal[0] = '0';
        decimal[1] = '.';
        size_t length = 2;
        for (size_t i = decimal_start; i < n; i++) {
            if (values[i] < 0 || values[i] > 9) {
                has_decimal = false;
                break;
            }
            decimal[length++] = (char)('0' + values[i]);
        }
        if (has_decimal) {
            double fraction;
            if (!parse_float(w, (str){decimal, length}, &fraction))
                fail(w, FPT_SOURCE_ERROR);
            total += fraction;
        }
    }
    if (!has_decimal)
        return integer_text(w, total);
    return float_text(w, total);
}
words replace_digits(work *w, words tokens) {
    words trimmed = {0}, removed = {0};
    for (size_t i = 0; i < tokens.n; i++) {
        str t = tokens.v[i];
        while (t.n && t.p[t.n - 1] == ',')
            t.n--;
        push_word(w, &trimmed, t);
    }
    for (size_t i = 0; i < trimmed.n; i++) {
        if (eq(trimmed.v[i], "and") && i >= 2 && i + 1 < trimmed.n && unit(trimmed.v[i - 1]) &&
            numeric_token(trimmed.v[i - 2]) && numeric_token(trimmed.v[i + 1]))
            continue;
        push_word(w, &removed, trimmed.v[i]);
    }
    str s = join_words(w, removed, " ");
    static const char *const phrases[][2] = {{"the day before yesterday", "2 days ago"},
                                             {"day before yesterday", "2 days ago"},
                                             {"the day after tomorrow", "2 days from now"},
                                             {"day after tomorrow", "2 days from now"},
                                             {"overmorrow", "2 days from now"},
                                             {"till date", "today"},
                                             {"to date", "today"},
                                             {"half an hour", "30 minutes"},
                                             {"half a day", "12 hours"}};
    for (size_t i = 0; i < COUNT(phrases); i++)
        s = replace_text(w, s, literal(phrases[i][0]), literal(phrases[i][1]));
    tokens = split_words(w, s);
    for (size_t i = 0; i < tokens.n; i++)
        if (memchr(tokens.v[i].p, '.', tokens.v[i].n)) {
            double v;
            if (parse_float(w, tokens.v[i], &v)) {
                if (isinf(v))
                    fail(w, FPT_OVERFLOW);
                if (!isnan(v))
                    tokens.v[i] = integer_text(w, v);
            }
        }
    tokens = replace_weekdays(w, tokens);
    for (size_t i = 0; i < tokens.n; i++) {
        str t = tokens.v[i];
        tokens.v[i] = eq(t, "a") || eq(t, "an")          ? literal("1")
                      : eq(t, "several") || eq(t, "few") ? literal("3")
                                                         : word_to_number(w, t);
    }
    for (size_t i = 1; i < tokens.n; i++)
        if ((eq(tokens.v[i], "min") || eq(tokens.v[i], "hr") || eq(tokens.v[i], "hour")) &&
            digit_string(tokens.v[i - 1])) {
            int64_t n;
            if (!integer(tokens.v[i - 1], &n))
                fail(w, FPT_SOURCE_ERROR);
            if (n > 1) {
                str t = tokens.v[i];
                if (eq(t, "min"))
                    tokens.v[i] = literal("mins");
                else if (eq(t, "hr"))
                    tokens.v[i] = literal("hrs");
                else if (eq(t, "hour"))
                    tokens.v[i] = literal("hours");
            }
        }
    return tokens;
}
static const kb_entry *solve(work *w, words seq) {
    const kb_entry *entry = lookup(join_words(w, seq, " "));
    if (entry || seq.n <= 1)
        return entry;
    entry = lookup(join_words(w, (words){seq.v + 1, seq.n - 1}, " "));
    if (entry)
        return entry;
    return lookup(join_words(w, (words){seq.v, seq.n - 1}, " "));
}
static void compound(work *w, words seq, fpt_result *out) {
    if (seq.n < 4)
        return;
    words body = seq;
    const char *suffix = "ago";
    bool future = false;
    str last = seq.v[seq.n - 1];
    if (eq(last, "ago") || eq(last, "back") || eq(last, "before")) {
        suffix = eq(last, "ago") ? "ago" : eq(last, "back") ? "back" : "before";
        body.n--;
    } else if (seq.n >= 2 && eq(seq.v[seq.n - 2], "from") &&
               (eq(last, "now") || eq(last, "today"))) {
        future = true;
        body.n -= 2;
    } else if (eq(seq.v[0], "in")) {
        future = true;
        body.v++;
        body.n--;
    }
    words clean = {0};
    for (size_t i = 0; i < body.n; i++)
        if (!eq(body.v[i], "and"))
            push_word(w, &clean, body.v[i]);
    size_t pairs = 0;
    for (size_t i = 0; i + 1 < clean.n; i++)
        if (numeric_token(clean.v[i]) && unit(clean.v[i + 1])) {
            pairs++;
            i++;
        }
    if (pairs < 2)
        return;
    for (size_t i = 0; i + 1 < clean.n; i++)
        if (numeric_token(clean.v[i]) && unit(clean.v[i + 1])) {
            str items[] = {clean.v[i], clean.v[i + 1], literal(future ? "from" : suffix),
                           literal("now")};
            const kb_entry *entry = solve(w, (words){items, future ? 4 : 3});
            if (entry && entry->value.frame)
                add_relative(w, out, entry->value);
            i++;
        }
}
void extract_relative(work *w, str s) {
    s = expand_compact(w, s);
    s = lower_text(w, s);
    words tokens = replace_digits(w, split_words(w, s));
    fpt_result compounds = {0};
    size_t start = 0;
    for (size_t i = 0; i <= tokens.n; i++)
        if (i == tokens.n || !keyterm(tokens.v[i])) {
            if (i > start) {
                words seq = {tokens.v + start, i - start};
                const kb_entry *entry = solve(w, seq);
                if (entry && entry->value.frame)
                    add_relative(w, &w->result, entry->value);
                compound(w, seq, &compounds);
            }
            start = i + 1;
        }
    if (compounds.relative_count > w->result.relative_count) {
        w->result.relative_times = compounds.relative_times;
        w->result.relative_count = compounds.relative_count;
    }
}
