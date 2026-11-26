@echo off
set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set "PATH=C:\Qt\6.8.3\msvc2022_64\bin;C:\Qt\Tools\CMake_64\bin;C:\Program Files (x86)\NSIS;%PATH%"
"C:\Qt\Tools\CMake_64\bin\cpack.exe" --config "%ROOT%\build\CPackConfig.cmake" -C Release -B "%ROOT%\build\pkg"
exit /b %errorlevel%
