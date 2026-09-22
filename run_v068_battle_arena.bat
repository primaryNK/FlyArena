@echo off
setlocal
cd /d "%~dp0"
if not exist "data\cache\banc_latest_v888_v3.farena" goto :missing
if not exist "data\io\banc_v888_io_v061.fio" goto :missing
call "%~dp0build_v068.bat"
if errorlevel 1 goto :fail
"%~dp0bin\FlyArena-v0.6.8.exe" ^
  --topology "%~dp0data\cache\banc_latest_v888_v3.farena" ^
  --io-map "%~dp0data\io\banc_v888_io_v061.fio" ^
  --red-name Ruby --blue-name Azure ^
  --duration-ms 60000 --world-step-ms 50 --gpu-budget 40 ^
  --render-fps 120 --sensitivity 2.05 --forward-gain 1.35 --turn-gain 1.15 ^
  --learning 0 --exploration 0 ^
  --red-flypack "%~dp0data\flies\Ruby.flypack" ^
  --blue-flypack "%~dp0data\flies\Azure.flypack" ^
  --red-train "%~dp0data\training\Ruby_v065.flytrain" ^
  --blue-train "%~dp0data\training\Azure_v065.flytrain"
if errorlevel 1 goto :fail
exit /b 0
:missing
echo [ERROR] Required BANC cache/IO map missing.
echo Run prepare_banc_latest.bat and prepare_v061_io_map.bat first.
:fail
exit /b 1
