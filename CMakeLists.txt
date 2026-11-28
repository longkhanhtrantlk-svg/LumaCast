cmake_minimum_required(VERSION 3.21)
project(LumaCast VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL Network)
find_package(Qt6 REQUIRED COMPONENTS LinguistTools)   # lrelease: bien dich translations/*.ts
find_package(Qt6 QUIET COMPONENTS Test)      # chi cho test Multiview (go phim that qua he phim tat cua Qt)
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
  src/core/I18n.h src/core/I18n.cpp
  src/core/Update.h src/core/Update.cpp src/core/UpdateVelopack.cpp
  src/core/Channel.cpp
  src/core/Overlay.h src/core/Overlay.cpp
  src/core/CountdownAlarm.h src/core/CountdownAlarm.cpp
  src/audio/Tone.h src/audio/Tone.cpp
  src/render/OverlayRaster.h src/render/OverlayRaster.cpp
  src/render/OverlayCache.h src/render/OverlayCache.cpp
  src/core/Project.cpp
  src/core/ScreenSetup.h src/core/ScreenSetup.cpp
  src/core/Animation.cpp
  src/core/Scene.cpp
  src/core/Log.cpp
  src/core/Playlist.h src/core/Playlist.cpp
  src/core/Features.h src/core/Features.cpp
  src/core/Startup.h src/core/Startup.cpp
  src/render/GlHelpers.cpp
  src/render/Compositor.cpp
  src/render/SceneThumbs.h src/render/SceneThumbs.cpp
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
  src/live/WindowCapture.h src/live/WindowCapture.cpp
  src/live/WindowCaptureFake.h src/live/WindowCaptureFake.cpp
  src/live/WindowCaptureWgc.cpp
  src/live/SlideRemote.h src/live/SlideRemote.cpp src/live/SlideRemotePpt.cpp
  src/audio/AudioDrift.h
  src/render/NdiOutput.h src/render/NdiOutput.cpp
  src/control/HttpServer.h src/control/HttpServer.cpp
  src/control/Osc.h src/control/Osc.cpp
  src/control/ControlApi.h src/control/ControlApi.cpp
  src/control/ControlPage.h src/control/ControlPage.cpp
  src/control/ControlService.h src/control/ControlService.cpp
  src/control/LiveActions.h src/control/LiveActions.cpp
  src/control/Midi.h src/control/Midi.cpp src/control/MidiWinmm.cpp
)
target_include_directories(lumacast_core PUBLIC src)
# Ngon ngu giao dien: chu goc tieng Viet; ban tieng Anh bien dich vao file thuc thi (resource :/i18n/lumacast_en.qm)
set(LC_QM ${CMAKE_CURRENT_BINARY_DIR}/lumacast_en.qm)
add_custom_command(OUTPUT ${LC_QM}
  COMMAND Qt6::lrelease -silent ${CMAKE_CURRENT_SOURCE_DIR}/translations/lumacast_en.ts -qm ${LC_QM}
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/translations/lumacast_en.ts VERBATIM)
set_source_files_properties(${LC_QM} PROPERTIES QT_RESOURCE_ALIAS lumacast_en.qm)
qt_add_resources(lumacast_core lumacast_i18n PREFIX /i18n FILES ${LC_QM})
if(NOT LUMACAST_ENABLE_NDI)
  target_compile_definitions(lumacast_core PUBLIC LUMACAST_NO_NDI=1)
