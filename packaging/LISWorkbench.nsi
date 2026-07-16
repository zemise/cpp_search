Unicode false

!ifndef APP_NAME
!define APP_NAME "LIS Workbench"
!endif
!ifndef APP_VERSION
!define APP_VERSION "v2026.07.16"
!endif
!ifndef APP_PUBLISHER
!define APP_PUBLISHER "Zhao Wang"
!endif
!ifndef APP_EXE
!define APP_EXE "lis_workbench.exe"
!endif
!ifndef BUILD_DIR
!define BUILD_DIR "..\build\windows-x64"
!endif
!ifndef OUTPUT_DIR
!define OUTPUT_DIR "..\out\windows\package-work"
!endif
!ifndef OUTPUT_NAME
!define OUTPUT_NAME "LISWorkbench-Setup.exe"
!endif

Name "${APP_NAME}"
OutFile "${OUTPUT_DIR}\${OUTPUT_NAME}"
InstallDir "$PROGRAMFILES64\LISWorkbench"
RequestExecutionLevel admin
CRCCheck on
XPStyle on
Icon "..\resource\app.ico"
UninstallIcon "..\resource\app.ico"

Page directory
Page instfiles

UninstPage uninstConfirm
UninstPage instfiles

Section "Install"
  SetOutPath "$INSTDIR"
  ; Remove stale bundled CRT/UCRT DLLs from older packages. New Release builds
  ; link the MSVC runtime statically, and newer CRT DLLs can break Windows 7.
  Delete "$INSTDIR\MSVCP*.dll"
  Delete "$INSTDIR\VCRUNTIME*.dll"
  Delete "$INSTDIR\CONCRT*.dll"
  Delete "$INSTDIR\ucrtbase.dll"
  Delete "$INSTDIR\api-ms-win-crt-*.dll"
  File "${BUILD_DIR}\${APP_EXE}"
  !if /FileExists "${BUILD_DIR}\Updater.exe"
    File "${BUILD_DIR}\Updater.exe"
  !endif
  !if /FileExists "${BUILD_DIR}\*.dll"
    File "${BUILD_DIR}\*.dll"
  !endif
  !if /FileExists "${BUILD_DIR}\*.pdb"
    File "${BUILD_DIR}\*.pdb"
  !endif
  !ifdef VC_REDIST_DIR
    !if /FileExists "${VC_REDIST_DIR}\*.dll"
      File "${VC_REDIST_DIR}\*.dll"
    !endif
  !endif
  SetOutPath "$INSTDIR"
  SetOverwrite off
  IfFileExists "$INSTDIR\ClientConfig.ini" config_done 0
  IfFileExists "$INSTDIR\result_search.ini" copy_legacy_config create_config
copy_legacy_config:
    CopyFiles /SILENT "$INSTDIR\result_search.ini" "$INSTDIR\ClientConfig.ini"
    Goto config_done
create_config:
    FileOpen $0 "$INSTDIR\ClientConfig.ini" w
    FileClose $0
config_done:
  SetOverwrite on

  CreateDirectory "$SMPROGRAMS\${APP_NAME}"
  CreateShortcut "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"
  CreateShortcut "$DESKTOP\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"
  CreateShortcut "$SMPROGRAMS\${APP_NAME}\Uninstall ${APP_NAME}.lnk" "$INSTDIR\Uninstall.exe"

  WriteUninstaller "$INSTDIR\Uninstall.exe"

  ; Allow normal users to update local ini/config files.
  ExecWait '"$SYSDIR\icacls.exe" "$INSTDIR" /grant *S-1-5-32-545:M /C'
  ExecWait '"$SYSDIR\icacls.exe" "$INSTDIR" /grant *S-1-5-32-545:(OI)(CI)M /T /C'
SectionEnd

Section "Uninstall"
  Delete "$DESKTOP\${APP_NAME}.lnk"
  Delete "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk"
  Delete "$SMPROGRAMS\${APP_NAME}\Uninstall ${APP_NAME}.lnk"
  RMDir "$SMPROGRAMS\${APP_NAME}"

  Delete "$INSTDIR\${APP_EXE}"
  Delete "$INSTDIR\Updater.exe"
  Delete "$INSTDIR\MSVCP*.dll"
  Delete "$INSTDIR\VCRUNTIME*.dll"
  Delete "$INSTDIR\CONCRT*.dll"
  Delete "$INSTDIR\ucrtbase.dll"
  Delete "$INSTDIR\api-ms-win-crt-*.dll"
  Delete "$INSTDIR\templates\HIVStatisticsTemplate.docx"
  RMDir "$INSTDIR\templates"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
SectionEnd
