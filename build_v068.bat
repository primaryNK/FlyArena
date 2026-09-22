@echo off
setlocal
set "ROOT=%~dp0"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" ( echo [ERROR] vswhere.exe not found. & exit /b 1 )
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL ( echo [ERROR] Visual Studio C++ toolchain not found. & exit /b 1 )
call "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" -arch=x64 >nul
if errorlevel 1 exit /b 1
if not exist "%ROOT%bin" mkdir "%ROOT%bin"
if not exist "%ROOT%obj\v068" mkdir "%ROOT%obj\v068"
pushd "%ROOT%obj\v068"
echo Building FlyArena v0.6.8...
cl /nologo /O2 /std:c++20 /EHsc /DNDEBUG /DNOMINMAX /DUNICODE /D_UNICODE /DFLYARENA_PRODUCT_UI /W4 ^
  /I"%ROOT%src\shared" /I"%ROOT%src\neural" /I"%ROOT%src\io" /I"%ROOT%src\arena" /I"%ROOT%src\render" /I"%ROOT%src\learning" /I"%ROOT%src\profile" /I"%ROOT%src\ui" ^
  /Fo"%ROOT%obj\v068\\" ^
  "%ROOT%src\shared\topology_v2.cpp" ^
  "%ROOT%src\io\io_map.cpp" ^
  "%ROOT%src\neural\gpu_dual_brain.cpp" ^
  "%ROOT%src\arena\arena_sim.cpp" ^
  "%ROOT%src\arena\equipment_combat.cpp" ^
  "%ROOT%src\profile\fly_profile.cpp" ^
  "%ROOT%src\ui\create_fly_dialog.cpp" ^
  "%ROOT%src\ui\reward_tuning_dialog.cpp" ^
  "%ROOT%src\learning\plastic_readout.cpp" ^
  "%ROOT%src\render\win32_renderer.cpp" ^
  "%ROOT%src\sim\v065_learning_main.cpp" ^
  d3d12.lib dxgi.lib d3dcompiler.lib d2d1.lib dwrite.lib user32.lib gdi32.lib winmm.lib ole32.lib comdlg32.lib ^
  /Fe:"%ROOT%bin\FlyArena-v0.6.8.exe"
if errorlevel 1 ( popd & exit /b 1 )
popd
echo.
echo FlyArena v0.6.8 build complete.
exit /b 0
