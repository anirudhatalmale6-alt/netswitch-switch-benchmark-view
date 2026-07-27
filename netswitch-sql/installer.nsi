; installer.nsi — Windows installer for the NetSwitch (Microsoft SQL Server) client.
; Build:  makensis installer.nsi   ->  NetSwitch-SQL-Setup.exe
;
; Installs netswitch_sql.exe into Program Files\NetSwitch, adds Start-Menu + Desktop
; shortcuts (a console opens in the install folder so the operator can run the connector
; and go through the forced first-use password change), registers an uninstaller, and
; appears in "Apps & features".

!include "MUI2.nsh"

Name "NetSwitch SQL Server Client"
OutFile "NetSwitch-SQL-Setup.exe"
Unicode true
InstallDir "$PROGRAMFILES64\NetSwitch"
InstallDirRegKey HKLM "Software\NetSwitch\SQL" "InstallDir"
RequestExecutionLevel admin      ; Program Files + HKLM need elevation
BrandingText "NetSwitch 6GGW"

!define APPNAME    "NetSwitch SQL Server Client"
!define COMPANY    "NetSwitch 6GGW"
!define VERSION    "3.0.0"
!define UNINSTKEY  "Software\Microsoft\Windows\CurrentVersion\Uninstall\NetSwitch-SQL"

!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\netswitch_sql.exe"
!define MUI_FINISHPAGE_RUN_PARAMETERS "--help"
!define MUI_FINISHPAGE_RUN_TEXT "Show the connector's options now"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Section "NetSwitch SQL client (required)" SecMain
  SectionIn RO
  SetOutPath "$INSTDIR"
  File "netswitch_sql.exe"
  File "README.md"

  ; a shortcut that opens a console in the install dir, ready to run the connector
  CreateDirectory "$SMPROGRAMS\NetSwitch"
  CreateShortCut "$SMPROGRAMS\NetSwitch\NetSwitch SQL (console).lnk" "$SYSDIR\cmd.exe" '/K "cd /d \"$INSTDIR\" && echo Run:  netswitch_sql.exe --server tcp:HOST,1433 --user YOURLOGIN"' "$SYSDIR\cmd.exe" 0
  CreateShortCut "$SMPROGRAMS\NetSwitch\Uninstall NetSwitch SQL.lnk" "$INSTDIR\uninstall.exe"
  CreateShortCut "$DESKTOP\NetSwitch SQL.lnk" "$SYSDIR\cmd.exe" '/K "cd /d \"$INSTDIR\""' "$SYSDIR\cmd.exe" 0

  ; remember where we installed
  WriteRegStr HKLM "Software\NetSwitch\SQL" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "Software\NetSwitch\SQL" "Version" "${VERSION}"

  ; Apps & features entry
  WriteRegStr HKLM "${UNINSTKEY}" "DisplayName"     "${APPNAME}"
  WriteRegStr HKLM "${UNINSTKEY}" "DisplayVersion"  "${VERSION}"
  WriteRegStr HKLM "${UNINSTKEY}" "Publisher"       "${COMPANY}"
  WriteRegStr HKLM "${UNINSTKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNINSTKEY}" "UninstallString" "$INSTDIR\uninstall.exe"
  WriteRegDWORD HKLM "${UNINSTKEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINSTKEY}" "NoRepair" 1

  WriteUninstaller "$INSTDIR\uninstall.exe"
SectionEnd

Section "Uninstall"
  Delete "$INSTDIR\netswitch_sql.exe"
  Delete "$INSTDIR\README.md"
  Delete "$INSTDIR\uninstall.exe"
  RMDir  "$INSTDIR"

  Delete "$SMPROGRAMS\NetSwitch\NetSwitch SQL (console).lnk"
  Delete "$SMPROGRAMS\NetSwitch\Uninstall NetSwitch SQL.lnk"
  RMDir  "$SMPROGRAMS\NetSwitch"
  Delete "$DESKTOP\NetSwitch SQL.lnk"

  ; note: the per-user profile (%APPDATA%\NetSwitch\sql.cfg) holds only server+user, no password.
  DeleteRegKey HKLM "${UNINSTKEY}"
  DeleteRegKey HKLM "Software\NetSwitch\SQL"
SectionEnd
