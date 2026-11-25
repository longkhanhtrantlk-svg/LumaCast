# Kiem thu goi cai dat (ctest "package"): cai vao thu muc tam, kiem tra bo file, va kiem tra rao chan NDI.
#   cmake -DBUILD_DIR=<build> -DSOURCE_DIR=<repo> -P cmake/TestInstall.cmake
set(_prefix "${BUILD_DIR}/_pkgtest_prefix")
file(REMOVE_RECURSE "${_prefix}")
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --prefix "${_prefix}" RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(_rc)
  message(FATAL_ERROR "cmake --install that bai (${_rc}):\n${_out}\n${_err}")
endif()
# CMAKE_EXECUTABLE_SUFFIX khong duoc dat o che do script (-P) -> tu chon hau to theo nen tang
if(WIN32)
  set(_exe ".exe")
  set(_bin "${_prefix}")
  set(_doc "${_prefix}")
else()
  set(_exe "")
  set(_bin "${_prefix}/bin")
  set(_doc "${_prefix}/share/doc/lumacast")
endif()
set(_need
  "${_bin}/lumacast${_exe}"
  "${_bin}/plugins/mirror/plugin.json" "${_bin}/plugins/ripple/plugin.json" "${_bin}/plugins/spin-wipe/plugin.json" "${_bin}/plugins/vignette/plugin.json"
  "${_doc}/THIRD-PARTY-NOTICES.txt" "${_doc}/README.md" "${_doc}/license.example.json" "${_doc}/docs/SPEC.md")
foreach(_f ${_need})
  if(NOT EXISTS "${_f}")
    message(FATAL_ERROR "Goi cai dat thieu: ${_f}")
  endif()
endforeach()
# bo kiem thu/benchmark khong duoc nam trong ban cai
file(GLOB _tests "${_bin}/lumacast_*tests*")
if(_tests)
  message(FATAL_ERROR "Goi cai dat khong duoc chua file kiem thu: ${_tests}")
endif()
# 1) goi sach: CheckNoNdi qua
execute_process(COMMAND "${CMAKE_COMMAND}" -DDIR=${_prefix} -P "${SOURCE_DIR}/cmake/CheckNoNdi.cmake" RESULT_VARIABLE _rc2 OUTPUT_QUIET ERROR_QUIET)
if(_rc2)
  message(FATAL_ERROR "CheckNoNdi bao loi tren goi sach")
endif()
# 2) co file NDI -> phai THAT BAI
foreach(_ndi "Processing.NDI.Lib.x64.dll" "libndi.so.5" "Processing.NDI.Lib.Advanced.x64.dll")
  file(WRITE "${_prefix}/${_ndi}" "gia")
  execute_process(COMMAND "${CMAKE_COMMAND}" -DDIR=${_prefix} -P "${SOURCE_DIR}/cmake/CheckNoNdi.cmake" RESULT_VARIABLE _rc3 OUTPUT_QUIET ERROR_QUIET)
  if(NOT _rc3)
    message(FATAL_ERROR "CheckNoNdi KHONG bat duoc ${_ndi}")
  endif()
  file(REMOVE "${_prefix}/${_ndi}")
endforeach()
file(REMOVE_RECURSE "${_prefix}")
message(STATUS "package: OK (bo file day du, khong co file kiem thu, rao chan NDI hoat dong)")
