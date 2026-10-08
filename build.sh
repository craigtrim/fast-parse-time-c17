#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
build_dir="$repo_root/build-linux"
configuration=Release
package=false
jobs=
cmake_args=()

usage() {
    cat <<'HELP'
Usage: ./build.sh [OPTIONS] [-- CMAKE_OPTIONS...]
Build and test the Linux C17 executable. No Python is required.

  --package                 Also install an SDK and create a versioned .tar.gz
  --configuration TYPE      Release (default) or Debug
  --build-dir PATH          Build directory (default: repository/build-linux)
  --jobs N                  Maximum parallel build jobs
  --help                    Show this help

Examples:
  ./build.sh
  ./build.sh --package
  CC=clang ./build.sh --build-dir build-linux-clang
  ./build.sh --build-dir build-linux-sanitize --configuration Debug -- -DFPT_SANITIZE=ON
HELP
}

die() { printf '%s\n' "$*" >&2; exit 2; }
while (($#)); do
    case "$1" in
        --help|-h) usage; exit 0 ;;
        --package) package=true; shift ;;
        --configuration|--build-dir|--jobs)
            (($# >= 2)) || die "Missing value for $1"
            case "$1" in
                --configuration) configuration=$2 ;;
                --build-dir) build_dir=$2 ;;
                --jobs) jobs=$2 ;;
            esac
            shift 2 ;;
        --) shift; cmake_args=("$@"); break ;;
        *) die "Unknown option: $1 (use --help)" ;;
    esac
done

[[ $(uname -s) == Linux ]] || die 'This script builds Linux binaries; use build.ps1 on Windows.'
[[ $configuration == Release || $configuration == Debug ]] || die 'Configuration must be Release or Debug.'
[[ -n $build_dir ]] || die 'Build directory cannot be empty.'
[[ -z $jobs || $jobs =~ ^[1-9][0-9]*$ ]] || die 'Jobs must be a positive integer.'
for tool in cmake ctest ninja; do
    command -v "$tool" >/dev/null || die "Missing tool: $tool. Install CMake 3.20+, Ninja, and a C17 compiler."
done
if $package; then command -v tar >/dev/null || die 'Packaging requires tar.'; fi

mkdir -p -- "$build_dir"
build_dir="$(cd -- "$build_dir" && pwd -P)"
cmake -S "$repo_root" -B "$build_dir" -G Ninja "-DCMAKE_BUILD_TYPE=$configuration" "${cmake_args[@]}"
if [[ -n $jobs ]]; then
    cmake --build "$build_dir" --parallel "$jobs"
else
    cmake --build "$build_dir" --parallel
fi
ctest --test-dir "$build_dir" --output-on-failure

version_output="$("$build_dir/fpt" --version)"
[[ $version_output =~ ^([0-9]+\.[0-9]+\.[0-9]+-c17\.[0-9]+)[[:space:]] ]] || die 'Could not read the built executable version.'
artifact_version=${BASH_REMATCH[1]}
if [[ $configuration == Debug ]]; then artifact_version+=-debug; fi
executable_name="fpt-$artifact_version"
cp -- "$build_dir/fpt" "$build_dir/$executable_name"
chmod +x -- "$build_dir/$executable_name"
printf 'Executable: %s\n' "$build_dir/$executable_name"

if $package; then
    architecture=$(uname -m)
    package_name="fast-parse-time-c17-$artifact_version-linux-$architecture"
    package_dir="$repo_root/dist/$package_name"
    archive_path="$repo_root/dist/$package_name.tar.gz"
    cmake --install "$build_dir" --prefix "$package_dir"
    cp -- "$build_dir/$executable_name" "$package_dir/bin/$executable_name"
    cp -- "$repo_root/README.md" "$repo_root/src/generated/manifest.json" "$package_dir/"
    cp -R -- "$repo_root/docs" "$repo_root/examples" "$package_dir/"
    tar -czf "$archive_path" -C "$repo_root/dist" "$package_name"
    printf 'Package: %s\n' "$archive_path"
fi
