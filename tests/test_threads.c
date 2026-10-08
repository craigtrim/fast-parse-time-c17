#include "fast_parse_time.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

typedef struct {
    fpt_context *context;
    int failed;
} request;

#ifdef _WIN32
static DWORD WINAPI worker(LPVOID argument) {
#else
static void *worker(void *argument) {
#endif
    request *req = argument;
    for (int i = 0; i < 250; i++) {
        fpt_result result = FPT_RESULT_INIT;
        const char *text = "March 15, 2024 about five days ago";
        fpt_status status = fpt_parse_dates(req->context, text, strlen(text), &result);
        if (status || result.explicit_count != 1 || result.relative_count != 1 ||
            result.relative_times[0].cardinality != 5)
            req->failed = 1;
        fpt_result_free(&result);
        const char *calendar_text = "11/28/2025 4/14/2026 March 13";
        fpt_text input = {calendar_text, strlen(calendar_text)};
        fpt_calendar_result dates = FPT_CALENDAR_RESULT_INIT;
        status = fpt_parse_date_strings(req->context, &input, 1, NULL, &dates);
        if (status || dates.count != 3 || dates.dates[2].year != 2026)
            req->failed = 1;
        fpt_calendar_result_free(&dates);
    }
    return 0;
}

int main(void) {
    enum { THREADS = 8 };
    fpt_context *context = NULL;
    if (fpt_context_create(NULL, &context))
        return 1;
    request requests[THREADS];
#ifdef _WIN32
    HANDLE threads[THREADS];
#else
    pthread_t threads[THREADS];
#endif
    int started = 0, failed = 0;
    for (int i = 0; i < THREADS; i++) {
        requests[i] = (request){context, 0};
#ifdef _WIN32
        threads[i] = CreateThread(NULL, 0, worker, &requests[i], 0, NULL);
        if (!threads[i]) {
            failed = 1;
            break;
        }
#else
        if (pthread_create(&threads[i], NULL, worker, &requests[i])) {
            failed = 1;
            break;
        }
#endif
        started++;
    }
    for (int i = 0; i < started; i++) {
#ifdef _WIN32
        if (WaitForSingleObject(threads[i], INFINITE) != WAIT_OBJECT_0)
            failed = 1;
        CloseHandle(threads[i]);
#else
        if (pthread_join(threads[i], NULL))
            failed = 1;
#endif
        failed |= requests[i].failed;
    }
    fpt_context_destroy(context);
    if (!failed)
        puts("Shared-context concurrency checks passed");
    return failed;
}
