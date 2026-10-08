#include "fast_parse_time.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <fcntl.h>
#include <io.h>
#endif

typedef struct {
    int mode; /* 0: legacy parse; 1: calendar parse; 2: original date strings */
    fpt_calendar_options calendar;
} cli_options;

static void json_string(const char *s, size_t n) {
    putchar('"');
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"' || c == '\\') {
            putchar('\\');
            putchar(c);
        } else if (c < 32)
            printf("\\u%04x", c);
        else
            putchar(c);
    }
    putchar('"');
}
static void print_date(fpt_calendar_date date) {
    if (date.year)
        printf("\"%04d-%02d-%02d\"", date.year, date.month, date.day);
    else
        printf("null");
}
static void print_component(int value) {
    if (value)
        printf("%d", value);
    else
        printf("null");
}
static int calendar_parse(fpt_context *ctx, const char *text, size_t n,
                          const cli_options *options) {
    fpt_text input = {text, n};
    fpt_calendar_result result = {0};
    fpt_status status = options->mode == 2
                            ? fpt_extract_date_strings(ctx, &input, 1, &result)
                            : fpt_parse_date_strings(ctx, &input, 1, &options->calendar, &result);
    if (status) {
        printf("{\"error\":");
        const char *message = fpt_status_string(status);
        json_string(message, strlen(message));
        puts("}");
        return 1;
    }
    putchar('[');
    for (size_t i = 0; i < result.count; i++) {
        if (i)
            putchar(',');
        fpt_parsed_date *d = result.dates + i;
        if (options->mode == 2) {
            json_string(d->text, d->length);
            continue;
        }
        printf("{\"text\":");
        json_string(d->text, d->length);
        printf(",\"month\":");
        print_component(d->month);
        printf(",\"day\":");
        print_component(d->day);
        printf(",\"year\":");
        print_component(d->year);
        printf(",\"date\":");
        print_date(d->date);
        printf(",\"candidates\":[");
        for (size_t j = 0; j < d->candidate_count; j++) {
            if (j)
                putchar(',');
            print_date(d->candidates[j]);
        }
        printf("]}");
    }
    puts("]");
    fpt_calendar_result_free(&result);
    return 0;
}
static int parse(fpt_context *ctx, const char *text, size_t n, const cli_options *options) {
    if (options->mode)
        return calendar_parse(ctx, text, n, options);
    fpt_result r = FPT_RESULT_INIT;
    fpt_status status = fpt_parse_dates(ctx, text, n, &r);
    if (status) {
        printf("{\"error\":");
        const char *msg = fpt_status_string(status);
        json_string(msg, strlen(msg));
        puts("}");
        return 1;
    }
    printf("{\"explicit_dates\":[");
    for (size_t i = 0; i < r.explicit_count; i++) {
        if (i)
            putchar(',');
        printf("{\"text\":");
        json_string(r.explicit_dates[i].text, r.explicit_dates[i].length);
        printf(",\"date_type\":");
        const char *type = fpt_date_type_name(r.explicit_dates[i].date_type);
        json_string(type, strlen(type));
        putchar('}');
    }
    printf("],\"relative_times\":[");
    for (size_t i = 0; i < r.relative_count; i++) {
        if (i)
            putchar(',');
        fpt_relative_time *t = r.relative_times + i;
        printf("{\"cardinality\":");
        if (t->cardinality_text)
            json_string(t->cardinality_text, strlen(t->cardinality_text));
        else
            printf("%.17g", t->cardinality);
        printf(",\"frame\":");
        json_string(t->frame, strlen(t->frame));
        printf(",\"tense\":");
        json_string(t->tense, strlen(t->tense));
        putchar('}');
    }
    printf("],\"has_dates\":%s}\n", fpt_result_has_dates(&r) ? "true" : "false");
    fpt_result_free(&r);
    return 0;
}
static int run_cli(int argc, char **argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    fpt_options opt = FPT_OPTIONS_INIT;
    cli_options options = {0, FPT_CALENDAR_OPTIONS_INIT};
    bool lines = false;
    int first = 1;
    for (; first < argc; first++) {
        if (!strcmp(argv[first], "--help")) {
            puts("Usage: fpt [OPTIONS] [TEXT]\nUTF-8 "
                 "text to JSON. Without TEXT, read all stdin; --lines reads one "
                 "input per line.\n"
                 "  --calendar                 Parse calendar occurrences and infer years\n"
                 "  --date-strings             Extract original calendar date strings\n"
                 "  --date-order auto|mdy|dmy  Numeric calendar interpretation\n"
                 "  --range YYYY-MM-DD YYYY-MM-DD  Inclusive calendar inference bounds\n"
                 "  --year YYYY                Freeze legacy year-window validation\n"
                 "  --weekday 0..6             Freeze weekday references (Monday=0)\n"
                 "  --lines                    Independent input per line\n"
                 "  --version                  Print version and source commit");
            return 0;
        }
        if (!strcmp(argv[first], "--version")) {
            puts(FPT_VERSION " (" FPT_SOURCE_COMMIT ")");
            return 0;
        }
        if (!strcmp(argv[first], "--lines")) {
            lines = true;
            continue;
        }
        if (!strcmp(argv[first], "--calendar")) {
            options.mode = 1;
            continue;
        }
        if (!strcmp(argv[first], "--date-strings")) {
            options.mode = 2;
            continue;
        }
        if (!strcmp(argv[first], "--date-order") && first + 1 < argc) {
            const char *order = argv[++first];
            if (!strcmp(order, "auto"))
                options.calendar.date_order = FPT_DATE_ORDER_AUTO;
            else if (!strcmp(order, "mdy"))
                options.calendar.date_order = FPT_DATE_ORDER_MDY;
            else if (!strcmp(order, "dmy"))
                options.calendar.date_order = FPT_DATE_ORDER_DMY;
            else {
                fputs("Invalid date order\n", stderr);
                return 2;
            }
            continue;
        }
        if (!strcmp(argv[first], "--range") && first + 2 < argc) {
            fpt_calendar_date *bounds[] = {&options.calendar.start, &options.calendar.end};
            for (int i = 0; i < 2; i++) {
                const char *s = argv[++first];
                int used = 0;
                if (sscanf(s, "%4d-%2d-%2d%n", &bounds[i]->year, &bounds[i]->month, &bounds[i]->day,
                           &used) != 3 ||
                    s[used]) {
                    fputs("Invalid range date\n", stderr);
                    return 2;
                }
            }
            options.calendar.has_range = true;
            continue;
        }
        if ((!strcmp(argv[first], "--year") || !strcmp(argv[first], "--weekday")) &&
            first + 1 < argc) {
            bool year = !strcmp(argv[first], "--year");
            char *end;
            long value = strtol(argv[++first], &end, 10);
            if (*end || value < 0 || value > (year ? 9999 : 6)) {
                fputs("Invalid option value\n", stderr);
                return 2;
            }
            if (year)
                opt.current_year = (int)value;
            else
                opt.weekday = (int)value;
            continue;
        }
        if (!strcmp(argv[first], "--")) {
            first++;
            break;
        }
        break;
    }
    fpt_context *ctx = NULL;
    fpt_status status = fpt_context_create(&opt, &ctx);
    if (status) {
        fprintf(stderr, "%s\n", fpt_status_string(status));
        return 2;
    }
    int rc = 0;
    if (first < argc) {
        size_t n = 0;
        for (int i = first; i < argc; i++)
            n += strlen(argv[i]) + 1;
        char *text = malloc(n);
        if (!text) {
            fpt_context_destroy(ctx);
            return 2;
        }
        text[0] = 0;
        for (int i = first; i < argc; i++) {
            if (i > first)
                strcat(text, " ");
            strcat(text, argv[i]);
        }
        rc = parse(ctx, text, strlen(text), &options);
        free(text);
    } else {
        size_t n = 0, cap = 4096;
        char *buffer = malloc(cap);
        if (!buffer) {
            fpt_context_destroy(ctx);
            return 2;
        }
        int ch;
        while ((ch = getchar()) != EOF) {
            if (lines && ch == '\n') {
                if (n && buffer[n - 1] == '\r')
                    n--;
                rc |= parse(ctx, buffer, n, &options);
                n = 0;
                continue;
            }
            if (n == cap) {
                if (cap > SIZE_MAX / 2) {
                    rc = 2;
                    break;
                }
                cap *= 2;
                char *p = realloc(buffer, cap);
                if (!p) {
                    rc = 2;
                    break;
                }
                buffer = p;
            }
            buffer[n++] = (char)ch;
        }
        if (rc != 2 && (n || !lines))
            rc |= parse(ctx, buffer, n, &options);
        if (ferror(stdin))
            rc = 2;
        free(buffer);
    }
    fpt_context_destroy(ctx);
    return rc;
}

int main(int argc, char **argv) {
#ifdef _WIN32
    /* Read the native UTF-16 command line rather than the process ANSI codepage. */
    int count = 0;
    wchar_t **wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!wide)
        return 2;
    char **utf8 = calloc((size_t)count, sizeof(*utf8));
    if (!utf8) {
        LocalFree(wide);
        return 2;
    }
    int result = 2;
    for (int i = 0; i < count; i++) {
        int n =
            WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide[i], -1, NULL, 0, NULL, NULL);
        if (!n)
            goto done;
        utf8[i] = malloc((size_t)n);
        if (!utf8[i] || !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide[i], -1, utf8[i], n,
                                             NULL, NULL))
            goto done;
    }
    (void)argc;
    (void)argv;
    result = run_cli(count, utf8);
done:
    for (int i = 0; i < count; i++)
        free(utf8[i]);
    free(utf8);
    LocalFree(wide);
    return result;
#else
    return run_cli(argc, argv);
#endif
}