endif()
# Tu cap nhat (Velopack): can thu vien velopack_libc (giai nen vao mot thu muc co include/ va lib/). Khong co thi ban dich van chay,
# chi khong tu cap nhat. LUMACAST_UPDATE_URL: noi lay ban moi mac dinh (kho GitHub, URL web hoac thu muc).
set(LUMACAST_VELOPACK_DIR "C:/velopack/1.2.161" CACHE PATH "Thu muc velopack_libc (include/Velopack.h, lib/*.dll.lib)")
set(LUMACAST_UPDATE_URL "https://github.com/longkhanhtrantlk-svg/LumaCast" CACHE STRING "Noi lay ban cap nhat mac dinh")
set_source_files_properties(src/core/Update.cpp PROPERTIES COMPILE_DEFINITIONS "LUMACAST_UPDATE_URL=\"${LUMACAST_UPDATE_URL}\"")
if(WIN32 AND EXISTS "${LUMACAST_VELOPACK_DIR}/include/Velopack.h" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(LC_VELOPACK_DLL "${LUMACAST_VELOPACK_DIR}/lib/velopack_libc_win_x64_msvc.dll")
  target_include_directories(lumacast_core PRIVATE "${LUMACAST_VELOPACK_DIR}/include")
  target_link_libraries(lumacast_core PUBLIC "${LC_VELOPACK_DLL}.lib")
  target_compile_definitions(lumacast_core PRIVATE LUMACAST_HAS_VELOPACK=1)
  # thu vien nhap tro toi "velopack_libc.dll" (khac ten file trong goi tai ve) -> doi ten khi chep / cai
  file(COPY_FILE "${LC_VELOPACK_DLL}" "${CMAKE_BINARY_DIR}/velopack_libc.dll" ONLY_IF_DIFFERENT)     # canh exe + file test
  message(STATUS "Tu cap nhat: Velopack tai ${LUMACAST_VELOPACK_DIR}")
else()
  message(STATUS "Tu cap nhat: KHONG dich kem (khong thay ${LUMACAST_VELOPACK_DIR}/include/Velopack.h)")
endif()
target_link_libraries(lumacast_core PUBLIC Qt6::Core Qt6::Gui Qt6::OpenGL Qt6::Network ${LC_FFMPEG})
if(WIN32)
  target_link_libraries(lumacast_core PUBLIC winmm ole32 avrt)   # winmm: timeBeginPeriod(1); ole32 + avrt: WASAPI (COM, MMCSS)
  target_link_libraries(lumacast_core PUBLIC mfplat mf mfreadwrite mfuuid)   # camera: Media Foundation (M5b); khong can Qt Multimedia
  target_link_libraries(lumacast_core PUBLIC windowsapp d3d11 dxgi dwmapi oleaut32 user32)   # bat cua so (SRC-09) + PowerPoint qua COM (CTL-02)
endif()

add_library(lumacast_ui STATIC
  src/app/Theme.cpp
  src/app/AboutInfo.h src/app/AboutInfo.cpp
  src/app/MonitorWidget.h src/app/MonitorWidget.cpp
  src/app/PreviewMonitor.h src/app/PreviewMonitor.cpp
  src/app/NowShowing.h src/app/NowShowing.cpp
  src/app/DragList.h src/app/DragList.cpp
  src/app/DockChrome.h src/app/DockChrome.cpp
  src/app/Ui.h src/app/Ui.cpp src/app/Text.h
  src/app/MainWindowLayouts.cpp
  src/app/OverlayPanel.h src/app/OverlayPanel.cpp
  src/app/MultiviewWindow.h src/app/MultiviewWindow.cpp
  src/app/InspectorPanel.h src/app/InspectorPanel.cpp
  src/app/MappingWindow.h src/app/MappingWindow.cpp
  src/app/ParamEditor.h src/app/ParamEditor.cpp
  src/app/TimelineWidget.h src/app/TimelineWidget.cpp
  src/app/LockButton.h src/app/LockButton.cpp
  src/app/InspectorWall.cpp
  src/app/AudioPanel.h src/app/AudioPanel.cpp
  src/app/LiveDialogs.h src/app/LiveDialogs.cpp
  src/app/SettingsDialog.h src/app/SettingsDialog.cpp
  src/app/PlaylistPanel.h src/app/PlaylistPanel.cpp
  src/app/Icons.h src/app/Icons.cpp
  src/app/ScenePanel.h src/app/ScenePanel.cpp
  src/app/ScreenDialog.h src/app/ScreenDialog.cpp
  src/app/MainWindow.h src/app/MainWindow.cpp
  src/app/MainWindowInput.cpp
  src/app/MainWindowUpdate.cpp
  src/app/UpdateDialog.h src/app/UpdateDialog.cpp
  src/app/Shortcuts.h src/app/Shortcuts.cpp
  src/app/InputDialog.h src/app/InputDialog.cpp
)
target_include_directories(lumacast_ui PUBLIC src src/app)
target_link_libraries(lumacast_ui PUBLIC lumacast_core Qt6::Widgets)
# Icon (nguon: resources/icon/lumacast.svg; doi icon thi chay resources/icon/make_icon.py)
qt_add_resources(lumacast_ui lumacast_icon PREFIX /icon BASE resources/icon FILES resources/icon/lumacast.ico)

add_executable(lumacast src/app/main.cpp)
if(WIN32)
  target_sources(lumacast PRIVATE resources/icon/lumacast.rc)
  set_source_files_properties(resources/icon/lumacast.rc PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/resources/icon/lumacast.ico)
endif()
target_link_libraries(lumacast PRIVATE lumacast_ui)
target_compile_definitions(lumacast PRIVATE LUMACAST_VERSION="${PROJECT_VERSION}")
# plug-in mau (xem docs/PLUGIN-SDK.md) di kem file thuc thi: <exe>/plugins/
add_custom_command(TARGET lumacast POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/plugins $<TARGET_FILE_DIR:lumacast>/plugins)

add_executable(lumacast_tests tests/test_render.cpp)
target_link_libraries(lumacast_tests PRIVATE lumacast_core)

enable_testing()
add_test(NAME render COMMAND lumacast_tests)

# Bu khe video wall (OUT-04)
add_executable(lumacast_wall_tests tests/test_wall.cpp)
target_link_libraries(lumacast_wall_tests PRIVATE lumacast_ui)
target_compile_definitions(lumacast_wall_tests PRIVATE LUMACAST_EXE="$<TARGET_FILE:lumacast>")
add_dependencies(lumacast_wall_tests lumacast)
add_test(NAME wall COMMAND lumacast_wall_tests)

add_executable(lumacast_ui_tests tests/test_ui.cpp)
target_link_libraries(lumacast_ui_tests PRIVATE lumacast_ui)
add_test(NAME ui COMMAND lumacast_ui_tests)

# SRC-08: lop phu chu / logo
add_executable(lumacast_overlay_tests tests/test_overlay.cpp)
target_link_libraries(lumacast_overlay_tests PRIVATE lumacast_ui)
add_test(NAME overlay COMMAND lumacast_overlay_tests)

add_executable(lumacast_m3c_tests tests/test_m3c.cpp)
target_link_libraries(lumacast_m3c_tests PRIVATE lumacast_ui)
add_test(NAME reliability COMMAND lumacast_m3c_tests)

if(TARGET Qt6::Test)
  add_executable(lumacast_multiview_tests tests/test_multiview.cpp)
  target_link_libraries(lumacast_multiview_tests PRIVATE lumacast_ui Qt6::Test)
  add_test(NAME multiview COMMAND lumacast_multiview_tests)
else()
  message(WARNING "Khong co Qt6 Test - bo qua test Multiview")
endif()

add_executable(lumacast_effects_tests tests/test_effects.cpp)
target_link_libraries(lumacast_effects_tests PRIVATE lumacast_core)
target_compile_definitions(lumacast_effects_tests PRIVATE LUMACAST_PLUGINS_DIR="${CMAKE_SOURCE_DIR}/plugins")
add_test(NAME effects COMMAND lumacast_effects_tests)

add_executable(lumacast_anim_tests tests/test_anim.cpp)
target_link_libraries(lumacast_anim_tests PRIVATE lumacast_core)
add_test(NAME animation COMMAND lumacast_anim_tests)

# Playlist / slide anh tu chuyen (CHN-10)
add_executable(lumacast_playlist_tests tests/test_playlist.cpp)
target_link_libraries(lumacast_playlist_tests PRIVATE lumacast_ui)
add_test(NAME playlist COMMAND lumacast_playlist_tests)

add_executable(lumacast_control_tests tests/test_control.cpp)
target_link_libraries(lumacast_control_tests PRIVATE lumacast_ui)
add_test(NAME control COMMAND lumacast_control_tests)

# Phim tat tu dat (UI-07) va MIDI (CTL-01, backend gia)
find_package(Qt6 REQUIRED COMPONENTS Test)                  # QTest::keyClick: bom phim qua dung duong QShortcut (chi cho test)
add_executable(lumacast_input_tests tests/test_input.cpp)
target_link_libraries(lumacast_input_tests PRIVATE lumacast_ui Qt6::Test)
add_test(NAME input COMMAND lumacast_input_tests)

# Bat cua so slideshow (SRC-09) va chuyen slide PowerPoint (CTL-02)
add_executable(lumacast_window_tests tests/test_window.cpp)
target_link_libraries(lumacast_window_tests PRIVATE lumacast_ui Qt6::Test)
add_test(NAME window COMMAND lumacast_window_tests)

add_executable(lumacast_m5c_tests tests/test_m5c.cpp)
target_link_libraries(lumacast_m5c_tests PRIVATE lumacast_ui)
target_compile_definitions(lumacast_m5c_tests PRIVATE LUMACAST_EXE="$<TARGET_FILE:lumacast>")
add_dependencies(lumacast_m5c_tests lumacast)
add_test(NAME ops COMMAND lumacast_m5c_tests)

# An toan khi dang dien (Enter, video phat tu dau, dong Output, thieu file)
add_executable(lumacast_safety_tests tests/test_safety.cpp)
target_link_libraries(lumacast_safety_tests PRIVATE lumacast_ui)
add_test(NAME safety COMMAND lumacast_safety_tests)

# Bo cuc cua so chinh: vua man hinh nho, khung khong de len nhau
add_executable(lumacast_layout_tests tests/test_layout.cpp)
target_link_libraries(lumacast_layout_tests PRIVATE lumacast_ui)
add_test(NAME layout COMMAND lumacast_layout_tests)

# Danh sach Scenes (UI-02): phat/tua video, tieng rieng, doi ten/xoa, mot file mot Scene
add_executable(lumacast_scenes_tests tests/test_scenes.cpp)
target_link_libraries(lumacast_scenes_tests PRIVATE lumacast_ui)
add_test(NAME scenes COMMAND lumacast_scenes_tests)

# Thiet lap man hinh (UI-08): vung LED, them/bot Output, khung hinh, video wall, project moi
add_executable(lumacast_screens_tests tests/test_screens.cpp)
target_link_libraries(lumacast_screens_tests PRIVATE lumacast_ui)
add_test(NAME screens COMMAND lumacast_screens_tests)

# Mapping truc quan
add_executable(lumacast_mapping_tests tests/test_mapping.cpp)
target_link_libraries(lumacast_mapping_tests PRIVATE lumacast_ui)
add_test(NAME mapping COMMAND lumacast_mapping_tests)

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
# Bo cai Windows (tu cap nhat): package_lumacast.bat -> cmake/Package.cmake (vpk + packaging/installer.nsi). CPack: ban ZIP mang theo.
# KHONG BAO GIO kem runtime NDI (SPEC 5.1): buoc cuoi cua cai dat chay cmake/CheckNoNdi.cmake va dung neu thay.
if(WIN32)
  set(LC_INSTALL_BIN ".")
  set(LC_INSTALL_DOC ".")
else()
  set(LC_INSTALL_BIN "bin")
  set(LC_INSTALL_DOC "share/doc/lumacast")
endif()
install(TARGETS lumacast RUNTIME DESTINATION ${LC_INSTALL_BIN})
if(LC_VELOPACK_DLL)
  install(FILES "${LC_VELOPACK_DLL}" DESTINATION ${LC_INSTALL_BIN} RENAME velopack_libc.dll)
endif()
install(DIRECTORY plugins/ DESTINATION ${LC_INSTALL_BIN}/plugins PATTERN "*.md" EXCLUDE)       # plug-in mau canh file thuc thi: <exe>/plugins/
install(FILES THIRD-PARTY-NOTICES.txt THIRD-PARTY-NOTICES.en.txt README.md license.example.json DESTINATION ${LC_INSTALL_DOC})
install(DIRECTORY docs/ DESTINATION ${LC_INSTALL_DOC}/docs)
install(DIRECTORY licenses/ DESTINATION ${LC_INSTALL_DOC}/licenses)

if(WIN32)
  # --- Qt: chay windeployqt tren thu muc cai dat ---
  # KHONG dung qt_generate_deploy_app_script: no goi windeployqt voi --libdir bin (DLL Qt vao <goi>/bin/) trong khi exe nam o goc goi ->
  # "Qt6Widgets.dll was not found" luc chay. O day DLL Qt + plug-in Qt nam CANH exe (plugins/ cho plug-in Qt: platforms, styles...).
  option(LUMACAST_DEPLOY_QT "Dong goi Qt bang windeployqt luc cai dat (tat khi chi muon thu cau hinh goi)" ON)
  if(LUMACAST_DEPLOY_QT)
    if(TARGET Qt6::windeployqt)
      install(CODE "
        execute_process(COMMAND [[$<TARGET_FILE:Qt6::windeployqt>]] [[$<TARGET_FILE:lumacast>]]
                        --dir \"\${CMAKE_INSTALL_PREFIX}\" --plugindir \"\${CMAKE_INSTALL_PREFIX}/plugins\" --no-translations --no-opengl-sw
                        --no-compiler-runtime
                        RESULT_VARIABLE _lc_wdq_rc OUTPUT_VARIABLE _lc_wdq_out ERROR_VARIABLE _lc_wdq_err)
        message(STATUS \"windeployqt: \${_lc_wdq_out}\")
        if(_lc_wdq_rc)
          message(FATAL_ERROR \"windeployqt that bai (\${_lc_wdq_rc}): \${_lc_wdq_err}\")
        endif()
      ")
    else()
      message(WARNING "Khong co target Qt6::windeployqt - chay windeployqt tay truoc khi dong goi")
    endif()
  endif()
  # --- Visual C++ runtime: chep DLL CANH exe (app-local) de may sach khong co mang van cai va chay duoc ---
  if(MSVC)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
    include(InstallRequiredSystemLibraries)
    if(NOT CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
      message(FATAL_ERROR "Khong tim thay DLL Visual C++ runtime de dong goi (can thanh phan Redist cua Visual Studio)")
    endif()
    install(FILES ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} DESTINATION ${LC_INSTALL_BIN})
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
           PRE_EXCLUDE_REGEXES [[api-ms-.*]] [[ext-ms-.*]] POST_EXCLUDE_REGEXES [[.*[Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\][Ss][Yy][Ss][Tt][Ee][Mm]32.*]])
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
set(CPACK_PACKAGE_FILE_NAME "LumaCast-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}")
if(WIN32)
  set(CPACK_GENERATOR "ZIP")
else()
  set(CPACK_GENERATOR "TGZ")
endif()
include(CPack)

enable_testing()
add_test(NAME package COMMAND ${CMAKE_COMMAND} -DBUILD_DIR=${CMAKE_BINARY_DIR} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/TestInstall.cmake)
add_test(NAME installer COMMAND ${CMAKE_COMMAND} -DBUILD_DIR=${CMAKE_BINARY_DIR} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/TestInstaller.cmake)

add_executable(lumacast_license_tests tests/test_license.cpp)
target_link_libraries(lumacast_license_tests PRIVATE lumacast_ui)
target_compile_definitions(lumacast_license_tests PRIVATE LUMACAST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME license COMMAND lumacast_license_tests)

# Dot 6: ngon ngu giao dien (bang dich day du, khong sot chu, giao dien tieng Anh)
add_executable(lumacast_i18n_tests tests/test_i18n.cpp)
target_link_libraries(lumacast_i18n_tests PRIVATE lumacast_ui)
target_compile_definitions(lumacast_i18n_tests PRIVATE LUMACAST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME i18n COMMAND lumacast_i18n_tests)
# nhan tieng Anh dai hon khong duoc lam vo bo cuc
add_test(NAME layout_en COMMAND lumacast_layout_tests en)

# Tu cap nhat (backend gia, khong dung mang)
add_executable(lumacast_update_tests tests/test_update.cpp)
target_link_libraries(lumacast_update_tests PRIVATE lumacast_ui Qt6::Test)
target_compile_definitions(lumacast_update_tests PRIVATE LUMACAST_EXE="$<TARGET_FILE:lumacast>")
add_dependencies(lumacast_update_tests lumacast)
add_test(NAME update COMMAND lumacast_update_tests)
# thu tay T-UPD: goi backend that trong mot ban da cai (khong cai kem, khong phai ctest)
add_executable(lumacast_update_probe tests/update_probe.cpp)
target_link_libraries(lumacast_update_probe PRIVATE lumacast_core)

# Icon ung dung: du cac co, dung hinh, nam trong lumacast.exe (khong mo cua so)
add_executable(lumacast_icon_tests tests/test_icon.cpp)
target_link_libraries(lumacast_icon_tests PRIVATE lumacast_ui)
target_compile_definitions(lumacast_icon_tests PRIVATE LUMACAST_EXE="$<TARGET_FILE:lumacast>")
add_dependencies(lumacast_icon_tests lumacast)
add_test(NAME icon COMMAND lumacast_icon_tests)

# Dot 4 + 5: tieng nhac dem nguoc, keo tha, dai "Dang hien", keo overlay / hinh, tooltip
add_executable(lumacast_ux_tests tests/test_ux.cpp)
target_link_libraries(lumacast_ux_tests PRIVATE lumacast_ui)
add_test(NAME ux COMMAND lumacast_ux_tests)
