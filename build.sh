#!/usr/bin/env bash
set -euo pipefail

project_root=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
cd "$project_root"

if [[ -n "${LLVM_MINGW_ROOT:-}" ]]; then
    export PATH="${LLVM_MINGW_ROOT%/}/bin:$PATH"
fi

for tool in cmake ninja git x86_64-w64-mingw32-clang x86_64-w64-mingw32-clang++ x86_64-w64-mingw32-windres; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        printf 'Required tool not found: %s\n' "$tool" >&2
        exit 1
    fi
done

jobs=${BUILD_JOBS:-2}
if [[ ! "$jobs" =~ ^[1-9][0-9]*$ ]]; then
    printf 'BUILD_JOBS must be a positive integer\n' >&2
    exit 1
fi

cmake --preset linux-cross-windows-x64 "$@"
cmake --build --preset linux-cross-release --parallel "$jobs"
printf '\nWindows x64 build completed: %s/build-linux-cross/trainingmaster.dll\n' "$project_root"
