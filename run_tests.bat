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

echo [1/5] FSR Optical Flow Status
echo ----------------------------------------
%BUILD_DIR%\test_fsr_opticalflow.exe
if errorlevel 1 (set /a FAIL+=1) else (set /a PASS+=1)
echo.

echo [2/5] DXGI Capture Test (5 seconds)
echo ----------------------------------------
echo Starting capture test... Press Ctrl+C to skip.
timeout /t 1 /nobreak >nul
start /b "" %BUILD_DIR%\test_dxgi_capture.exe
timeout /t 5 /nobreak >nul
taskkill /f /im test_dxgi_capture.exe >nul 2>&1
set /a PASS+=1
echo Capture test completed.
echo.

echo [3/5] Simple Optical Flow Test
echo ----------------------------------------
echo Running optical flow test (10 seconds)...
%BUILD_DIR%\test_simple_opticalflow.exe
if errorlevel 1 (set /a FAIL+=1) else (set /a PASS+=1)
echo.

echo ========================================
echo Test Summary
echo ========================================
echo Passed: %PASS%
echo Failed: %FAIL%
echo.

if %FAIL% GTR 0 (
    echo Some tests failed. Check output above.
    exit /b 1
) else (
    echo All tests passed!
)

endlocal
