#include "fast_parse_time.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    fpt_context *context = NULL;
    fpt_status status = fpt_context_create(NULL, &context);
    if (status != FPT_OK)
        return 1;
    const char *text = "Meeting on March 15, 2024 about five days ago";
    fpt_result result = FPT_RESULT_INIT;
    status = fpt_parse_dates(context, text, strlen(text), &result);
    if (status == FPT_OK) {
        for (size_t i = 0; i < result.explicit_count; ++i)
            printf("%s: %s\n", result.explicit_dates[i].text,
                   fpt_date_type_name(result.explicit_dates[i].date_type));
        for (size_t i = 0; i < result.relative_count; ++i)
            printf("%.0f %s (%s)\n", result.relative_times[i].cardinality,
                   result.relative_times[i].frame, result.relative_times[i].tense);
    }
    fpt_result_free(&result);
    fpt_context_destroy(context);
    return status == FPT_OK ? 0 : 1;
}
