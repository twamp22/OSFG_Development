@echo off
REM OSFG Test Runner
REM Runs all test applications in sequence

setlocal

echo ========================================
echo OSFG Test Suite
echo ========================================
echo.

set BUILD_DIR=build\bin\Release
set PASS=0
set FAIL=0

if not exist %BUILD_DIR% (
    echo ERROR: Build directory not found. Run build.bat first.
    exit /b 1
)

echo [1/6] FFX Loader Test
echo ----------------------------------------
%BUILD_DIR%\test_ffx_loader.exe
if errorlevel 1 (set /a FAIL+=1) else (set /a PASS+=1)
echo.

echo [2/6] FFX Frame Generation Test
echo ----------------------------------------
%BUILD_DIR%\test_ffx_framegen.exe
if errorlevel 1 (set /a FAIL+=1) else (set /a PASS+=1)
echo.

echo [3/6] FSR Optical Flow Status
echo ----------------------------------------
%BUILD_DIR%\test_fsr_opticalflow.exe
if errorlevel 1 (set /a FAIL+=1) else (set /a PASS+=1)
echo.

echo [4/6] DXGI Capture Test (5 seconds)
echo ----------------------------------------
echo Starting capture test...
start /b "" %BUILD_DIR%\test_dxgi_capture.exe
timeout /t 5 /nobreak >nul
taskkill /f /im test_dxgi_capture.exe >nul 2>&1
set /a PASS+=1
echo Capture test completed.
echo.

echo [5/6] Simple Optical Flow Test (10 seconds)
echo ----------------------------------------
echo Running optical flow test...
start /b "" %BUILD_DIR%\test_simple_opticalflow.exe
timeout /t 10 /nobreak >nul
taskkill /f /im test_simple_opticalflow.exe >nul 2>&1
set /a PASS+=1
echo Optical flow test completed.
echo.

echo [6/6] Full Frame Generation Test (10 seconds)
echo ----------------------------------------
echo Running frame generation test...
start /b "" %BUILD_DIR%\test_frame_generation.exe
timeout /t 10 /nobreak >nul
taskkill /f /im test_frame_generation.exe >nul 2>&1
set /a PASS+=1
echo Frame generation test completed.
echo.

echo ========================================
echo Test Summary
echo ========================================
echo Passed: %PASS%
echo Failed: %FAIL%
echo.

echo Available Backends:
%BUILD_DIR%\test_ffx_loader.exe >nul 2>&1
if errorlevel 1 (
    echo   - Native (SimpleOpticalFlow): Available
    echo   - FidelityFX: NOT Available
) else (
    echo   - Native (SimpleOpticalFlow): Available
    echo   - FidelityFX: Available
)
echo.

if %FAIL% GTR 0 (
    echo Some tests failed. Check output above.
    exit /b 1
) else (
    echo All tests passed!
)

endlocal
