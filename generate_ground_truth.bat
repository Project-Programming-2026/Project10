@echo off
setlocal
cd /d "%~dp0"
if not exist "build\dev\project10.exe" (
  echo project10.exe was not found. Build the project with CMake first.
  pause
  exit /b 1
)
"build\dev\project10.exe"
set "result=%ERRORLEVEL%"
pause
exit /b %result%
