cmake_minimum_required(VERSION 3.21)
project(LumaCast VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL Network)
# FFmpeg: Linux/macOS dung pkg-config; Windows dung vcpkg (`vcpkg install ffmpeg:x64-windows`, ban LGPL, lien ket dong).
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(FFMPEG QUIET IMPORTED_TARGET libavformat libavcodec libswscale libswresample libavutil)
endif()
if(TARGET PkgConfig::FFMPEG)
  set(LC_FFMPEG PkgConfig::FFMPEG)
else()
  find_package(FFMPEG REQUIRED)
  add_library(lc_ffmpeg INTERFACE)
  target_include_directories(lc_ffmpeg INTERFACE ${FFMPEG_INCLUDE_DIRS})
  target_link_directories(lc_ffmpeg INTERFACE ${FFMPEG_LIBRARY_DIRS})
  target_link_libraries(lc_ffmpeg INTERFACE ${FFMPEG_LIBRARIES})
  # swresample (M5a): vcpkg FindFFMPEG thuong da gom san trong FFMPEG_LIBRARIES; neu chua co thi tim rieng (khong can pkg-config).
  set(_lc_has_swr FALSE)
  foreach(_l ${FFMPEG_LIBRARIES})
    if(_l MATCHES "swresample")
      set(_lc_has_swr TRUE)
    endif()
  endforeach()
  if(NOT _lc_has_swr)
    find_library(LC_SWRESAMPLE_LIB swresample HINTS ${FFMPEG_LIBRARY_DIRS} ${FFMPEG_LIBDIR} PATH_SUFFIXES lib)
    if(LC_SWRESAMPLE_LIB)
      target_link_libraries(lc_ffmpeg INTERFACE ${LC_SWRESAMPLE_LIB})
    else()
      target_link_libraries(lc_ffmpeg INTERFACE swresample)   # co ten thu vien, dua vao link_directories o tren
    endif()
  endif()
  set(LC_FFMPEG lc_ffmpeg)
endif()

# NDI(R): bien dich co NDI hay khong. OFF = KHONG co ma nap runtime NDI nao trong file thuc thi (ban phat hanh khong dinh dieu khoan SDK NDI, xem SPEC 5.1).
# ON (mac dinh, de phat trien/kiem thu) = co backend NDI that (nap dong), chua doi chieu ABI.
option(LUMACAST_ENABLE_NDI "Bien dich ho tro NDI (nap dong runtime). MAC DINH TAT: xem docs/SPEC.md muc 5.1" OFF)
set(LUMACAST_NDI_EULA_ACCEPTED "" CACHE STRING "Dat = I_ACCEPT_NDI_SDK_EULA de BAT NDI, sau khi ban da doc va chap nhan NDI SDK License Agreement")
if(LUMACAST_ENABLE_NDI AND NOT LUMACAST_NDI_EULA_ACCEPTED STREQUAL "I_ACCEPT_NDI_SDK_EULA")
  message(FATAL_ERROR
    "NDI dang bi KHOA. Ma NDI trong repo chi de nguoi co chuyen mon xem. Muon bat phai:\n"
    "  1) tai NDI SDK tu https://ndi.video/for-developers/ va doc/chap nhan NDI SDK License Agreement (tu chiu trach nhiem phap ly: EULA rieng cho ban phan phoi (muc 3d), tuong thich SDK moi nhat, header SDK la thong tin mat - khong dua vao repo);\n"
    "  2) doi chieu ABI trong src/live/NdiRuntime.cpp voi header chinh thuc;\n"
    "  3) cau hinh them -DLUMACAST_NDI_EULA_ACCEPTED=I_ACCEPT_NDI_SDK_EULA.\n"
    "Chi tiet: docs/SPEC.md muc 5.1.")
endif()
if(LUMACAST_ENABLE_NDI)
  set(LC_NDI_RUNTIME_SRC src/live/NdiRuntime.cpp)
else()
  set(LC_NDI_RUNTIME_SRC src/live/NdiStub.cpp)
endif()

