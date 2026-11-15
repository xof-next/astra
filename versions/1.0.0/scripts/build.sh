#!/usr/bin/env bash
set -euo pipefail
build_jobs="${ASTRA_BUILD_JOBS:-2}"
case "$build_jobs" in
    ''|*[!0-9]*|0)
        printf '%s\n' 'ASTRA_BUILD_JOBS must be a positive integer.' >&2
        exit 2
        ;;
esac
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel "$build_jobs"
ctest --test-dir build --output-on-failure --parallel "$build_jobs" --timeout 60
