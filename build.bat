@echo off
REM Builds Piano Host on Windows.
REM Needs: Visual Studio 2022 (with "Desktop development with C++") and CMake (included with VS).
REM The first build downloads JUCE (~150 MB) and takes a few minutes.

setlocal
cd /d "%~dp0"

where cmake >nul 2>nul
if errorlevel 1 (
    echo CMake not found. Run this from the "Developer Command Prompt for VS 2022",
    echo or install CMake from https://cmake.org and tick "Add to PATH".
    pause
    exit /b 1
)

cmake -B build -G "Visual Studio 17 2022" -A x64 || goto :fail
cmake --build build --config Release --parallel || goto :fail

echo.
echo Built: %~dp0build\PianoHost_artefacts\Release\Piano Host.exe
start "" "%~dp0build\PianoHost_artefacts\Release"
exit /b 0

:fail
echo.
echo Build failed - see the messages above.
pause
exit /b 1
