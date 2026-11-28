; Bo cai LumaCast: hoi Tieng Viet / English ngay dau, hien thong bao ben thu ba theo ngon ngu do, roi chay bo cai cua
; Velopack o che do im lang (cai vao %LocalAppData%\LumaCast, khong can quyen admin, tu cap nhat duoc).
; Dung bang cmake/Package.cmake (package_lumacast.bat). KHONG co dieu khoan su dung: trang chi la thong bao ben thu ba.
; Go cai: Velopack tu dang ky muc "LumaCast" trong Windows Settings.
Unicode true

!ifndef VERSION
  !error "thieu /DVERSION"
!endif
!ifndef SETUP
  !error "thieu /DSETUP=<LumaCast-...-Setup.exe cua vpk>"
!endif
!ifndef OUTFILE
  !error "thieu /DOUTFILE"
!endif
!ifndef NOTICES_VI
  !error "thieu /DNOTICES_VI"
!endif
!ifndef NOTICES_EN
  !error "thieu /DNOTICES_EN"
!endif

Name "LumaCast"
Caption "LumaCast ${VERSION}"
OutFile "${OUTFILE}"
RequestExecutionLevel user
ShowInstDetails nevershow
SetCompress off                       ; Setup.exe cua Velopack da nen san
BrandingText "LumaCast ${VERSION}"

!include "MUI2.nsh"
!include "LogicLib.nsh"

!define MUI_ICON "${__FILEDIR__}\..\resources\icon\lumacast.ico"
!define MUI_ABORTWARNING
!define MUI_LANGDLL_ALWAYSSHOW
!define MUI_LANGDLL_ALLLANGUAGES
!define MUI_LANGDLL_WINDOWTITLE "LumaCast"
!define MUI_LANGDLL_INFO "Chọn ngôn ngữ / Select a language"
!define MUI_LANGDLL_REGISTRY_ROOT HKCU
!define MUI_LANGDLL_REGISTRY_KEY "Software\LumaCast\Installer"
!define MUI_LANGDLL_REGISTRY_VALUENAME "Language"

!insertmacro MUI_PAGE_LICENSE "$(lcNotices)"
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_FUNCTION LaunchApp
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_LANGUAGE "Vietnamese"
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_RESERVEFILE_LANGDLL

LicenseLangString lcNotices ${LANG_VIETNAMESE} "${NOTICES_VI}"
LicenseLangString lcNotices ${LANG_ENGLISH} "${NOTICES_EN}"
LangString lcRunning ${LANG_VIETNAMESE} "LumaCast đang chạy. Hãy đóng LumaCast rồi chạy lại bộ cài.$\n$\n(Bộ cài không tự đóng phần mềm để tránh tắt hình giữa buổi diễn.)"
LangString lcRunning ${LANG_ENGLISH} "LumaCast is running. Close it, then run the installer again.$\n$\n(The installer never closes it by itself, so a show cannot be cut off.)"
LangString lcOldInstall ${LANG_VIETNAMESE} "Máy đang có bản LumaCast kiểu cũ (cài trong Program Files, không tự cập nhật được).$\n$\nGỡ bản cũ ngay bây giờ? Windows sẽ hỏi quyền quản trị. Project và cài đặt được giữ."
LangString lcOldInstall ${LANG_ENGLISH} "An older LumaCast (installed in Program Files, without self-update) is on this machine.$\n$\nUninstall it now? Windows will ask for administrator rights. Projects and settings are kept."
LangString lcInstalling ${LANG_VIETNAMESE} "Đang cài LumaCast, vui lòng chờ..."
LangString lcInstalling ${LANG_ENGLISH} "Installing LumaCast, please wait..."
LangString lcFailed ${LANG_VIETNAMESE} "Cài đặt không thành công (mã $1). Xem nhật ký trong %LocalAppData%\LumaCast."
LangString lcFailed ${LANG_ENGLISH} "Installation failed (code $1). See the logs in %LocalAppData%\LumaCast."

Function .onInit
  StrCpy $LANGUAGE ${LANG_VIETNAMESE}              ; chon san Tieng Viet
  IfSilent 0 lcAsk
    ReadRegStr $0 HKCU "Software\LumaCast\Installer" "Language"      ; cai im lang: giu ngon ngu lan truoc
    StrCmp $0 "" lcAsked
    StrCpy $LANGUAGE $0
    Goto lcAsked
  lcAsk:
    !insertmacro MUI_LANGDLL_DISPLAY
  lcAsked:

  nsExec::ExecToStack '"$SYSDIR\cmd.exe" /c tasklist /FI "IMAGENAME eq lumacast.exe" /NH | "$SYSDIR\find.exe" /I /C "lumacast.exe"'
  Pop $0
  Pop $1
  ${If} $0 == 0                                    ; find tra 0 khi thay it nhat mot dong
    MessageBox MB_OK|MB_ICONEXCLAMATION "$(lcRunning)" /SD IDOK
    SetErrorLevel 2
    Abort
  ${EndIf}
FunctionEnd

Section
  ; Ban cai kieu cu (CPack NSIS, ca may): hai ban song song de gay nham -> de nghi go
  ReadRegStr $0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\LumaCast" "UninstallString"
  ${If} $0 != ""
    MessageBox MB_YESNO|MB_ICONQUESTION "$(lcOldInstall)" /SD IDNO IDNO lcKeepOld
    StrCpy $2 $0 1
    ${If} $2 == '"'
      StrCpy $0 $0 "" 1
      StrCpy $0 $0 -1
    ${EndIf}
    ExecShellWait "open" "$0"
  lcKeepOld:
  ${EndIf}

  ; Ghi ngon ngu TRUOC khi cai: lan mo app dau tien dung ngay ngon ngu nay (lua chon trong app van uu tien hon)
  StrCpy $2 "vi"
  ${If} $LANGUAGE == ${LANG_ENGLISH}
    StrCpy $2 "en"
  ${EndIf}
  WriteRegStr HKCU "Software\LumaCast\LumaCast\ui" "installLanguage" "$2"
  WriteRegStr HKCU "Software\LumaCast\Installer" "Language" "$LANGUAGE"

  InitPluginsDir
  File "/oname=$PLUGINSDIR\LumaCast-Setup.exe" "${SETUP}"
  DetailPrint "$(lcInstalling)"
  Banner::show /NOUNLOAD "$(lcInstalling)"
  ExecWait '"$PLUGINSDIR\LumaCast-Setup.exe" --silent' $1
  Banner::destroy
  ${If} $1 != 0
    MessageBox MB_OK|MB_ICONSTOP "$(lcFailed)" /SD IDOK
    SetErrorLevel 1
    Abort
  ${EndIf}
SectionEnd

Function LaunchApp
  Exec '"$LOCALAPPDATA\LumaCast\current\lumacast.exe"'
FunctionEnd
