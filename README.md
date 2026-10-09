# fast-parse-time C17

[![C17 verification](https://github.com/craigtrim/fast-parse-time-c17/actions/workflows/ci.yml/badge.svg)](https://github.com/craigtrim/fast-parse-time-c17/actions/workflows/ci.yml)
[![Version](https://img.shields.io/badge/version-1.5.0--c17.1-blue.svg)](include/fast_parse_time.h)
[![Port of fast-parse-time 1.5.0](https://img.shields.io/badge/port%20of-fast--parse--time%201.5.0-3776AB.svg?logo=python&logoColor=white)](https://pypi.org/project/fast-parse-time/)
[![C17](https://img.shields.io/badge/C-17-00599C.svg?logo=c&logoColor=white)](https://en.cppreference.com/w/c/17)
[![CMake 3.20+](https://img.shields.io/badge/CMake-3.20%2B-064F8C.svg?logo=cmake&logoColor=white)](https://cmake.org/)
[![Platforms](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-lightgrey.svg)](#build-from-source)
[![Runtime dependencies: none](https://img.shields.io/badge/runtime%20dependencies-none-brightgreen.svg)](#highlights)
[![Upstream tests: 21,170 identical](https://img.shields.io/badge/upstream%20tests-21%2C170%20identical-brightgreen.svg)](docs/VERIFICATION.md)
[![Differential checks: 0 mismatches](https://img.shields.io/badge/differential%20checks-342%2C294%20%2F%200%20mismatches-brightgreen.svg)](docs/VERIFICATION.md)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

`fast-parse-time-c17` is a native C17 port of [fast-parse-time](https://github.com/craigtrim/fast-parse-time) 1.5.0. It finds explicit dates and relative time expressions in text, resolves them to offsets and datetimes, and infers missing years from the surrounding dates without a Python interpreter. It ships as a standalone command-line tool and as static and shared libraries with a CMake package.

The port reproduces the Python library's results across the full upstream test suite and 342,294 additional differential comparisons with zero mismatches. On the benchmark below, it processed about 21 times as many lines per second as the Python package.

```console
$ fpt 'Meeting on March 15, 2024 about five days ago'
{"explicit_dates":[{"text":"March 15, 2024","date_type":"FULL_EXPLICIT_DATE"}],"relative_times":[{"cardinality":5,"frame":"day","tense":"past"}],"has_dates":true}
```

## Highlights

- **No runtime dependencies.** On Windows, `fpt.exe` and the DLL import only system libraries, with no separate PCRE2 or GCC runtime DLL. PCRE2 10.45 is vendored and statically linked, so the build downloads nothing.
- **The knowledge base is compiled in.** All 164,108 phrase entries live in C tables alongside the Unicode and regex tables, so nothing is loaded from disk at startup. A single `fpt` invocation, from process start to JSON output, takes about 35 ms.
- **Results match the Python release.** The port preserves ordering, duplicate handling, ambiguous dates, phrase-table quirks, and the upstream expected failures.
- **The C API is designed for embedding.** Input is UTF-8 with explicit byte lengths, so embedded NUL characters are supported. Results own their memory, and one context can be shared across threads.
- **Every Python export has a C counterpart.** All 23 public exports are mapped, including the 1.4.0 calendar occurrence API and the legacy `ExplicitTimeExtractor` stages.
- **Output can be made independent of the clock.** Fix the current year and weekday through `fpt_options`, or `--year` and `--weekday` on the command line, and the same input produces the same output on any day.

## Performance

| | Python `fast-parse-time` 1.5.0 | `fpt` 1.5.0-c17.1 |
|---|---|---|
| 20,000 lines | 34.0 to 35.4 s | 1.63 to 1.65 s |
| Throughput | about 575 lines/s | about 12,200 lines/s |

Measured on an AMD Ryzen Threadripper 3960X under Windows 10, single-threaded, with Python 3.11.0 and a GCC 16.1 Release build. The corpus repeats 12 sentences that mix explicit dates, relative expressions, and plain text. The Python figure times only a loop of `parse_dates` calls and excludes interpreter startup and import. The C figure times the entire `fpt --lines` process, including startup and JSON output. Both implementations produced identical results for all 12 sentences.

## Quick start

You need a C17 compiler, CMake 3.20 or newer, and Ninja.

```powershell
git clone https://github.com/craigtrim/fast-parse-time-c17.git
cd fast-parse-time-c17
.\build.ps1
.\build\fpt.exe 'Meeting on March 15, 2024 about five days ago'
```

On Linux:

```sh
git clone https://github.com/craigtrim/fast-parse-time-c17.git
cd fast-parse-time-c17
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/fpt 'Meeting on March 15, 2024 about five days ago'
```

## What it finds

| Input | Result |
|---|---|
| `Invoice dated 2023-07-14` | `2023-07-14` as `FULL_EXPLICIT_DATE` |
| `14 March 2025` | `14 March 2025` as `FULL_EXPLICIT_DATE` |
| `see you on 3/4` | `3/4` as `DAY_MONTH_AMBIGUOUS` |
| `The contract ran from 1999 to 2004` | `1999-2004` as `YEAR_RANGE` |
| `half an hour ago` | 30 minutes, past |
| `the day after tomorrow` | 2 days, future |
| `next Monday`, asked on a Wednesday | 5 days, future |

Explicit dates carry one of 15 classifications, from `FULL_EXPLICIT_DATE` and `MONTH_YEAR` to `SEASON_YEAR` and `FUZZY_DATE`. Relative expressions come back as a cardinality, a frame such as `day` or `week`, and a tense, and they resolve to signed offsets or to datetimes against a reference time you supply.

## Command line

`fpt` reads UTF-8 text and writes UTF-8 JSON. Pass the text as an argument, or omit it and the tool reads all of stdin as one input.

### Explicit dates and relative expressions

```console
$ fpt 'Meeting on March 15, 2024 about five days ago' | jq .
{
  "explicit_dates": [
    {
      "text": "March 15, 2024",
      "date_type": "FULL_EXPLICIT_DATE"
    }
  ],
  "relative_times": [
    {
      "cardinality": 5,
      "frame": "day",
      "tense": "past"
    }
  ],
  "has_dates": true
}
```

### Calendar dates with year inference

`--calendar` returns every date occurrence with its month, day, year, and candidate dates. When the input contains at least two full dates, they bound an inclusive range, and a date without a year resolves only if the range admits exactly one calendar date.

```console
$ fpt --calendar 'Term runs from 11/28/2025 to 4/14/2026; meetings on Nov 22 and March 13' | jq -c '.[]'
{"text":"11/28/2025","month":11,"day":28,"year":2025,"date":"2025-11-28","candidates":["2025-11-28"]}
{"text":"4/14/2026","month":4,"day":14,"year":2026,"date":"2026-04-14","candidates":["2026-04-14"]}
{"text":"Nov 22","month":11,"day":22,"year":null,"date":null,"candidates":[]}
{"text":"March 13","month":3,"day":13,"year":2026,"date":"2026-03-13","candidates":["2026-03-13"]}
```

March 13 falls inside that range once, in 2026. November 22 falls outside it in both 2025 and 2026, so the parser leaves its year unknown instead of substituting the current year.

### Ambiguous numeric dates

The parser keeps both readings of a numeric date until you tell it which convention the source uses.

```console
$ fpt --calendar --range 2026-01-01 2026-12-31 'Meet on 3/4'
[{"text":"3/4","month":null,"day":null,"year":2026,"date":null,"candidates":["2026-03-04","2026-04-03"]}]

$ fpt --calendar --date-order dmy --range 2026-01-01 2026-12-31 'Meet on 3/4'
[{"text":"3/4","month":4,"day":3,"year":2026,"date":"2026-04-03","candidates":["2026-04-03"]}]
```

### Batches

`--lines` treats each line of stdin as an independent input and writes one JSON document per line.

```console
$ printf 'Invoice dated 2023-07-14\nThe outage started half an hour ago\nNo dates here\n' | fpt --lines
{"explicit_dates":[{"text":"2023-07-14","date_type":"FULL_EXPLICIT_DATE"}],"relative_times":[],"has_dates":true}
{"explicit_dates":[],"relative_times":[{"cardinality":30,"frame":"minute","tense":"past"}],"has_dates":true}
{"explicit_dates":[],"relative_times":[],"has_dates":false}
```

### Options

| Option | Effect |
|---|---|
| `--calendar` | Parse calendar occurrences and infer years |
| `--date-strings` | Extract the original calendar date strings, preserving duplicates |
| `--date-order auto\|mdy\|dmy` | Choose the numeric calendar interpretation |
| `--range YYYY-MM-DD YYYY-MM-DD` | Supply inclusive bounds for year inference |
| `--year YYYY` | Fix the year used by legacy year-window validation |
| `--weekday 0..6` | Fix the current weekday for weekday references (Monday is 0) |
| `--lines` | Treat each stdin line as an independent input |
| `--version` | Print the version and the upstream source commit |

Exit code 0 means success, 1 means a parsing error, and 2 means a command, I/O, or setup error. Put `--` before text that begins with an option name.

## C API

Include `fast_parse_time.h`. Create a context once, then call any parsing function with a UTF-8 pointer and a byte length.

```c
#include "fast_parse_time.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    fpt_context *context = NULL;
    if (fpt_context_create(NULL, &context) != FPT_OK)
        return 1;
    const char *text = "Meeting on March 15, 2024 about five days ago";
    fpt_result result = FPT_RESULT_INIT;
    fpt_status status = fpt_parse_dates(context, text, strlen(text), &result);
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
```

```text
March 15, 2024: FULL_EXPLICIT_DATE
5 day (past)
```

Every result type has an `*_INIT` macro and a matching free function. Results own their memory and stay valid after the context is destroyed. A context holds only immutable state, so threads can share one as long as each request has its own result object.

### Use it from CMake

```cmake
find_package(fast_parse_time 1.5 CONFIG REQUIRED)
target_link_libraries(your_program PRIVATE fast_parse_time::fast_parse_time)
```

Link `fast_parse_time::fast_parse_time_shared` instead for the shared library; the target supplies the DLL import definition. The static target brings its PCRE2 dependency with it. [examples](examples) contains a complete consumer project.

### Choose a function

| What you need | Function | Result |
|---|---|---|
| Explicit dates and relative expressions together | `fpt_parse_dates` | `fpt_result` |
| Explicit dates with their classifications | `fpt_extract_explicit_dates` | `fpt_result` |
| Relative expressions as cardinality, frame, and tense | `fpt_extract_relative_times` | `fpt_result` |
| Relative expressions as signed offsets | `fpt_resolve_to_timedelta` | `fpt_resolved_result` |
| Relative expressions resolved against a reference time | `fpt_resolve_to_datetime` | `fpt_resolved_result` |
| Date strings from prose or a batch of strings | `fpt_extract_date_strings` | `fpt_calendar_result` |
| Calendar dates with range-based year inference | `fpt_parse_date_strings` | `fpt_calendar_result` |
| A yes or no answer about temporal content | `fpt_has_temporal_info` | `bool` |

The [API reference](docs/API.md) maps all 23 Python exports to C and documents ownership, return statuses, clock behavior, calendar options, and the low-level `fpt_run` extraction stages.

## Fidelity and verification

The port targets fast-parse-time 1.5.0 at commit `4841982b791fd0c3a2da8b48e28c7c22e7d86a86`, and `src/generated/manifest.json` records the hash of every source file it was generated from.

| Check | Result |
|---|---|
| Complete upstream suite through the native adapter | 21,170 identical outcomes |
| Direct recorded-output comparisons | 31,458, zero mismatches |
| Every knowledge-base entry plus generated extraction, normalization, and arithmetic cases | 190,836, zero mismatches |
| Additional duration and floating-point comparisons | 120,000, zero mismatches |
| Native API tests, thread test, and installed CMake consumer | Passed |

Matching the source includes matching its arithmetic, so months resolve to 30 days and years to 365 days. The calendar parser and the legacy explicit extractor keep their distinct semantics. These checks establish parity on the tested domain; they are not a proof over every possible Unicode string. [VERIFICATION.md](docs/VERIFICATION.md) describes the method and how to repeat it.

The checked-in regression corpus runs with the Python standard library alone, without the upstream package:

```sh
python tests/differential.py --corpus tests/data/observations.jsonl --calendar-corpus tests/data/calendar-observations.jsonl
python tests/arithmetic_parity.py
python tests/cli_checks.py
```

Set `FPT_LIBRARY` to test a different shared library, or `FPT_CLI` to test a different executable. CI builds with GCC and Clang on Linux and with GCC on Windows, replays the corpus, and runs the Linux build under AddressSanitizer and UndefinedBehaviorSanitizer.

## Build from source

The release build was verified with GCC 16.1 (MinGW-w64, UCRT) on Windows x64. Python is not needed to build or run anything; it is used only for verification and for regenerating the tables.

### Windows

```powershell
.\build.ps1                          # Release build, native tests, versioned executable
.\build.ps1 -Package                 # also builds the SDK directory and ZIP
.\build.ps1 -Configuration Debug     # Debug build; filenames gain -debug
```

The version comes from `FPT_VERSION` in [fast_parse_time.h](include/fast_parse_time.h), and the build carries it into the filenames, `--version`, the CMake package version, and the Windows file properties.

```text
build\fpt-1.5.0-c17.1.exe
build\libfast_parse_time.dll
build\libfast_parse_time.a
dist\fast-parse-time-c17-1.5.0-c17.1-windows-x64.zip
```

The ZIP contains the standalone executable, the DLL, the static libraries, the public header, the CMake package, documentation, examples, and licenses.

### Linux and other POSIX systems

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --install build --prefix "$PWD/install"
```

Pass `-DFPT_SANITIZE=ON` to build with AddressSanitizer and UndefinedBehaviorSanitizer, or `-DFPT_BUILD_SHARED=OFF` to skip the shared library.

## Project layout

| Path | Contents |
|---|---|
| [include](include) | The public header |
| [src](src) | The handwritten C parser and command-line tool |
| [src/generated](src/generated) | Generated knowledge-base, Unicode, and regex tables, with the source manifest |
| [tests](tests) | Native C tests and the Python differential harness |
| [tools](tools) | Table generation and full source verification scripts |
| [examples](examples) | A standalone CMake consumer |
| [vendor](vendor) | PCRE2 10.45 and the Python license |

[VERIFICATION.md](docs/VERIFICATION.md#updating-the-port) explains how to move the port to a new upstream release.

## License

This port and the upstream parser are available under the [MIT license](LICENSE). Third-party notices are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and [vendor](vendor).

Craig Trim maintains both projects. Report problems with the C port in [this repository's issues](https://github.com/craigtrim/fast-parse-time-c17/issues), and problems with parsing behavior in [the upstream issues](https://github.com/craigtrim/fast-parse-time/issues).
