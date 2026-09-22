@echo off
setlocal
cd /d "%~dp0"
if not exist "data\cache\banc_latest_v888_v3.farena" goto :missing
if not exist "data\io\banc_v888_io_v061.fio" goto :missing
"%~dp0FlyArena.exe"
exit /b %ERRORLEVEL%

:missing
echo [SETUP REQUIRED] BANC topology cache or IO map is missing.
echo Run SETUP_DATA_AND_RUN.bat once from this extracted folder.
echo.
pause
exit /b 2
