# Trang thong bao ben thu ba cua bo cai: NSIS chi doc dung chu co dau khi file co BOM UTF-8; o van ban tren Windows can CRLF.
function(lc_installer_notices source target)
  file(READ "${source}" _text)
  string(ASCII 239 187 191 _bom)
  string(REPLACE "${_bom}" "" _text "${_text}")
  string(REPLACE "\r\n" "\n" _text "${_text}")
  string(REPLACE "\n" "\r\n" _text "${_text}")
  file(CONFIGURE OUTPUT "${target}" CONTENT "${_bom}@_text@" @ONLY NEWLINE_STYLE LF)
endfunction()
