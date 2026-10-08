# Linux build and usage

Build a native Linux executable from the same C17 source. The Linux command
line, JSON output, and library API match the Windows build. Python is not
required to build or run the parser.

## Build

Install GCC or Clang, CMake 3.20 or newer, and Ninja. On Ubuntu 22.04 or newer
or Debian 12 or newer, the distribution packages provide these prerequisites:

```sh
sudo apt-get update
sudo apt-get install build-essential cmake ninja-build
git clone https://github.com/craigtrim/fast-parse-time-c17.git
cd fast-parse-time-c17
./build.sh
```

For an existing clone, run `git pull` before building. Older distributions may
need a newer CMake installation; check `cmake --version` against the 3.20 minimum.
The build uses only vendored source and downloads nothing.

The script runs the native API and concurrency tests and produces:

```text
build-linux/fpt-1.5.0-c17.1
build-linux/fpt
build-linux/libfast_parse_time.so
build-linux/libfast_parse_time.a
```

`build-linux` keeps Linux and Windows CMake caches separate, including when
building the same checkout from WSL. The script works from any current directory.
Names derive from `FPT_VERSION` in the public header.

```sh
./build.sh --package
./build.sh --configuration Debug
CC=clang ./build.sh --build-dir build-linux-clang --jobs 2
```

The package includes the executable, shared/static libraries, public header,
CMake package, examples, documentation, and licenses. On x86-64 its path is:

```text
dist/fast-parse-time-c17-1.5.0-c17.1-linux-x86_64.tar.gz
```

The architecture suffix comes from `uname -m`. Debug artifact names include
`-debug`. PCRE2 is linked statically; Linux's standard C and math libraries are
the runtime dependencies. Build on the deployment distribution when matching
its system-library version is required.

## Run

```sh
./build-linux/fpt-1.5.0-c17.1 'Meeting on March 15, 2024 about five days ago'
./build-linux/fpt-1.5.0-c17.1 --calendar '11/28/2025 4/14/2026 March 13'
./build-linux/fpt-1.5.0-c17.1 --date-strings 'March 13 and April 8, 2026'
./build-linux/fpt-1.5.0-c17.1 --lines < input.txt > results.jsonl
./build-linux/fpt-1.5.0-c17.1 --help
```

To run a packaged executable after extracting the archive:

```sh
tar -xzf fast-parse-time-c17-1.5.0-c17.1-linux-x86_64.tar.gz
./fast-parse-time-c17-1.5.0-c17.1-linux-x86_64/bin/fpt-1.5.0-c17.1 --version
```

The executable does not need the `.so` alongside it; it links the parser
statically. The `.so` is for applications that embed the shared C API.

## Additional verification

Python 3.8 or newer suffices for the frozen regression corpus, arithmetic, and
CLI checks. Set the test paths to the Linux build:

```sh
export FPT_LIBRARY="$PWD/build-linux/libfast_parse_time.so"
export FPT_CLI="$PWD/build-linux/fpt-1.5.0-c17.1"
python3 tests/differential.py --corpus tests/data/observations.jsonl --calendar-corpus tests/data/calendar-observations.jsonl
python3 tests/arithmetic_parity.py
python3 tests/cli_checks.py
```

GCC and Clang builds are also checked by GitHub Actions, including installed
CMake consumption and AddressSanitizer/UndefinedBehaviorSanitizer. Linux CI
uploads the versioned `.tar.gz` under the run's artifacts.

To build locally with sanitizers:

```sh
./build.sh --build-dir build-linux-sanitize --configuration Debug -- -DFPT_SANITIZE=ON
```

Extra arguments after `--` are passed to CMake. For example,
`-DFPT_BUILD_SHARED=OFF` builds only the static library and executable.
