# C17 API reference

The authoritative declarations are in `include/fast_parse_time.h`. Functions
return `fpt_status` unless their declaration specifies a direct value. All
public source exports are mapped below.

## Public API mapping

| Python export | C counterpart |
|---|---|
| `parse_dates` | `fpt_parse_dates` |
| `extract_explicit_dates` | `fpt_extract_explicit_dates` |
| `extract_relative_times` | `fpt_extract_relative_times` |
| `parse_time_references` | `fpt_parse_time_references` |
| `extract_numeric_dates` | `fpt_extract_numeric_dates` |
| `parse_dates_with_type` | `fpt_parse_dates_with_type` |
| `extract_ambiguous_dates` | `fpt_extract_ambiguous_dates` |
| `extract_full_dates_only` | `fpt_extract_full_dates_only` |
| `extract_past_references` | `fpt_extract_past_references` |
| `extract_future_references` | `fpt_extract_future_references` |
| `has_temporal_info` | `fpt_has_temporal_info` |
| `resolve_to_datetime` | `fpt_resolve_to_datetime` |
| `resolve_to_timedelta` | `fpt_resolve_to_timedelta` |
| `parse_and_resolve` | `fpt_parse_and_resolve` |
| `get_date_range` | `fpt_get_date_range` |
| `extract_date_strings` | `fpt_extract_date_strings` |
| `parse_date_strings` | `fpt_parse_date_strings` |
| `RelativeTime` | `fpt_relative_time` |
| `ExplicitDate` | `fpt_explicit_date` |
| `ParseResult` | `fpt_result` |
| `ParsedDate` | `fpt_parsed_date` |
| `DateType` | `fpt_date_type` |
| `ExplicitTimeExtractor` | `fpt_context` with the `fpt_run` extraction operations |

`DateType.find` maps to `fpt_date_type_find`; no match is enum value 0.
`fpt_date_type_name` returns the original uppercase enum name. The 16 nonzero
enum values preserve the source's numbering. `ParseResult.has_dates` maps to
`fpt_result_has_dates`.

`RelativeTime.to_timedelta` and `.to_datetime` map to
`fpt_relative_to_timedelta` and `fpt_relative_to_datetime`. A numeric cardinality
uses `double`; an original symbolic cardinality, such as `"2+"`, uses non-NULL
`cardinality_text`. Symbolic values retain their text and fail resolution with
`FPT_NON_NUMERIC_CARDINALITY`.

## Context and ownership

Create a context with `fpt_context_create` and destroy it with
`fpt_context_destroy`. Passing NULL options selects defaults. To override
clock-dependent behavior, start from `FPT_OPTIONS_INIT` and set `current_year`
or `weekday` (Monday=0 through Sunday=6). Zero year snapshots the local current
year at context creation; weekday -1 uses the local current weekday on each
request. This matches the source's cached legacy year window and live weekday
normalization. Recreate the context to refresh its year window.

Contexts contain immutable regex/lookup state and can be shared by threads.
Each simultaneous request needs its own output object. Do not destroy a context
while a request uses it. Decimal conversion uses a context-owned C locale and
does not change the caller's locale.

Every text input is a UTF-8 pointer and a byte length. NULL with length 0 is an
empty string; NULL with nonzero length is invalid. Invalid UTF-8 returns
`FPT_INVALID_UTF8`. C strings used for enum names, frames, and tenses are
NUL-terminated. Returned input-derived text has both a byte length and a final
NUL; use the length when embedded NULs matter.

Initialize `fpt_result`, `fpt_resolved_result`, and `fpt_calendar_result` with
their `*_INIT` macros. Free with `fpt_result_free`,
`fpt_resolved_result_free`, or `fpt_calendar_result_free`, respectively. The
free functions accept NULL and clear the supplied result. Free before reuse;
do not shallow-copy ownership or free individual strings/arrays. Results
remain valid after context destruction. On failure, an initialized output can
still be safely passed to its free function.

Legacy Python dictionaries become ordered `explicit_dates` arrays with unique
text keys and the same replacement order. Relative lists remain ordered and
can contain repeated entries. `has_value` exposes whether a low-level stage
produced a value. Empty C collections represent the source's None/empty
collection outcomes; the low-level operation and `has_value` distinguish them
when needed.

