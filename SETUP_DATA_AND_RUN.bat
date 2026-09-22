@echo off
setlocal
cd /d "%~dp0"

echo ============================================================
echo  FlyArena first-time BANC data setup
echo ============================================================
echo This downloads the official BANC v888 source files, creates a
echo local Python environment, converts the topology cache, builds
echo the IO map, and then starts FlyArena.
echo.
echo Requirements: Internet connection, Python 3, and free disk space.
echo Existing completed files are reused on later runs.
echo.

set "FLYARENA_EXE=%~dp0FlyArena.exe"
if exist "%FLYARENA_EXE%" goto :application_ready
if not exist "%~dp0build_v068.bat" goto :application_missing

echo [BUILD] No packaged executable is present; building from source...
call "%~dp0build_v068.bat"
if errorlevel 1 goto :build_failed
set "FLYARENA_EXE=%~dp0bin\FlyArena-v0.6.8.exe"
if not exist "%FLYARENA_EXE%" goto :build_failed

:application_ready

where py.exe >nul 2>nul
if not errorlevel 1 goto :python_ready
where python.exe >nul 2>nul
if errorlevel 1 goto :python_missing
:python_ready
where curl.exe >nul 2>nul
if errorlevel 1 goto :curl_missing

if exist "data\cache\banc_latest_v888_v3.farena" goto :io_map
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\prepare_banc_latest.ps1"
if errorlevel 1 goto :failed

:io_map
if exist "data\io\banc_v888_io_v061.fio" goto :run
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\prepare_v061_io_map.ps1"
if errorlevel 1 goto :failed

:run
echo.
echo [READY] Starting FlyArena...
"%FLYARENA_EXE%"
exit /b %ERRORLEVEL%

:failed
echo.
echo [ERROR] Data setup failed. Review the message above.
echo Python 3 must be available as py.exe or python.exe.
pause
exit /b 1

:build_failed
echo [ERROR] FlyArena source build failed.
echo Install Visual Studio with "Desktop development with C++" and a Windows SDK.
pause
exit /b 1

:application_missing
echo [ERROR] Neither FlyArena.exe nor the v0.6.8 source build script was found.
echo Download and fully extract the official Release archive, then try again.
pause
exit /b 1

:python_missing
echo [ERROR] Python 3 was not found.
echo Install Python 3 and enable "Add Python to PATH", then run this file again.
pause
exit /b 1

:curl_missing
echo [ERROR] Windows curl.exe was not found.
echo Install current Windows updates or curl, then run this file again.
pause
exit /b 1
