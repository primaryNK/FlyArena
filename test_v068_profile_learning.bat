@echo off
setlocal
set "ROOT=%~dp0"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" ( echo [ERROR] vswhere.exe not found. & exit /b 1 )
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL ( echo [ERROR] Visual Studio C++ toolchain not found. & exit /b 1 )
call "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" -arch=x64 >nul
if errorlevel 1 exit /b 1
if not exist "%ROOT%obj\v068_tests" mkdir "%ROOT%obj\v068_tests"
pushd "%ROOT%obj\v068_tests"
cl /nologo /O2 /std:c++20 /EHsc /DNOMINMAX /W4 ^
  /I"%ROOT%src\arena" /I"%ROOT%src\render" /I"%ROOT%src\learning" /I"%ROOT%src\profile" /I"%ROOT%src\neural" ^
  "%ROOT%tests\v065_profile_learning_tests.cpp" ^
  "%ROOT%src\arena\arena_sim.cpp" ^
  "%ROOT%src\arena\equipment_combat.cpp" ^
  "%ROOT%src\profile\fly_profile.cpp" ^
  "%ROOT%src\learning\plastic_readout.cpp" ^
  /Fe:"%ROOT%obj\v068_tests\FlyArenaV068Tests.exe"
if errorlevel 1 ( popd & exit /b 1 )
"%ROOT%obj\v068_tests\FlyArenaV068Tests.exe"
set "TEST_RESULT=%ERRORLEVEL%"
popd
exit /b %TEST_RESULT%
