@echo off
setlocal
where cmake >nul 2>&1
if errorlevel 1 (
    echo Required tool not found: cmake ^(3.24 or newer^) 1>&2
    exit /b 1
)

cmake -P "%~dp0cmake\build.cmake" -- %*
exit /b %ERRORLEVEL%
