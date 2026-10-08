#include "internal.h"
typedef struct {
    uint32_t first, last;
} urange;
typedef struct {
    uint32_t from, to[3];
} ulower;
typedef struct {
    uint32_t cp;
    int value;
} udigit;
#include "generated/unicode.inc"

_Noreturn void fail(work *w, fpt_status status) { longjmp(w->failure, (int)status); }
void *alloc_mem(work *w, size_t n) {
    if (n > SIZE_MAX - sizeof(allocation))
        fail(w, FPT_OUT_OF_MEMORY);
    allocation *a = malloc(sizeof(*a) + (n ? n : 1));
    if (!a)
        fail(w, FPT_OUT_OF_MEMORY);
    a->next = w->memory;
    w->memory = a;
    return a->data;
}
str literal(const char *s) { return (str){s, strlen(s)}; }
str copy_str(work *w, const char *s, size_t n) {
    if (n == SIZE_MAX)
        fail(w, FPT_OUT_OF_MEMORY);
    char *p = alloc_mem(w, n + 1);
    if (n)
        memcpy(p, s, n);
    p[n] = 0;
    return (str){p, n};
}
bool eq(str a, const char *b) { return a.n == strlen(b) && !memcmp(a.p, b, a.n); }
bool equal(str a, str b) { return a.n == b.n && !memcmp(a.p, b.p, a.n); }
void push_word(work *w, words *v, str a) {
    if (v->n == SIZE_MAX / sizeof(str))
        fail(w, FPT_OUT_OF_MEMORY);
    /* Geometric allocation; unused old buffers are reclaimed with the work arena.
     */
    if ((v->n & (v->n - 1)) == 0) {
        size_t cap = v->n ? v->n * 2 : 1;
        if (cap > SIZE_MAX / sizeof(str))
            fail(w, FPT_OUT_OF_MEMORY);
        str *p = alloc_mem(w, cap * sizeof(str));
        if (v->n)
            memcpy(p, v->v, v->n * sizeof(str));
        v->v = p;
    }
    v->v[v->n++] = a;
}
str join_words(work *w, words v, const char *sep) {
    size_t n = 0, sn = strlen(sep);
    for (size_t i = 0; i < v.n; i++) {
        if (v.v[i].n > SIZE_MAX - n - sn - 1)
            fail(w, FPT_OUT_OF_MEMORY);
        n += v.v[i].n + (i ? sn : 0);
    }
    char *p = alloc_mem(w, n + 1), *q = p;
    for (size_t i = 0; i < v.n; i++) {
        if (i) {
            memcpy(q, sep, sn);
            q += sn;
        }
        memcpy(q, v.v[i].p, v.v[i].n);
        q += v.v[i].n;
    }
    *q = 0;
    return (str){p, n};
}
uint32_t utf8_next(str s, size_t *i) {
    unsigned char c = (unsigned char)s.p[(*i)++];
    if (c < 128)
        return c;
    int n = c < 224 ? 1 : c < 240 ? 2 : 3;
    uint32_t cp = c & (unsigned)(0x7f >> n);
    while (n-- && *i < s.n)
        cp = (cp << 6) | ((unsigned char)s.p[(*i)++] & 63u);
    return cp;
}
static bool in_ranges(uint32_t cp, const urange *r, size_t n) {
    size_t a = 0, b = n;
    while (a < b) {
        size_t m = a + (b - a) / 2;
        if (cp < r[m].first)
            b = m;
        else if (cp > r[m].last)
            a = m + 1;
        else
            return true;
    }
    return false;
}
bool is_space(uint32_t cp) { return in_ranges(cp, u_space, COUNT(u_space)); }
bool is_digit(uint32_t cp) { return in_ranges(cp, u_digit, COUNT(u_digit)); }
bool is_numeric(uint32_t cp) { return in_ranges(cp, u_numeric, COUNT(u_numeric)); }
int decimal_value(uint32_t cp) {
    size_t a = 0, b = COUNT(u_digits);
    while (a < b) {
        size_t m = a + (b - a) / 2;
        if (cp < u_digits[m].cp)
            b = m;
        else if (cp > u_digits[m].cp)
            a = m + 1;
        else
            return u_digits[m].value;
    }
    return -1;
}
words split_words(work *w, str s) {
    words v = {0};
    size_t i = 0, start = 0;
    while (i < s.n) {
        size_t pos = i;
        uint32_t cp = utf8_next(s, &i);
        if (is_space(cp)) {
            if (pos > start)
                push_word(w, &v, copy_str(w, s.p + start, pos - start));
            start = i;
        }
    }
    if (i > start)
        push_word(w, &v, copy_str(w, s.p + start, i - start));
    return v;
}
str strip_text(str s) {
    size_t i = 0, first = s.n, last = 0;
    while (i < s.n) {
        size_t p = i;
        if (!is_space(utf8_next(s, &i))) {
            if (first == s.n)
                first = p;
            last = i;
        }
    }
    return first == s.n ? (str){s.p, 0} : (str){s.p + first, last - first};
}
static size_t encode(char *p, uint32_t cp) {
    if (cp < 0x80) {
        p[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        p[0] = (char)(0xc0 | (cp >> 6));
        p[1] = (char)(0x80 | (cp & 63));
        return 2;
    }
    if (cp < 0x10000) {
        p[0] = (char)(0xe0 | (cp >> 12));
        p[1] = (char)(0x80 | ((cp >> 6) & 63));
        p[2] = (char)(0x80 | (cp & 63));
        return 3;
    }
    p[0] = (char)(0xf0 | (cp >> 18));
    p[1] = (char)(0x80 | ((cp >> 12) & 63));
    p[2] = (char)(0x80 | ((cp >> 6) & 63));
    p[3] = (char)(0x80 | (cp & 63));
    return 4;
}
str lower_text(work *w, str s) {
    if (s.n > (SIZE_MAX - 1) / 3)
        fail(w, FPT_OUT_OF_MEMORY);
    char *p = alloc_mem(w, s.n * 3 + 1);
    size_t i = 0, n = 0;
    while (i < s.n) {
        uint32_t cp = utf8_next(s, &i);
        size_t a = 0, b = COUNT(u_lower);
        const ulower *found = NULL;
        while (a < b) {
            size_t m = a + (b - a) / 2;
            if (cp < u_lower[m].from)
                b = m;
            else if (cp > u_lower[m].from)
                a = m + 1;
            else {
                found = u_lower + m;
                break;
            }
        }
        if (found) {
            for (size_t j = 0; found->to[j]; j++)
                n += encode(p + n, found->to[j]);
        } else
            n += encode(p + n, cp);
    }
    p[n] = 0;
    return (str){p, n};
}
str replace_text(work *w, str s, str old, str replacement) {
    if (!old.n)
        return s;
    words v = {0};
    size_t start = 0, i = 0;
    while (i + old.n <= s.n) {
        if (!memcmp(s.p + i, old.p, old.n)) {
            push_word(w, &v, (str){s.p + start, i - start});
            push_word(w, &v, replacement);
            i += old.n;
            start = i;
        } else
            i++;
    }
    push_word(w, &v, (str){s.p + start, s.n - start});
    return join_words(w, v, "");
}
bool integer(str s, int64_t *out) {
    s = strip_text(s);
    size_t i = 0;
    bool neg = false, have = false, last_digit = false;
    uint64_t n = 0;
    if (s.n && (s.p[0] == '+' || s.p[0] == '-')) {
        neg = s.p[0] == '-';
        i++;
    }
    while (i < s.n) {
        uint32_t cp = utf8_next(s, &i);
        if (cp == '_' && last_digit) {
            last_digit = false;
            continue;
        }
        int d = decimal_value(cp);
        if (d < 0)
            return false;
        have = last_digit = true;
        if (n > ((uint64_t)INT64_MAX - (unsigned)d) / 10)
            n = INT64_MAX;
        else
            n = n * 10 + (unsigned)d;
    }
    if (!have || !last_digit)
        return false;
    *out = neg ? -(int64_t)n : (int64_t)n;
    return true;
}
bool digit_string(str s) {
    size_t i = 0;
    if (!s.n)
        return false;
    while (i < s.n)
        if (!is_digit(utf8_next(s, &i)))
            return false;
    return true;
}
bool numeric_token(str s) {
    size_t i = 0;
    int dots = 0, nd = 0;
    while (i < s.n) {
        uint32_t cp = utf8_next(s, &i);
        if (cp == '.' && !dots)
            dots++;
        else if (is_digit(cp))
            nd++;
        else
            return false;
    }
    return nd > 0;
}
str integer_text(work *w, double n) {
    char buf[400];
    int size = snprintf(buf, sizeof(buf), "%.0f", trunc(n));
    if (size < 0 || (size_t)size >= sizeof(buf))
        fail(w, FPT_OVERFLOW);
    return copy_str(w, buf, (size_t)size);
}
void add_date(work *w, str s, fpt_date_type type, bool overwrite) {
    fpt_result *r = &w->result;
    for (size_t i = 0; i < r->explicit_count; i++)
        if (equal(s, (str){r->explicit_dates[i].text, r->explicit_dates[i].length})) {
            if (overwrite)
                r->explicit_dates[i].date_type = type;
            return;
        }
    size_t n = r->explicit_count;
    if ((n & (n - 1)) == 0) {
        size_t cap = n ? n * 2 : 1;
        if (cap > SIZE_MAX / sizeof(fpt_explicit_date))
            fail(w, FPT_OUT_OF_MEMORY);
        fpt_explicit_date *p = alloc_mem(w, cap * sizeof(*p));
        if (n)
            memcpy(p, r->explicit_dates, n * sizeof(*p));
        r->explicit_dates = p;
    }
    s = copy_str(w, s.p, s.n);
    r->explicit_dates[n] = (fpt_explicit_date){s.p, s.n, type};
    r->explicit_count++;
}
void add_relative(work *w, fpt_result *r, fpt_relative_time t) {
    size_t n = r->relative_count;
    if ((n & (n - 1)) == 0) {
        size_t cap = n ? n * 2 : 1;
        if (cap > SIZE_MAX / sizeof(t))
            fail(w, FPT_OUT_OF_MEMORY);
        fpt_relative_time *p = alloc_mem(w, cap * sizeof(t));
        if (n)
            memcpy(p, r->relative_times, n * sizeof(t));
        r->relative_times = p;
    }
    r->relative_times[n] = t;
    r->relative_count++;
}
bool valid_calendar(int y, int m, int d) {
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return y >= 1 && y <= 9999 && m >= 1 && m <= 12 && d >= 1 &&
           d <= days[m - 1] + (m == 2 && y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
}
bool year_valid(work *w, int64_t y) { return y >= w->ctx->year - 100 && y <= w->ctx->year + 10; }
void fpt_result_free(fpt_result *r) {
    if (!r)
        return;
    allocation *a = r->_storage;
    while (a) {
        allocation *next = a->next;
        free(a);
        a = next;
    }
    memset(r, 0, sizeof(*r));
}
