#!/usr/bin/env bash
set -euo pipefail

project_root=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
if ! command -v cmake >/dev/null 2>&1; then
    printf 'Required tool not found: cmake (3.24 or newer)\n' >&2
    exit 1
fi

# Native Windows CMake also works from Git Bash/MSYS2. Both entry points
# delegate platform and toolchain decisions to the same CMake script.
exec cmake -P "$project_root/cmake/build.cmake" -- "$@"