add_library(lumacast_core STATIC
  src/core/Channel.cpp
  src/core/Project.cpp
  src/core/Animation.cpp
  src/core/Scene.cpp
  src/core/Log.cpp
  src/core/Features.h src/core/Features.cpp
  src/core/Startup.h src/core/Startup.cpp
  src/render/GlHelpers.cpp
  src/render/Compositor.cpp
  src/render/Engine.h
  src/render/Engine.cpp
  src/render/OutputWindow.cpp
  src/render/OutputHealth.cpp
  src/render/Benchmark.h src/render/Benchmark.cpp
  src/render/EffectDef.cpp
  src/render/BuiltinEffects.cpp
  src/render/EffectRegistry.cpp
  src/render/PluginManager.h
  src/render/PluginManager.cpp
  src/render/OutputSupervisor.h
  src/render/OutputSupervisor.cpp
  src/audio/AudioStream.h
  src/audio/AudioDevice.h src/audio/AudioDevice.cpp
  src/audio/AudioWasapi.cpp
  src/audio/AudioMixer.h src/audio/AudioMixer.cpp
  src/audio/AudioPlan.h src/audio/AudioPlan.cpp
  src/media/StaticSources.cpp
  src/media/FFmpegSource.cpp
  src/media/Hap.h src/media/Hap.cpp
  src/live/LiveInput.h
  src/live/LiveSource.h src/live/LiveSource.cpp
  src/live/Ndi.h src/live/Ndi.cpp
  src/live/NdiFake.h src/live/NdiFake.cpp
  ${LC_NDI_RUNTIME_SRC}
  src/live/Camera.h src/live/Camera.cpp
  src/live/CameraFake.h src/live/CameraFake.cpp
  src/live/CameraMF.cpp
  src/audio/AudioDrift.h
  src/render/NdiOutput.h src/render/NdiOutput.cpp
  src/control/HttpServer.h src/control/HttpServer.cpp
  src/control/Osc.h src/control/Osc.cpp
  src/control/ControlApi.h src/control/ControlApi.cpp
  src/control/ControlPage.h src/control/ControlPage.cpp
  src/control/ControlService.h src/control/ControlService.cpp
)
target_include_directories(lumacast_core PUBLIC src)
if(NOT LUMACAST_ENABLE_NDI)
  target_compile_definitions(lumacast_core PUBLIC LUMACAST_NO_NDI=1)
endif()
target_link_libraries(lumacast_core PUBLIC Qt6::Core Qt6::Gui Qt6::OpenGL Qt6::Network ${LC_FFMPEG})
if(WIN32)
  target_link_libraries(lumacast_core PUBLIC winmm ole32 avrt)   # winmm: timeBeginPeriod(1); ole32 + avrt: WASAPI (COM, MMCSS)
  target_link_libraries(lumacast_core PUBLIC mfplat mf mfreadwrite mfuuid)   # camera: Media Foundation (M5b); khong can Qt Multimedia
endif()

add_library(lumacast_ui STATIC
  src/app/Theme.cpp
  src/app/AboutInfo.h src/app/AboutInfo.cpp
  src/app/MonitorWidget.h src/app/MonitorWidget.cpp
  src/app/InspectorPanel.h src/app/InspectorPanel.cpp
  src/app/ParamEditor.h src/app/ParamEditor.cpp
  src/app/TimelineWidget.h src/app/TimelineWidget.cpp
  src/app/LockButton.h src/app/LockButton.cpp
  src/app/AudioPanel.h src/app/AudioPanel.cpp
  src/app/LiveDialogs.h src/app/LiveDialogs.cpp
  src/app/SettingsDialog.h src/app/SettingsDialog.cpp
  src/app/MainWindow.h src/app/MainWindow.cpp
)
target_include_directories(lumacast_ui PUBLIC src src/app)
target_link_libraries(lumacast_ui PUBLIC lumacast_core Qt6::Widgets)

add_executable(lumacast src/app/main.cpp)
target_link_libraries(lumacast PRIVATE lumacast_ui)
# plug-in mau (xem docs/PLUGIN-SDK.md) di kem file thuc thi: <exe>/plugins/
add_custom_command(TARGET lumacast POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/plugins $<TARGET_FILE_DIR:lumacast>/plugins)

add_executable(lumacast_tests tests/test_render.cpp)
target_link_libraries(lumacast_tests PRIVATE lumacast_core)

enable_testing()
add_test(NAME render COMMAND lumacast_tests)

add_executable(lumacast_ui_tests tests/test_ui.cpp)
target_link_libraries(lumacast_ui_tests PRIVATE lumacast_ui)
add_test(NAME ui COMMAND lumacast_ui_tests)

