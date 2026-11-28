@echo off
rem Dong goi bo cai Windows tu thu muc build (chay build_lumacast.bat truoc). Tham so tuy chon: phien ban x.y.z.
rem Ket qua: build\pkg\LumaCast-Setup-x.y.z.exe va build\pkg\releases (de dang len GitHub Releases khi muon phat hanh).
set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set "PATH=C:\Qt\6.8.3\msvc2022_64\bin;C:\Qt\Tools\CMake_64\bin;C:\Program Files (x86)\NSIS;C:\velopack\tools;%PATH%"
set "VER="
if not "%~1"=="" set "VER=-DVERSION=%~1"
"C:\Qt\Tools\CMake_64\bin\cmake.exe" -DBUILD_DIR="%ROOT%\build" -DSOURCE_DIR="%ROOT%" %VER% -P "%ROOT%\cmake\Package.cmake"
exit /b %errorlevel%
