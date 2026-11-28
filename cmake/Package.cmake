# Dong goi ban phat hanh Windows (khong dang gi len mang):
#   1) cmake --install (exe + Qt + FFmpeg + velopack_libc.dll + tai lieu; chan NDI) vao <build>/pkg/app
#   2) vpk pack -> <build>/pkg/releases: goi day du, goi phan thay doi (neu con ban truoc trong thu muc), releases.win.json, Setup.exe
#   3) makensis packaging/installer.nsi -> <build>/pkg/LumaCast-Setup-<ban>.exe  (hoi ngon ngu roi chay Setup.exe im lang)
#
#   cmake -DBUILD_DIR=<build> -DSOURCE_DIR=<repo> [-DVERSION=x.y.z] [-DOUTPUT_DIR=<thu muc>] [-DVPK=<vpk.exe>] [-DMAKENSIS=<makensis.exe>] -P cmake/Package.cmake
cmake_minimum_required(VERSION 3.21)

if(NOT BUILD_DIR OR NOT SOURCE_DIR)
  message(FATAL_ERROR "Can -DBUILD_DIR va -DSOURCE_DIR")
endif()
if(NOT OUTPUT_DIR)
  set(OUTPUT_DIR "${BUILD_DIR}/pkg")
endif()
if(NOT VERSION)
  file(STRINGS "${BUILD_DIR}/CMakeCache.txt" _v REGEX "^CMAKE_PROJECT_VERSION:")
  string(REGEX REPLACE "^[^=]*=" "" VERSION "${_v}")
endif()
if(NOT VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+")
  message(FATAL_ERROR "Phien ban phai dang x.y.z: '${VERSION}'")
endif()
find_program(VPK NAMES vpk HINTS "C:/velopack/tools")
find_program(MAKENSIS NAMES makensis HINTS "C:/Program Files (x86)/NSIS")
if(NOT VPK)
  message(FATAL_ERROR "Khong thay vpk. Cai: dotnet tool install --tool-path C:/velopack/tools vpk --version 1.2.161")
endif()
if(NOT MAKENSIS)
  message(FATAL_ERROR "Khong thay makensis (NSIS)")
endif()

function(run what)
  execute_process(COMMAND ${ARGN} RESULT_VARIABLE _rc)
  if(_rc)
    message(FATAL_ERROR "${what} that bai (${_rc})")
  endif()
endfunction()

set(_app "${OUTPUT_DIR}/app")
set(_releases "${OUTPUT_DIR}/releases")
file(REMOVE_RECURSE "${_app}")
run("cmake --install" "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --prefix "${_app}")
if(NOT EXISTS "${_app}/velopack_libc.dll")
  message(FATAL_ERROR "Ban dich nay khong kem tu cap nhat (khong thay velopack_libc.dll). Cau hinh lai voi -DLUMACAST_VELOPACK_DIR.")
endif()
if(NOT EXISTS "${_app}/vcruntime140.dll")                # DLL Visual C++ di kem goi: bo cai KHONG tai gi tu mang
  message(FATAL_ERROR "Goi thieu Visual C++ runtime (khong thay vcruntime140.dll). Cau hinh lai trong moi truong MSVC.")
endif()

run("vpk pack" "${VPK}" pack --packId LumaCast --packVersion "${VERSION}" --packDir "${_app}" --mainExe lumacast.exe --runtime win-x64
    --packTitle LumaCast --packAuthors LumaCast --icon "${SOURCE_DIR}/resources/icon/lumacast.ico"
    --outputDir "${_releases}")

file(GLOB _setup "${_releases}/*Setup.exe")
list(LENGTH _setup _n)
if(NOT _n EQUAL 1)
  message(FATAL_ERROR "Can dung mot file *Setup.exe trong ${_releases}: ${_setup}")
endif()

include("${SOURCE_DIR}/cmake/Notices.cmake")
lc_installer_notices("${SOURCE_DIR}/THIRD-PARTY-NOTICES.txt" "${OUTPUT_DIR}/nsis/notices-vi.txt")
lc_installer_notices("${SOURCE_DIR}/THIRD-PARTY-NOTICES.en.txt" "${OUTPUT_DIR}/nsis/notices-en.txt")

set(_out "${OUTPUT_DIR}/LumaCast-Setup-${VERSION}.exe")
file(TO_NATIVE_PATH "${_setup}" _setup_n)
file(TO_NATIVE_PATH "${_out}" _out_n)
file(TO_NATIVE_PATH "${OUTPUT_DIR}/nsis/notices-vi.txt" _vi)
file(TO_NATIVE_PATH "${OUTPUT_DIR}/nsis/notices-en.txt" _en)
run("makensis" "${MAKENSIS}" /V2 /INPUTCHARSET UTF8 "/DVERSION=${VERSION}" "/DSETUP=${_setup_n}" "/DOUTFILE=${_out_n}"
    "/DNOTICES_VI=${_vi}" "/DNOTICES_EN=${_en}" "${SOURCE_DIR}/packaging/installer.nsi")

message(STATUS "Bo cai: ${_out}")
message(STATUS "Dang len GitHub Releases (TU LAM khi muon phat hanh): cac file trong ${_releases}")