## Resolution

`fpt_timedelta` uses normalized days (-999999999 through 999999999), seconds
(0 through 86399), and microseconds (0 through 999999), including negative
durations. `fpt_timedelta_total_seconds` accepts that normalized representation
and returns the correctly rounded total as a double.

`fpt_datetime` represents Gregorian civil components, years 1 through 9999,
with microsecond precision. A NULL reference selects the local current time.
The C representation has no Python timezone object; callers retain their own
timezone metadata. Arithmetic follows the source's civil datetime addition.
It does not perform timezone conversion or calendar-month arithmetic.

Resolution returns one value per extracted relative expression, in order.
`fpt_parse_and_resolve` additionally retains explicit dates in `parsed`.
`fpt_get_date_range` sets `found` only when there are exactly two relative
references, and orders their resolved dates. It leaves start/end unused when
`found` is false. Errors propagate instead of silently omitting a reference.

## Calendar occurrences

`fpt_extract_date_strings` and `fpt_parse_date_strings` accept an array of
`fpt_text`; use one element for a single string and zero elements for an empty
batch. Results retain the original occurrence text, duplicates, and encounter
order across the entire batch.

Initialize options with `FPT_CALENDAR_OPTIONS_INIT`, or pass NULL. Numeric order
is auto, mdy, or dmy. An optional inclusive range has `has_range=true` and
valid start/end dates. Without an explicit range, the source's inference from
full dates in the batch is reproduced. Ambiguous numeric dates can retain two
interpretations; multi-year bounds can produce several candidates.

In `fpt_parsed_date`, month/day/year equal 0 when unknown or inconsistent across
interpretations. A nonzero `date.year` means exactly one candidate. Candidate
arrays contain complete valid Gregorian dates and preserve source order.
Yearless dates without a usable range have no concrete candidates. Invalid
date matches are rejected with the same overlap/coverage rules as the source.

## Low-level stages

`fpt_run` exposes stages used by the original implementation. Extractor stages
receive their input directly, without applying the high-level normalizer.

| Operation | Output |
|---|---|
| `FPT_OP_PARSE`, `FPT_OP_EXPLICIT`, `FPT_OP_RELATIVE` | High-level results |
| `FPT_OP_NUMERIC`, `FPT_OP_WRITTEN` | Numeric or written explicit dates |
| `FPT_OP_HYPHEN_MONTH_YEAR`, `FPT_OP_PROSE_YEAR` | Month/year or prose year/range results |
| `FPT_OP_ISO8601`, `FPT_OP_ORDINAL`, `FPT_OP_SPACE_MONTH_NUMBER` | Corresponding explicit extraction stage |
| `FPT_OP_NORMALIZE`, `FPT_OP_EXPAND_COMPACT` | One text token, including an empty string |
| `FPT_OP_TOKENIZE_NUMERIC`, `FPT_OP_REPLACE_DIGITS` | Token array |
| `FPT_OP_PRECLASSIFY_NUMERIC`, `FPT_OP_VALIDATE_DATE` | Boolean `has_value` |
| `FPT_OP_CLASSIFY_SLASH`, `FPT_OP_CLASSIFY_DOT`, `FPT_OP_CLASSIFY_HYPHEN` | Zero or one classified explicit entry |

`fpt_classify_delimited` offers a direct enum output for slash, dot, and hyphen
classification. `fpt_parse_dates_with_type` requires an exact uppercase enum
name; NULL selects all types and an unknown name yields an empty collection.

## Errors and C representation

Statuses distinguish invalid arguments, allocation failure, invalid UTF-8,
regex failures, arithmetic overflow, unsupported frames, symbolic
cardinalities, and a source-parser error. `fpt_status_string` supplies a static
human-readable description. The C API reports statuses instead of throwing
Python exceptions.

Python-specific module paths, lazy imports, pickling, logging setup, dataclass
introspection, and arbitrary Python object inputs are represented by C headers,
structs, explicit lengths, statuses, and library packaging. Numeric fields use
the declared C types; arbitrary-precision Python integers and timezone objects
are not embedded in the ABI. All shipped KB cardinalities are represented.
Valid Unicode scalar input uses the pinned Python 3.11 Unicode 14.0.0 rules.
