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

add_library(lumacast_core STATIC
  src/core/Channel.cpp
  src/core/Project.cpp
  src/core/Animation.cpp
  src/core/Scene.cpp
  src/core/Log.cpp
  src/core/Startup.h src/core/Startup.cpp
  src/render/GlHelpers.cpp
  src/render/Compositor.cpp
  src/render/Engine.h
  src/render/Engine.cpp
  src/render/OutputWindow.cpp
  src/render/OutputHealth.cpp
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
  src/live/LiveInput.h
  src/live/LiveSource.h src/live/LiveSource.cpp
  src/live/Ndi.h src/live/Ndi.cpp
  src/live/NdiFake.h src/live/NdiFake.cpp
  src/live/NdiRuntime.cpp
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
target_link_libraries(lumacast_core PUBLIC Qt6::Core Qt6::Gui Qt6::OpenGL Qt6::Network ${LC_FFMPEG})
if(WIN32)
  target_link_libraries(lumacast_core PUBLIC winmm ole32 avrt)   # winmm: timeBeginPeriod(1); ole32 + avrt: WASAPI (COM, MMCSS)
  target_link_libraries(lumacast_core PUBLIC mfplat mf mfreadwrite mfuuid)   # camera: Media Foundation (M5b); khong can Qt Multimedia
endif()

add_library(lumacast_ui STATIC
  src/app/Theme.cpp
  src/app/MonitorWidget.h src/app/MonitorWidget.cpp
  src/app/InspectorPanel.h src/app/InspectorPanel.cpp
  src/app/ParamEditor.h src/app/ParamEditor.cpp
  src/app/TimelineWidget.h src/app/TimelineWidget.cpp
  src/app/LockButton.h src/app/LockButton.cpp
  src/app/AudioPanel.h src/app/AudioPanel.cpp
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
add_test(NAME live COMMAND lumacast_live_tests)