add_executable(lumacast_m3c_tests tests/test_m3c.cpp)
target_link_libraries(lumacast_m3c_tests PRIVATE lumacast_ui)
add_test(NAME reliability COMMAND lumacast_m3c_tests)

add_executable(lumacast_effects_tests tests/test_effects.cpp)
target_link_libraries(lumacast_effects_tests PRIVATE lumacast_core)
target_compile_definitions(lumacast_effects_tests PRIVATE LUMACAST_PLUGINS_DIR="${CMAKE_SOURCE_DIR}/plugins")
add_test(NAME effects COMMAND lumacast_effects_tests)

add_executable(lumacast_anim_tests tests/test_anim.cpp)
target_link_libraries(lumacast_anim_tests PRIVATE lumacast_core)
add_test(NAME animation COMMAND lumacast_anim_tests)

add_executable(lumacast_control_tests tests/test_control.cpp)
target_link_libraries(lumacast_control_tests PRIVATE lumacast_ui)
add_test(NAME control COMMAND lumacast_control_tests)

add_executable(lumacast_m5c_tests tests/test_m5c.cpp)
target_link_libraries(lumacast_m5c_tests PRIVATE lumacast_ui)
target_compile_definitions(lumacast_m5c_tests PRIVATE LUMACAST_EXE="$<TARGET_FILE:lumacast>")
add_dependencies(lumacast_m5c_tests lumacast)
add_test(NAME ops COMMAND lumacast_m5c_tests)

add_executable(lumacast_audio_tests tests/test_audio.cpp)
target_link_libraries(lumacast_audio_tests PRIVATE lumacast_core)
add_test(NAME audio COMMAND lumacast_audio_tests)

add_executable(lumacast_audio_ui_tests tests/test_audio_ui.cpp)
target_link_libraries(lumacast_audio_ui_tests PRIVATE lumacast_ui)
add_test(NAME audio_engine COMMAND lumacast_audio_ui_tests)

# M5b: thu vien NDI gia (chi cho test: kiem chung duong nap dong + chuyen doi dinh dang; KHONG phai NDI that)
add_library(lumacast_fake_ndi SHARED tests/fake_ndi_lib.cpp)
set_target_properties(lumacast_fake_ndi PROPERTIES CXX_VISIBILITY_PRESET default)

add_executable(lumacast_live_tests tests/test_live.cpp)
target_link_libraries(lumacast_live_tests PRIVATE lumacast_core)
target_compile_definitions(lumacast_live_tests PRIVATE LUMACAST_FAKE_NDI_LIB="$<TARGET_FILE:lumacast_fake_ndi>")
add_dependencies(lumacast_live_tests lumacast_fake_ndi)
if(LUMACAST_ENABLE_NDI)
  add_test(NAME live COMMAND lumacast_live_tests)      # can duong nap dong NDI (thu vien NDI gia)
endif()

add_executable(lumacast_live_ui_tests tests/test_live_ui.cpp)
target_link_libraries(lumacast_live_ui_tests PRIVATE lumacast_ui)
if(LUMACAST_ENABLE_NDI)
  add_test(NAME live_ui COMMAND lumacast_live_ui_tests)
endif()

# M6: hieu nang (YUV shader, PBO, giai ma phan cung du phong, benchmark)
add_executable(lumacast_perf_tests tests/test_perf.cpp)
target_link_libraries(lumacast_perf_tests PRIVATE lumacast_core)
add_test(NAME perf COMMAND lumacast_perf_tests)

# M6: phat hanh (co tinh nang theo goi, Gioi thieu/NDI, giay phep thu vien)
add_executable(lumacast_release_tests tests/test_release.cpp)
target_link_libraries(lumacast_release_tests PRIVATE lumacast_ui)
target_compile_definitions(lumacast_release_tests PRIVATE LUMACAST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME release COMMAND lumacast_release_tests)

# ======================= M6: dong goi / cai dat / CPack =======================
# Cai dat: lumacast + plug-in mau + tai lieu + giay phep. Windows: cai thang vao thu muc goc (exe + DLL canh nhau); he khac: bin/ + share/doc/lumacast.
# Dong goi Windows:  cmake --build build --config Release && cpack --config build/CPackConfig.cmake -G NSIS   (hoac -G ZIP = ban mang theo)
# KHONG BAO GIO kem runtime NDI (SPEC 5.1): buoc cuoi cua cai dat chay cmake/CheckNoNdi.cmake va dung neu thay.
if(WIN32)
  set(LC_INSTALL_BIN ".")
  set(LC_INSTALL_DOC ".")
