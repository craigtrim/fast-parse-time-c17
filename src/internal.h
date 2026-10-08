#ifndef FPT_INTERNAL_H
#define FPT_INTERNAL_H
#include "fast_parse_time.h"
#define PCRE2_CODE_UNIT_WIDTH 8
#ifndef PCRE2_STATIC
#define PCRE2_STATIC
#endif
#include "generated/regex_ids.h"
#include "pcre2.h"
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
typedef struct {
    const char *p;
    size_t n;
} str;
typedef struct {
    str *v;
    size_t n;
} words;
typedef struct allocation {
    struct allocation *next;
    max_align_t data[];
} allocation;
typedef struct {
    fpt_context *ctx;
    fpt_result result;
    allocation *memory;
    jmp_buf failure;
    pcre2_match_data *match;
    int weekday;
    str input;
} work;
struct fpt_context {
    pcre2_code **regex;
    size_t regex_count;
    int year, weekday;
#ifdef _WIN32
    _locale_t number_locale;
#else
    locale_t number_locale;
#endif
};
typedef struct {
    size_t start[10], end[10];
} match;

_Noreturn void fail(work *, fpt_status);
void *alloc_mem(work *, size_t);
str copy_str(work *, const char *, size_t);
str literal(const char *);
bool eq(str, const char *);
bool equal(str, str);
str join_words(work *, words, const char *);
words split_words(work *, str);
str replace_text(work *, str, str, str);
str lower_text(work *, str);
str strip_text(str);
uint32_t utf8_next(str, size_t *);
bool is_space(uint32_t);
bool is_digit(uint32_t);
bool is_numeric(uint32_t);
int decimal_value(uint32_t);
bool integer(str, int64_t *);
bool digit_string(str);
bool numeric_token(str);
str integer_text(work *, double);
void push_word(work *, words *, str);
void add_date(work *, str, fpt_date_type, bool);
void add_relative(work *, fpt_result *, fpt_relative_time);
bool valid_calendar(int, int, int);
bool year_valid(work *, int64_t);
void extract_explicit(work *, str, fpt_operation);
void extract_relative(work *, str);
str normalize(work *, str);
str expand_compact(work *, str);
words replace_digits(work *, words);
bool validate_date(work *, str);
words tokenize_numeric(work *, str);
bool preclassify_numeric(work *, str);
fpt_date_type classify_delimited(work *, str, char);
bool regex_find(work *, int, str, size_t, match *);
str group(str, match *, int);
int group_number(work *, int, const char *);
extern const char *const fpt_type_names[];
bool valid_utf8(str);
#endif
