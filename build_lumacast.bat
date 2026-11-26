@echo off
set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set "VCPKG_VISUAL_STUDIO_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
set "PATH=C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;%PATH%"
echo === CONFIGURE ===
"C:\Qt\Tools\CMake_64\bin\cmake.exe" -S "%ROOT%" -B "%ROOT%\build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64 -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows -DCMAKE_MAKE_PROGRAM=C:\Qt\Tools\Ninja\ninja.exe
if errorlevel 1 exit /b 1
echo === BUILD ===
"C:\Qt\Tools\CMake_64\bin\cmake.exe" --build "%ROOT%\build"
exit /b %errorlevel%
