# fast-parse-time — C17

A complete native port of fast-parse-time 1.5.0, including explicit extraction,
relative expressions, temporal resolution, and the calendar occurrence API.
The source snapshot is commit `4841982b791fd0c3a2da8b48e28c7c22e7d86a86`;
`src/generated/manifest.json` records the exact source file hashes.

The executable and libraries run without Python. All 164,108 knowledge-base
entries are compiled into C tables. Number-word conversion, date validation,
Unicode handling, and date arithmetic execute natively. PCRE2 10.45 is vendored
and linked statically, so building requires no downloads.

## Build on Windows

Requirements: a C17 compiler, CMake 3.20 or newer, and Ninja. The delivered build
was verified with GCC 16.1, MinGW-w64/UCRT, on Windows x64.

```powershell
cd D:\git\fpt\fast-parse-time-c17
.\build.ps1
```

The script builds Release by default, runs the native tests, and writes a
versioned executable. Use `-Package` to also create the versioned SDK directory
and ZIP, or `-Configuration Debug` for a debug build (filenames gain `-debug`).
The version comes from `FPT_VERSION` in `include/fast_parse_time.h` and is used
for the filenames, `--version`, CMake package version, and Windows File
Properties. No Python environment is required.

Current outputs (`-Package` includes the ZIP):

```text
D:\git\fpt\fast-parse-time-c17\build\fpt-1.5.0-c17.1.exe
D:\git\fpt\fast-parse-time-c17\build\libfast_parse_time.dll
D:\git\fpt\fast-parse-time-c17\build\libfast_parse_time.a
D:\git\fpt\fast-parse-time-c17\dist\fast-parse-time-c17-1.5.0-c17.1-windows-x64.zip
```

`build\fpt.exe` is also available for scripts and tests. Packaged executables
are under `dist\fast-parse-time-c17-1.5.0-c17.1\bin`.

The ZIP includes the standalone executable, DLL, static libraries, public
header, CMake package, documentation, examples, and licenses. `fpt.exe` and the
DLL depend only on Windows system libraries; no separate PCRE2 or GCC runtime
DLL is needed. The static library's PCRE2 dependency is included in the package
and supplied automatically by its CMake target.

On a POSIX platform the equivalent build commands are:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --install build --prefix "$PWD/install"
```

## Command line

```powershell
.\build\fpt.exe 'Meeting on March 15, 2024 about five days ago'
.\build\fpt.exe --calendar '11/28/2025 4/14/2026 March 13'
.\build\fpt.exe --calendar --date-order dmy --range 2026-01-01 2026-12-31 '3/4'
.\build\fpt.exe --date-strings 'March 13 and March 13'
.\build\fpt.exe --year 2026 --weekday 2 'next Monday'
'5 days ago', 'next week' | .\build\fpt.exe --lines
```

Output is UTF-8 JSON. The default mode returns explicit dates, relative times,
and `has_dates`. `--calendar` returns original occurrences, month/day/year,
the unique date when known, and all candidates. `--date-strings` returns
original calendar strings, preserving duplicates. Without a text argument,
the program reads stdin as one UTF-8 input; `--lines` treats each line as an
independent input. Exit codes: 0 success, 1 parsing error, 2 command/I/O/setup
error. Use `--` before text beginning with an option name.

## C API

Include `fast_parse_time.h`. All text is UTF-8 with explicit byte lengths;
embedded NUL characters are supported. Results own their memory. Initialize
them with the corresponding `*_INIT` macro and free them before reuse.

```c
#include "fast_parse_time.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    fpt_context *ctx = NULL;
    if (fpt_context_create(NULL, &ctx) != FPT_OK) return 1;
    const char *text = "March 15, 2024 about five days ago";
    fpt_result result = FPT_RESULT_INIT;
    fpt_status status = fpt_parse_dates(ctx, text, strlen(text), &result);
    if (status == FPT_OK)
        printf("%zu explicit, %zu relative\n",
               result.explicit_count, result.relative_count);
    fpt_result_free(&result);
    fpt_context_destroy(ctx);
    return status == FPT_OK ? 0 : 1;
}
```

For an installed package:

```cmake
find_package(fast_parse_time 1.5 CONFIG REQUIRED)
target_link_libraries(your_program PRIVATE fast_parse_time::fast_parse_time)
```

Alternatively link `fast_parse_time::fast_parse_time_shared` for the shared
library. The CMake target supplies the DLL import definition. An independent
consumer project is provided in `examples`.

All 23 public Python exports have C counterparts, including the legacy
`ExplicitTimeExtractor` stages. `docs/API.md` maps every export and documents low-level
operations, ownership, return statuses, clock behavior, and calendar options.

## Fidelity and verification

The port preserves observed source behavior, including ordering, duplicate
handling, ambiguous dates, phrase-table oddities, and upstream expected
failures. Months resolve to 30 days and years to 365 days, matching the source.
The newer calendar parser and legacy explicit extractor retain their distinct
semantics.

Verified against the current 1.5.0 source:

| Check | Result |
|---|---|
| Complete upstream suite through the native adapter | 21,170 identical outcomes |
| Direct recorded-output comparisons | 31,458, zero mismatches |
| Exhaustive KB plus generated extraction/normalization/arithmetic cases | 190,836, zero mismatches |
| Additional duration and floating-point comparisons | 120,000, zero mismatches |
| Native API tests and installed CMake consumer | Passed |

Python is needed only for development verification and optional table
regeneration. The checked-in regression corpus runs without the upstream
package or third-party Python dependencies:

```powershell
python tests/differential.py --corpus tests/data/observations.jsonl --calendar-corpus tests/data/calendar-observations.jsonl
python tests/arithmetic_parity.py
python tests/cli_checks.py
```

Set `FPT_LIBRARY` to override the shared library under test. `docs/VERIFICATION.md`
explains the original-source comparison, test counts, and reproducibility.
`tools/verify.ps1` runs the full source comparison when a source checkout and
development Python environment are available.

## Layout and licensing

`include` is the public interface; `src` contains the handwritten C parser;
`src/generated` contains immutable KB/Unicode/regex tables; `tests` contains
native and differential checks; `tools` contains development scripts.

MIT license for this port and the upstream parser. Third-party license notices
are in `THIRD_PARTY_NOTICES.md` and `vendor`.