else()
  set(LC_INSTALL_BIN "bin")
  set(LC_INSTALL_DOC "share/doc/lumacast")
endif()
install(TARGETS lumacast RUNTIME DESTINATION ${LC_INSTALL_BIN})
install(DIRECTORY plugins/ DESTINATION ${LC_INSTALL_BIN}/plugins PATTERN "*.md" EXCLUDE)       # plug-in mau canh file thuc thi: <exe>/plugins/
install(FILES THIRD-PARTY-NOTICES.txt README.md license.example.json DESTINATION ${LC_INSTALL_DOC})
install(DIRECTORY docs/ DESTINATION ${LC_INSTALL_DOC}/docs)
install(DIRECTORY licenses/ DESTINATION ${LC_INSTALL_DOC}/licenses)

if(WIN32)
  # --- Qt: windeployqt qua script trien khai cua Qt (>= 6.3) ---
  option(LUMACAST_DEPLOY_QT "Dong goi Qt bang windeployqt luc cai dat (tat khi chi muon thu cau hinh goi)" ON)
  if(LUMACAST_DEPLOY_QT AND COMMAND qt_generate_deploy_app_script)
    if(Qt6_VERSION VERSION_GREATER_EQUAL 6.5)
      qt_generate_deploy_app_script(TARGET lumacast OUTPUT_SCRIPT LC_QT_DEPLOY_SCRIPT NO_UNSUPPORTED_PLATFORM_ERROR)
    else()
      qt_generate_deploy_app_script(TARGET lumacast FILENAME_VARIABLE LC_QT_DEPLOY_SCRIPT NO_UNSUPPORTED_PLATFORM_ERROR)    # Qt 6.3-6.4
    endif()
    install(SCRIPT ${LC_QT_DEPLOY_SCRIPT})
  elseif(LUMACAST_DEPLOY_QT)
    message(WARNING "Qt < 6.3: khong co qt_generate_deploy_app_script - chay windeployqt tay truoc khi dong goi")
  endif()
  # --- FFmpeg: DLL + cac DLL phu thuoc (zlib...) canh exe; tim trong thu muc bin cua vcpkg/FFmpeg ---
  set(LUMACAST_FFMPEG_BIN_DIR "" CACHE PATH "Thu muc chua avcodec-*.dll, avformat-*.dll... (mac dinh: ../bin cua FFMPEG_LIBRARY_DIRS / vcpkg)")
  set(_lc_ff_dirs ${LUMACAST_FFMPEG_BIN_DIR})
  foreach(_d ${FFMPEG_LIBRARY_DIRS} ${FFMPEG_LIBDIR})
    list(APPEND _lc_ff_dirs "${_d}/../bin" "${_d}/bin" "${_d}")
  endforeach()
  if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    list(APPEND _lc_ff_dirs "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
  endif()
  set(LC_FFMPEG_DLLS "")
  foreach(_d ${_lc_ff_dirs})
    file(GLOB _g "${_d}/avcodec*.dll" "${_d}/avformat*.dll" "${_d}/avutil*.dll" "${_d}/swscale*.dll" "${_d}/swresample*.dll")
    list(APPEND LC_FFMPEG_DLLS ${_g})
  endforeach()
  list(REMOVE_DUPLICATES LC_FFMPEG_DLLS)
  if(LC_FFMPEG_DLLS)
    install(FILES ${LC_FFMPEG_DLLS} DESTINATION .)
    list(GET LC_FFMPEG_DLLS 0 _lc_ff_first)
    get_filename_component(LC_FFMPEG_BIN "${_lc_ff_first}" DIRECTORY)
    install(CODE "
      set(_lc_libs [[${LC_FFMPEG_DLLS}]])
      file(GET_RUNTIME_DEPENDENCIES LIBRARIES \${_lc_libs} DIRECTORIES [[${LC_FFMPEG_BIN}]]
           RESOLVED_DEPENDENCIES_VAR _lc_res UNRESOLVED_DEPENDENCIES_VAR _lc_unres
           PRE_EXCLUDE_REGEXES [[api-ms-.*]] [[ext-ms-.*]] POST_EXCLUDE_REGEXES [[.*[/\\][Ww]indows[/\\][Ss]ystem32.*]])
      foreach(_f IN LISTS _lc_res)
        file(INSTALL \"\${_f}\" DESTINATION \"\${CMAKE_INSTALL_PREFIX}\")
      endforeach()
    ")
  else()
    message(WARNING "Khong tim thay FFmpeg DLL de dong goi (dat -DLUMACAST_FFMPEG_BIN_DIR=...). Bo cai se THIEU FFmpeg.")
  endif()
  # --- Giay phep cua vcpkg (neu co) ---
  foreach(_d ${_lc_ff_dirs})
    get_filename_component(_share "${_d}/../share" ABSOLUTE)
    foreach(_pkg ffmpeg qtbase)
      if(EXISTS "${_share}/${_pkg}/copyright")
        install(FILES "${_share}/${_pkg}/copyright" DESTINATION ${LC_INSTALL_DOC}/licenses RENAME "${_pkg}-copyright.txt")
      endif()
    endforeach()
  endforeach()
endif()

# Buoc CUOI: cam NDI trong goi (chay sau moi install() khac)
install(CODE "
  execute_process(COMMAND \"${CMAKE_COMMAND}\" \"-DDIR=\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}\" -P \"${CMAKE_SOURCE_DIR}/cmake/CheckNoNdi.cmake\" RESULT_VARIABLE _lc_rc)
  if(_lc_rc)
    message(FATAL_ERROR \"Goi cai dat chua runtime/SDK NDI - khong duoc phan phoi (SPEC 5.1)\")
  endif()
")

set(CPACK_PACKAGE_NAME "LumaCast")
set(CPACK_PACKAGE_VENDOR "LumaCast")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "LumaCast - LED Display Controller")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "LumaCast")
set(CPACK_PACKAGE_EXECUTABLES "lumacast" "LumaCast")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/THIRD-PARTY-NOTICES.txt")      # trang dau cua trinh cai: thong bao ben thu ba (chua co EULA rieng)
set(CPACK_PACKAGE_FILE_NAME "LumaCast-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}")
set(LUMACAST_INSTALLER "NSIS" CACHE STRING "Trinh cai dat Windows: NSIS (mac dinh) | WIX | ZIP (ban mang theo)")
set_property(CACHE LUMACAST_INSTALLER PROPERTY STRINGS NSIS WIX ZIP)
if(WIN32)
  if(LUMACAST_INSTALLER STREQUAL "ZIP")
    set(CPACK_GENERATOR "ZIP")
  else()
    set(CPACK_GENERATOR "${LUMACAST_INSTALLER};ZIP")
  endif()
  set(CPACK_NSIS_DISPLAY_NAME "LumaCast")
  set(CPACK_NSIS_PACKAGE_NAME "LumaCast")
  set(CPACK_NSIS_INSTALLED_ICON_NAME "lumacast.exe")
  set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")                   # exe nam o goc thu muc cai dat (mac dinh CPack tim trong bin/)
  set(CPACK_NSIS_MODIFY_PATH OFF)
  set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
  set(CPACK_NSIS_MUI_FINISHPAGE_RUN "lumacast.exe")
  set(CPACK_NSIS_CREATE_ICONS_EXTRA "CreateShortCut '$DESKTOP\\\\LumaCast.lnk' '$INSTDIR\\\\lumacast.exe'")
  set(CPACK_NSIS_DELETE_ICONS_EXTRA "Delete '$DESKTOP\\\\LumaCast.lnk'")
  set(CPACK_WIX_UPGRADE_GUID "0F3A7C1E-5B7D-4C55-9B6B-6A3C2B2C8D11")                 # GUID co dinh cho WiX (doi neu fork)
  set(CPACK_WIX_PROGRAM_MENU_FOLDER "LumaCast")
else()
  set(CPACK_GENERATOR "TGZ")
endif()
include(CPack)

enable_testing()
add_test(NAME package COMMAND ${CMAKE_COMMAND} -DBUILD_DIR=${CMAKE_BINARY_DIR} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/TestInstall.cmake)

add_executable(lumacast_license_tests tests/test_license.cpp)
target_link_libraries(lumacast_license_tests PRIVATE lumacast_ui)
target_compile_definitions(lumacast_license_tests PRIVATE LUMACAST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME license COMMAND lumacast_license_tests)
