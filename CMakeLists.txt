cmake_minimum_required(VERSION 3.21)
project(LumaCast VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL Network)
# FFmpeg: Linux/macOS dung pkg-config; Windows dung vcpkg (`vcpkg install ffmpeg:x64-windows`, ban LGPL, lien ket dong).
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(FFMPEG QUIET IMPORTED_TARGET libavformat libavcodec libswscale libavutil)
endif()
if(TARGET PkgConfig::FFMPEG)
  set(LC_FFMPEG PkgConfig::FFMPEG)
else()
  find_package(FFMPEG REQUIRED)
  add_library(lc_ffmpeg INTERFACE)
  target_include_directories(lc_ffmpeg INTERFACE ${FFMPEG_INCLUDE_DIRS})
  target_link_directories(lc_ffmpeg INTERFACE ${FFMPEG_LIBRARY_DIRS})
  target_link_libraries(lc_ffmpeg INTERFACE ${FFMPEG_LIBRARIES})
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
  src/media/StaticSources.cpp
  src/media/FFmpegSource.cpp
  src/control/HttpServer.h src/control/HttpServer.cpp
  src/control/Osc.h src/control/Osc.cpp
  src/control/ControlApi.h src/control/ControlApi.cpp
  src/control/ControlPage.h src/control/ControlPage.cpp
  src/control/ControlService.h src/control/ControlService.cpp
)
target_include_directories(lumacast_core PUBLIC src)
target_link_libraries(lumacast_core PUBLIC Qt6::Core Qt6::Gui Qt6::OpenGL Qt6::Network ${LC_FFMPEG})
if(WIN32)
  target_link_libraries(lumacast_core PUBLIC winmm)   # timeBeginPeriod(1) cho nhip 60fps chinh xac
endif()

add_library(lumacast_ui STATIC
  src/app/Theme.cpp
  src/app/MonitorWidget.h src/app/MonitorWidget.cpp
  src/app/InspectorPanel.h src/app/InspectorPanel.cpp
  src/app/ParamEditor.h src/app/ParamEditor.cpp
  src/app/TimelineWidget.h src/app/TimelineWidget.cpp
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
