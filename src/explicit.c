#include "internal.h"

static const char *const months[] = {
    "january", "february", "march",    "april", "may", "june", "july", "august", "september",
    "october", "november", "december", "jan",   "feb", "mar",  "apr",  "may",    "jun",
    "jul",     "aug",      "sep",      "sept",  "oct", "nov",  "dec"};
static int month_number(str s) {
    for (size_t i = 0; i < 12; i++)
        if (eq(s, months[i]))
            return (int)i + 1;
    static const char *const ab[] = {"jan", "feb", "mar", "apr", "may", "jun",
                                     "jul", "aug", "sep", "oct", "nov", "dec"};
    for (size_t i = 0; i < 12; i++)
        if (eq(s, ab[i]))
            return (int)i + 1;
    return eq(s, "sept") ? 9 : 0;
}
static bool contains(str s, str part) {
    if (part.n > s.n)
        return false;
    for (size_t i = 0; i <= s.n - part.n; i++)
        if (!memcmp(s.p + i, part.p, part.n))
            return true;
    return false;
}
static bool month_anywhere(work *w, str s) {
    s = lower_text(w, s);
    for (size_t i = 0; i < COUNT(months); i++)
        if (contains(s, literal(months[i])))
            return true;
    return false;
}
static bool has_month_name(work *w, str s) {
    s = lower_text(w, s);
    size_t i = 0;
    while (i < s.n) {
        while (i < s.n && !((s.p[i] >= 'a' && s.p[i] <= 'z') || (s.p[i] >= 'A' && s.p[i] <= 'Z')))
            i++;
        size_t start = i;
        while (i < s.n && ((s.p[i] >= 'a' && s.p[i] <= 'z') || (s.p[i] >= 'A' && s.p[i] <= 'Z')))
            i++;
        if (i > start && month_number((str){s.p + start, i - start}))
            return true;
    }
    return false;
}
static int64_t num(str s) {
    int64_t n = 0;
    (void)integer(s, &n);
    return n;
}
static size_t char_count(str s) {
    size_t i = 0, n = 0;
    while (i < s.n) {
        utf8_next(s, &i);
        n++;
    }
    return n;
}
static str last_chars(str s, size_t n) {
    size_t count = char_count(s), i = 0;
    for (size_t k = n; k < count; k++)
        utf8_next(s, &i);
    return (str){s.p + i, s.n - i};
}
static str first_chars(str s, size_t n) {
    size_t i = 0;
    while (n-- && i < s.n)
        utf8_next(s, &i);
    return (str){s.p, i};
}
static words split_char(work *w, str s, char ch) {
    words v = {0};
    size_t start = 0;
    for (size_t i = 0; i < s.n; i++)
        if (s.p[i] == ch) {
            push_word(w, &v, copy_str(w, s.p + start, i - start));
            start = i + 1;
        }
    push_word(w, &v, copy_str(w, s.p + start, s.n - start));
    return v;
}
static size_t count_char(str s, char ch) {
    size_t n = 0;
    for (size_t i = 0; i < s.n; i++)
        n += s.p[i] == ch;
    return n;
}
static str substitute_groups(work *w, str s, int id, const char *middle) {
    words parts = {0};
    size_t offset = 0;
    match m;
    while (regex_find(w, id, s, offset, &m)) {
        push_word(w, &parts, (str){s.p + offset, m.start[0] - offset});
        push_word(w, &parts, group(s, &m, 1));
        if (middle) {
            push_word(w, &parts, literal(middle));
            push_word(w, &parts, group(s, &m, 2));
        }
        offset = m.end[0];
    }
    push_word(w, &parts, (str){s.p + offset, s.n - offset});
    return join_words(w, parts, "");
}
str normalize(work *w, str s) {
    words parts = {0};
    size_t i = 0, start = 0;
    while (i < s.n) {
        size_t before = i;
        uint32_t cp = utf8_next(s, &i);
        if ((cp >= 0x2010 && cp <= 0x2015) || cp == 0x2212 || cp == 0xfe63 || cp == 0xff0d) {
            push_word(w, &parts, (str){s.p + start, before - start});
            push_word(w, &parts, literal("-"));
            start = i;
        }
    }
    push_word(w, &parts, (str){s.p + start, s.n - start});
    s = join_words(w, parts, "");
    return substitute_groups(w, s, RX_SPACED_HYPHEN, "-");
}
static str strip_ordinal(work *w, str s) {
    s = substitute_groups(w, s, RX_STRIP_ORDINAL, NULL);
    return replace_text(w, s, literal(","), literal(""));
}
bool validate_date(work *w, str s) {
    s = strip_text(s);
    if (!s.n)
        return false;
    /* Match the validator's exact, first-occurrence Sept alias substitution. */
    str lower = lower_text(w, s);
    words tokens = split_words(w, lower);
    bool alias = false, period = false;
    for (size_t i = 0; i < tokens.n; i++) {
        if (eq(tokens.v[i], "sept"))
            alias = true;
        if (eq(tokens.v[i], "sept.")) {
            alias = true;
            period = true;
        }
    }
    if (alias) {
        size_t i = 0;
        while (i + 4 <= lower.n && memcmp(lower.p + i, "sept", 4))
            i++;
        if (i + 4 <= lower.n) {
            words v = {0};
            push_word(w, &v, (str){s.p, i});
            push_word(w, &v, literal(period ? "sep." : "sep"));
            size_t end = i + 4 + (period ? 1 : 0);
            if (end > s.n)
                end = s.n;
            push_word(w, &v, (str){s.p + end, s.n - end});
            s = join_words(w, v, "");
        }
    }
    for (int k = 0; k < VALIDATION_FORMAT_COUNT; k++) {
        int id = RX_VALIDATE_0 + k;
        match m;
        if (!regex_find(w, id, s, 0, &m))
            continue;
        int y = 1900, mo = 1, d = 1;
        int gn = group_number(w, id, "Y");
        if (gn > 0)
            y = (int)num(group(s, &m, gn));
        gn = group_number(w, id, "y");
        if (gn > 0) {
            y = (int)num(group(s, &m, gn));
            y += y <= 68 ? 2000 : 1900;
        }
        gn = group_number(w, id, "m");
        if (gn > 0)
            mo = (int)num(group(s, &m, gn));
        gn = group_number(w, id, "d");
        if (gn > 0)
            d = (int)num(group(s, &m, gn));
        gn = group_number(w, id, "B");
        if (gn < 0)
            gn = group_number(w, id, "b");
        if (gn > 0)
            mo = month_number(lower_text(w, group(s, &m, gn)));
        if (valid_calendar(y, mo, d))
            return true;
    }
    const char *delims = "/-.";
    for (size_t k = 0; k < 3; k++)
        if (count_char(s, delims[k]) == 1) {
            words v = split_char(w, s, delims[k]);
            int64_t a, b;
            if (integer(v.v[0], &a) && integer(v.v[1], &b) && a >= 1 && a <= 31 && b >= 1 &&
                b <= 31)
                return true;
        }
    return false;
}
static bool valid_component(work *w, str s) {
    int64_t n;
    return integer(s, &n) && ((n >= 1 && n <= 31) || year_valid(w, n));
}
words tokenize_numeric(work *w, str s) {
    words tokens = split_words(w, s), out = {0};
    const char *delims = "/.-";
    for (size_t i = 0; i < tokens.n; i++) {
        str t = tokens.v[i];
        bool duplicate = false;
        for (size_t j = 0; j < out.n; j++)
            if (equal(t, out.v[j])) {
                duplicate = true;
                break;
            }
        if (duplicate)
            continue;
        bool valid = valid_component(w, t);
        for (size_t k = 0; k < 3 && !valid; k++) {
            size_t n = count_char(t, delims[k]);
            if (!n || (n == 1 && delims[k] != '/'))
                continue;
            words v = split_char(w, t, delims[k]);
            bool all = true;
            for (size_t j = 0; j < v.n; j++)
                if (!valid_component(w, v.v[j])) {
                    all = false;
                    break;
                }
            valid = all;
        }
        if (valid)
            push_word(w, &out, t);
    }
    return out;
}
static bool numeric_string(str s) {
    size_t i = 0;
    if (!s.n)
        return false;
    while (i < s.n)
        if (!is_numeric(utf8_next(s, &i)))
            return false;
    return true;
}
bool preclassify_numeric(work *w, str s) {
    words t = split_words(w, s);
    const char *delims = "/.-";
    for (size_t i = 0; i < t.n; i++) {
        if (numeric_string(t.v[i]))
            return true;
        for (size_t k = 0; k < 3; k++)
            if (count_char(t.v[i], delims[k])) {
                words v = split_char(w, t.v[i], delims[k]);
                for (size_t j = 0; j < v.n; j++)
                    if (numeric_string(v.v[j]))
                        return true;
            }
    }
    return false;
}
static void numeric_dates(work *w, str s) {
    if (!preclassify_numeric(w, s))
        return;
    words tokens = tokenize_numeric(w, s);
    for (size_t i = 0; i < tokens.n; i++) {
        str t = tokens.v[i];
        const char *delims = "/.-";
        char delim = 0;
        int nd = 0;
        for (size_t k = 0; k < 3; k++)
            if (count_char(t, delims[k])) {
                delim = delims[k];
                nd++;
            }
        if (nd > 1)
            fail(w, FPT_SOURCE_ERROR);
        if (!nd)
            continue;
        words parts = split_char(w, t, delim);
        fpt_date_type type = 0;
        if (parts.n <= 3 && parts.n >= 2 && validate_date(w, t)) {
            if (parts.n == 3)
                type = FPT_FULL_EXPLICIT_DATE;
            else {
                int64_t a = num(parts.v[0]), b = num(parts.v[1]);
                if (year_valid(w, a) && b >= 1 && b <= 12)
                    type = FPT_YEAR_MONTH;
                else if (year_valid(w, b) && a >= 1 && a <= 12)
                    type = FPT_MONTH_YEAR;
                else if (a >= 1 && a <= 12 && b >= 1 && b <= 12)
                    type = FPT_DAY_MONTH_AMBIGUOUS;
                else if (a >= 13 && a <= 31 && b >= 1 && b <= 12) {
                    if (valid_calendar(2000, (int)b, (int)a))
                        type = FPT_DAY_MONTH;
                } else if (b >= 13 && b <= 31 && a >= 1 && a <= 12) {
                    if (valid_calendar(2000, (int)a, (int)b))
                        type = FPT_MONTH_DAY;
                } else
                    fail(w, FPT_SOURCE_ERROR);
            }
        }
        if (!type && delim == '-' && parts.n == 2) {
            int64_t a = num(parts.v[0]), b = num(parts.v[1]);
            if (year_valid(w, a) && year_valid(w, b) && a < b)
                type = FPT_YEAR_RANGE;
        }
        if (type)
            add_date(w, t, type, true);
    }
}
fpt_date_type classify_delimited(work *w, str s, char delimiter) {
    size_t count = count_char(s, delimiter);
    if (count == 0 || count >= 3 || !validate_date(w, s))
        return 0;
    if (count == 2)
        return FPT_FULL_EXPLICIT_DATE;
    words parts = split_char(w, s, delimiter);
    int types[2] = {0};
    size_t total = 0;
    for (size_t i = 0; i < parts.n; i++) {
        int64_t value;
        if (!integer(parts.v[i], &value))
            continue;
        int t = year_valid(w, value)         ? 1
                : value >= 13 && value <= 31 ? 2
                : value >= 1 && value <= 12  ? 3
                                             : 0;
        if (t)
            types[total++] = t;
    }
    if (total == 0)
        return 0;
    if (total == 1) {
        if (types[0] == 1)
            return FPT_YEAR_ONLY;
        fail(w, FPT_SOURCE_ERROR);
    }
    if (types[0] == 1 && types[1] == 3)
        return FPT_YEAR_MONTH;
    if (types[0] == 3 && types[1] == 1)
        return FPT_MONTH_YEAR;
    if (types[0] == 2 && types[1] == 3)
        return FPT_DAY_MONTH;
    if (types[0] == 3 && types[1] == 2)
        return FPT_MONTH_DAY;
    if (types[0] == 3 && types[1] == 3)
        return FPT_DAY_MONTH_AMBIGUOUS;
    fail(w, FPT_SOURCE_ERROR);
    return 0;
}
static void written_dates(work *w, str s) {
    if (!has_month_name(w, s))
        return;
    bool found = false;
    for (int id = RX_WRITTEN1; id <= RX_WRITTEN2; id++) {
        size_t pos = 0;
        match m;
        while (regex_find(w, id, s, pos, &m)) {
            str date = group(s, &m, 0);
            pos = m.end[0];
            if (validate_date(w, strip_ordinal(w, date))) {
                add_date(w, date, FPT_FULL_EXPLICIT_DATE, true);
                found = true;
            }
        }
    }
    if (found || !validate_date(w, strip_ordinal(w, s)))
        return;
    words t = split_words(w, s);
    bool has_year = false, has_day = false;
    for (size_t i = 0; i < t.n; i++) {
        str v = replace_text(w, replace_text(w, t.v[i], literal(","), literal("")), literal("."),
                             literal(""));
        if (char_count(v) == 4 && digit_string(v))
            has_year = true;
        match m;
        if (regex_find(w, RX_DAY_TOKEN, v, 0, &m))
            has_day = true;
    }
    if (has_year || has_day)
        add_date(w, s,
                 has_year ? (has_day ? FPT_FULL_EXPLICIT_DATE : FPT_MONTH_YEAR) : FPT_DAY_MONTH,
                 true);
}
static void hyphen_month(work *w, str s) {
    for (int id = RX_HYPHEN_FORWARD; id <= RX_HYPHEN_REVERSE; id++) {
        size_t pos = 0;
        match m;
        while (regex_find(w, id, s, pos, &m)) {
            pos = m.end[0];
            str year = group(s, &m, id == RX_HYPHEN_FORWARD ? 2 : 1);
            if (char_count(year) == 2 || year_valid(w, num(year)))
                add_date(w, group(s, &m, 0),
                         id == RX_HYPHEN_FORWARD ? FPT_MONTH_YEAR : FPT_YEAR_MONTH, true);
        }
    }
}
static void prose_year(work *w, str s) {
    /* Prose-year suppression sees this extractor's ranges only. */
    words ranges = {0};
    for (int id = RX_RANGE_HYPHEN; id <= RX_RANGE_ABBREV; id++) {
        size_t pos = 0;
        match m;
        while (regex_find(w, id, s, pos, &m)) {
            pos = m.end[0];
            str ya = group(s, &m, 1), yb = group(s, &m, 2);
            int64_t a = num(ya), b = num(yb);
            str key = group(s, &m, 0);
            if (id == RX_RANGE_ABBREV) {
                if (b >= 1 && b <= 12)
                    continue;
                if (month_anywhere(w, last_chars((str){s.p, m.start[0]}, 20)) &&
                    month_anywhere(w, first_chars((str){s.p + m.end[0], s.n - m.end[0]}, 20)))
                    continue;
                b = (a / 100) * 100 + b;
                if (b <= a)
                    b += 100;
            } else if (id != RX_RANGE_HYPHEN) {
                words v = {0};
                push_word(w, &v, ya);
                push_word(w, &v, yb);
                key = join_words(w, v, "-");
            }
            if (year_valid(w, a) && year_valid(w, b) && a < b) {
                add_date(w, key, FPT_YEAR_RANGE, true);
                push_word(w, &ranges, key);
            }
        }
    }
    size_t pos = 0;
    match m;
    while (regex_find(w, RX_PROSE_YEAR, s, pos, &m)) {
        pos = m.end[0];
        str y = group(s, &m, 1);
        if (!year_valid(w, num(y)))
            continue;
        bool inside = false;
        for (size_t i = 0; i < ranges.n; i++)
            if (contains(ranges.v[i], y)) {
                inside = true;
                break;
            }
        if (!inside)
            add_date(w, y, FPT_YEAR_ONLY, true);
    }
}
static void iso_dates(work *w, str s) {
    size_t pos = 0;
    match m;
    while (regex_find(w, RX_ISO, s, pos, &m)) {
        pos = m.end[0];
        add_date(w, group(s, &m, 1), FPT_FULL_EXPLICIT_DATE, true);
    }
}
static void ordinal_dates(work *w, str s) {
    for (int id = RX_ORDINAL1; id <= RX_ORDINAL4; id++) {
        size_t pos = 0;
        match m;
        while (regex_find(w, id, s, pos, &m)) {
            pos = m.end[0];
            int64_t d = num(group(s, &m, id == RX_ORDINAL3 ? 2 : 1));
            if (d < 1 || d > 31)
                continue;
            str year = group(s, &m, 3);
            bool good = year.n && year_valid(w, num(year));
            if (id == RX_ORDINAL1 && !good)
                continue;
            add_date(w, group(s, &m, 0), good ? FPT_FULL_EXPLICIT_DATE : FPT_DAY_MONTH, true);
        }
    }
}
static void space_month(work *w, str s, bool overwrite) {
    /* Standalone extractor uses dict last-value-wins; integration preserves
     * earlier stages. */
    fpt_result saved = w->result;
    w->result = (fpt_result){0};
    size_t pos = 0;
    match m;
    while (regex_find(w, RX_SPACE_MONTH, s, pos, &m)) {
        pos = m.end[0];
        str prep = lower_text(w, group(s, &m, 1)), month = group(s, &m, 2),
            number = group(s, &m, 3);
        words v = {0};
        push_word(w, &v, month);
        push_word(w, &v, number);
        str key = join_words(w, v, " ");
        int64_t n = num(number);
        fpt_date_type type = n > 31 || eq(prep, "in") ? FPT_MONTH_YEAR
                             : eq(prep, "on")         ? FPT_DAY_MONTH
                                                      : FPT_DAY_MONTH_AMBIGUOUS;
        add_date(w, key, type, true);
    }
    fpt_result local = w->result;
    w->result = saved;
    for (size_t i = 0; i < local.explicit_count; i++)
        add_date(w, (str){local.explicit_dates[i].text, local.explicit_dates[i].length},
                 local.explicit_dates[i].date_type, overwrite);
}
void extract_explicit(work *w, str s, fpt_operation op) {
    bool all = op == FPT_OP_PARSE || op == FPT_OP_EXPLICIT;
    if (all)
        s = normalize(w, s);
    if (all || op == FPT_OP_NUMERIC)
        numeric_dates(w, s);
    if (all || op == FPT_OP_WRITTEN)
        written_dates(w, s);
    if (all || op == FPT_OP_HYPHEN_MONTH_YEAR)
        hyphen_month(w, s);
    if (all || op == FPT_OP_PROSE_YEAR)
        prose_year(w, s);
    if (all || op == FPT_OP_ISO8601)
        iso_dates(w, s);
    if (all || op == FPT_OP_ORDINAL)
        ordinal_dates(w, s);
    if (all || op == FPT_OP_SPACE_MONTH_NUMBER)
        space_month(w, s, !all);
}
