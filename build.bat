@echo off
setlocal
pushd "%~dp0"
if errorlevel 1 exit /b 1

where cmake >nul 2>&1
if errorlevel 1 goto :missing_cmake
where git >nul 2>&1
if errorlevel 1 goto :missing_git

if not defined BUILD_JOBS set "BUILD_JOBS=1"
cmake --preset windows-x64 %*
if errorlevel 1 goto :failed
cmake --build --preset release --parallel "%BUILD_JOBS%"
if errorlevel 1 goto :failed

echo.
echo Windows x64 build completed: %CD%\build\Release\trainingmaster.dll
popd
exit /b 0

:missing_cmake
echo Required tool not found: cmake 1>&2
popd
exit /b 1

:missing_git
echo Required tool not found: git 1>&2
popd
exit /b 1

:failed
set "BUILD_EXIT_CODE=%ERRORLEVEL%"
echo Build failed with exit code %BUILD_EXIT_CODE% 1>&2
popd
exit /b %BUILD_EXIT_CODE%
