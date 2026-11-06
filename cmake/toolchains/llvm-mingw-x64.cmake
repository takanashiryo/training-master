# Build Windows x64 binaries on Windows, Linux, or macOS with LLVM MinGW.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

find_program(CMAKE_C_COMPILER NAMES x86_64-w64-mingw32-clang
  HINTS "$ENV{LLVM_MINGW_ROOT}/bin" REQUIRED)
find_program(CMAKE_CXX_COMPILER NAMES x86_64-w64-mingw32-clang++
  HINTS "$ENV{LLVM_MINGW_ROOT}/bin" REQUIRED)
find_program(CMAKE_RC_COMPILER NAMES x86_64-w64-mingw32-windres
  HINTS "$ENV{LLVM_MINGW_ROOT}/bin" REQUIRED)

# Host programs (including the build system) must not come from the target sysroot.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
