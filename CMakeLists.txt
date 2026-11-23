cmake_minimum_required(VERSION 3.21)
project(LumaCast VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL)
find_package(PkgConfig REQUIRED)
# Windows: cai FFmpeg qua vcpkg (ffmpeg[core,avcodec,avformat,swscale]) -- ban LGPL, lien ket dong.
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libswscale libavutil)

add_library(lumacast_core STATIC
  src/core/Channel.cpp
  src/render/GlHelpers.cpp
  src/render/Compositor.cpp
  src/render/Engine.h
  src/render/Engine.cpp
  src/render/OutputWindow.cpp
  src/media/StaticSources.cpp
  src/media/FFmpegSource.cpp
)
target_include_directories(lumacast_core PUBLIC src)
target_link_libraries(lumacast_core PUBLIC Qt6::Core Qt6::Gui Qt6::OpenGL PkgConfig::FFMPEG)
if(WIN32)
  target_link_libraries(lumacast_core PUBLIC winmm)   # timeBeginPeriod(1) cho nhip 60fps chinh xac
endif()

add_executable(lumacast src/app/main.cpp src/app/ControlPanel.h src/app/ControlPanel.cpp)
target_link_libraries(lumacast PRIVATE lumacast_core Qt6::Widgets)

add_executable(lumacast_tests tests/test_render.cpp)
target_link_libraries(lumacast_tests PRIVATE lumacast_core)

enable_testing()
add_test(NAME render COMMAND lumacast_tests)
