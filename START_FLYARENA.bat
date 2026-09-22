@echo off
setlocal
cd /d "%~dp0"
set "FLYARENA_EXE=%~dp0FlyArena.exe"
if exist "%FLYARENA_EXE%" goto :check_data
set "FLYARENA_EXE=%~dp0bin\FlyArena-v0.6.8.exe"
if exist "%FLYARENA_EXE%" goto :check_data
echo [BUILD REQUIRED] FlyArena has not been built yet.
echo Run SETUP_DATA_AND_RUN.bat first.
echo.
pause
exit /b 3

:check_data
if not exist "data\cache\banc_latest_v888_v3.farena" goto :missing
if not exist "data\io\banc_v888_io_v061.fio" goto :missing
"%FLYARENA_EXE%"
exit /b %ERRORLEVEL%

:missing
echo [SETUP REQUIRED] BANC topology cache or IO map is missing.
echo Run SETUP_DATA_AND_RUN.bat once from this extracted folder.
echo.
pause
exit /b 2
