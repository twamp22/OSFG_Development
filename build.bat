@echo off
REM OSFG Build Script
REM Usage: build.bat [config] [target]
REM   config: Release (default) or Debug
REM   target: all (default), test, demo, or specific target name

setlocal enabledelayedexpansion

set CONFIG=%1
set TARGET=%2

if "%CONFIG%"=="" set CONFIG=Release
if "%TARGET%"=="" set TARGET=all

REM Find CMake
set CMAKE_PATH=
for %%p in (cmake.exe) do set CMAKE_PATH=%%~$PATH:p
if "%CMAKE_PATH%"=="" set CMAKE_PATH=C:\Program Files\CMake\bin\cmake.exe

if not exist "%CMAKE_PATH%" (
    echo ERROR: CMake not found. Please install CMake 3.20+ and add to PATH.
    exit /b 1
)

echo ========================================
echo OSFG Build Script
echo ========================================
echo Configuration: %CONFIG%
echo Target: %TARGET%
echo CMake: %CMAKE_PATH%
echo ========================================
echo.

REM Configure if build directory doesn't exist
if not exist build (
    echo Configuring project...
    "%CMAKE_PATH%" -B build -G "Visual Studio 17 2022" -A x64
    if errorlevel 1 (
        echo ERROR: CMake configuration failed.
        exit /b 1
    )
    echo.
)

REM Build based on target
if "%TARGET%"=="all" (
    echo Building all targets...
    "%CMAKE_PATH%" --build build --config %CONFIG%
) else if "%TARGET%"=="test" (
    echo Building test targets...
    "%CMAKE_PATH%" --build build --config %CONFIG% --target test_dxgi_capture
    "%CMAKE_PATH%" --build build --config %CONFIG% --target test_simple_opticalflow
    "%CMAKE_PATH%" --build build --config %CONFIG% --target test_fsr_opticalflow
    "%CMAKE_PATH%" --build build --config %CONFIG% --target test_frame_generation
    "%CMAKE_PATH%" --build build --config %CONFIG% --target test_dual_gpu_pipeline
) else if "%TARGET%"=="demo" (
    echo Building demo...
    "%CMAKE_PATH%" --build build --config %CONFIG% --target osfg_demo
) else (
    echo Building target: %TARGET%
    "%CMAKE_PATH%" --build build --config %CONFIG% --target %TARGET%
)

if errorlevel 1 (
    echo.
    echo ERROR: Build failed.
    exit /b 1
)

echo.
echo ========================================
echo Build completed successfully!
echo ========================================
echo.
echo Binaries located in: build\bin\%CONFIG%\
echo.
echo Available executables:
dir /b build\bin\%CONFIG%\*.exe 2>nul
echo.

endlocal
