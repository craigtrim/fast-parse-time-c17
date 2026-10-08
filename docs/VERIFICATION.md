# Verification and source tracking

Source: fast-parse-time 1.5.0, commit
`4841982b791fd0c3a2da8b48e28c7c22e7d86a86`. The port includes the calendar API,
generator/Slot corrections, and latest package reorganization. Runtime Python
file hashes in `src/generated/manifest.json` identify the actual working tree,
including any uncommitted runtime changes. Documentation-only source edits do
not affect those hashes.

## Completed local checks

The Windows x64 Release build used GCC 16.1 (MinGW-w64/UCRT), C17 without language
extensions, and vendored PCRE2 10.45. Handwritten C builds with `-Wall -Wextra
-Wpedantic`. The native API test and a separately configured installed CMake
consumer both passed. DLL/executable imports contain only Windows system
libraries. CLI verification covers UTF-16 command-line conversion to UTF-8,
raw stdin whitespace preservation, batch lines, JSON output, and error exits.

The Linux x86-64 build was also verified under Ubuntu 20.04.6 in WSL, with GCC
9.4.0 and CMake 4.4.2. `build.sh --package` passed both native tests and produced
the versioned ELF executable and SDK archive. On that build, the 31,458 frozen
observations and 120,000 arithmetic comparisons had zero mismatches. CLI checks
passed both before and after archive extraction, and an independent CMake
consumer built and ran against the extracted SDK. See `LINUX.md` for commands.

The complete current upstream suite produces identical per-test outcomes with
the source implementation and native adapter:

- 17,427 passed.
- 383 xfailed (known upstream expected failures).
- 3,360 xpassed (upstream marks them as expected failures, but they pass).
- 56 passing subtests in addition to those 21,170 test outcomes.
- No failed tests or differing outcomes.

The suite comparison preserves expected failures rather than modifying parser
behavior to satisfy assertions the original implementation also fails.
The 1.5.0 Python import-identity, pickle, and isolated packaging checks still
exercise Python packaging/DTO behavior. They are included to keep the entire
source suite running, and are not claimed as native binary tests. C packaging
is separately checked by installed CMake consumption and the standalone CLI.

Direct differential comparison checks all 31,458 distinct operation/input
observations recorded during the source suite. It compares extracted values,
ordering, exception categories, original calendar text, ambiguity, candidate
dates, and the year/date fields computed in C: zero mismatches.

Extended parity checks 190,836 cases with zero mismatches:

- Every one of the 164,108 KB entries, processed through the source normalizer,
  sequence filter, and resolver, versus C.
- 5,000 generated relative expressions.
- 15,000 generated explicit inputs, including Unicode/boundary cases.
- 5,000 direct number-word normalization comparisons, including decimal
  conversion and Python's shortest-roundtrip float formatting.
- 288 duration and 1,440 datetime cases, including calendar extrema and
  half-microsecond rounding.

The exhaustive oracle caches immutable index sets to avoid rebuilding them
for every query; it retains the original intersection/ambiguity algorithm.
Additional standalone arithmetic comparison checks 100,000 total-seconds
values spanning Python's duration range and 20,000 fractional durations:
120,000 checks, zero mismatches. Seeds are fixed in the test scripts.

The native C test exercises exported wrappers, ownership/reuse, embedded NUL,
invalid UTF-8, calendar inference/ambiguity, arithmetic overflow, Unicode enum
lookup, decimal formatting, host numeric-locale independence, and concurrent
requests sharing one context. Test evidence is generated in
`test-results`; the source-derived regression inputs are checked into
`tests/data` for repeatable verification without the source package.

These checks establish parity with the pinned implementation on the tested
domain; they are not a proof over every possible Unicode string or arbitrary
Python object. C representation differences are explicit in `API.md`.

## Repeat the complete source comparison

Use Python 3.11 to reproduce the pinned Unicode 14.0.0 tables:

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements-dev.txt
.\tools\verify.ps1 -Source D:\git\fpt\fast-parse-time
```

The script checks source hashes, builds C, installs the source into the
development environment for its isolated packaging tests, runs both complete
suites, compares outcomes, replays all observations, exercises every KB phrase,
runs arithmetic/CLI checks, and rechecks source hashes. It does not change the
source repository. The runtime C build does not use this Python environment.

For corpus replay alone, use the commands in `README.md`; Python's standard
library is sufficient. To test a different shared library, set `FPT_LIBRARY`.
To test a different executable, set `FPT_CLI`.

## Updating the port

1. Review changed source algorithms and public exports, and update handwritten C.
2. Run `python tools/generate_data.py PATH_TO_SOURCE` with Python 3.11 to rebuild
   effective phrase lookup, regex patterns, Unicode tables, and the source manifest.
3. Check `FPT_VERSION` in the public header. CMake, Windows version metadata,
   executable filenames, and package names derive their versions from it.
4. Run the full verification script and resolve every new parity difference.
5. Refresh the two observation files and context metadata under `tests/data`
   from the successful baseline in `test-results`. Update documentation evidence.
6. Build the versioned executable and delivery archive with `build.ps1 -Package`
   on Windows or `./build.sh --package` on Linux.

The generator does not replace algorithm review: some explicit extraction
patterns and all procedural behavior are handwritten C translations.

CI configuration builds Windows GCC and Linux GCC/Clang, replays the frozen
corpus, and includes Linux AddressSanitizer/UndefinedBehaviorSanitizer checks.
The Linux GCC and Clang jobs have completed successfully, including sanitizer
checks. The workflow also verifies packaged executables and uploads versioned
Linux SDK archives for download from the run's artifacts.
