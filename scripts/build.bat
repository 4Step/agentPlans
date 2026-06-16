@echo off
rem Build agentPlans with MSVC (VS 2026 / VS18) + Ninja.
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
  echo [build] could not initialize VS18 environment
  exit /b 1
)
set ROOT=%~dp0..
cmake -S "%ROOT%" -B "%ROOT%\build" -G Ninja -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b 1
ninja -C "%ROOT%\build"
endlocal
