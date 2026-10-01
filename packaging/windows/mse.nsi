; MSE Windows installer.
;
; Packages the tree that `cmake --install` wrote, which is not the same thing as
; bin/ -- see cmake/Install.cmake for what is left out and why.
;
; Built by the `installer` CMake target, or by hand:
;
;   makensis /DVERSION=0.1.0 /DSTAGING=C:\...\dist\MSE ^
;            /DOUTFILE=C:\...\MSE-0.1.0-setup.exe /DLICENCE=C:\...\LICENCE mse.nsi

Unicode true

!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"
!include "WinVer.nsh"
!include "x64.nsh"

; --- what the build tells us --------------------------------------------------

!ifndef VERSION
  !define VERSION "0.0.0"
!endif

!ifndef STAGING
  !error "STAGING is required: the directory cmake --install wrote."
!endif

!ifndef OUTFILE
  !define OUTFILE "MSE-${VERSION}-setup.exe"
!endif

!ifndef LICENCE
  !define LICENCE "${STAGING}\LICENCE"
!endif

!define APPNAME     "MSE"
!define APPEXE      "mse.exe"
!define PUBLISHER   "MSE"
!define DESCRIPTION "Multi-System Emulator"
!define HOMEPAGE    "https://github.com/Zawuza/mse"

; Where Windows lists installed programs, and where we remember the install
; directory so an upgrade lands on top of the previous one.
!define ARP_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}"
!define APP_KEY "Software\${APPNAME}"

; The file-type id for the association component. Prefixed so it cannot collide
; with another emulator's, which is the whole reason these are not just ".nes".
!define ROM_PROGID "MSE.NesRom"

Name "${APPNAME} ${VERSION}"
OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES64\${APPNAME}"

; Program Files is per-machine, so this needs the elevation prompt. It is asked
; for up front rather than part way through, where a refusal would leave a half
; written install directory behind.
RequestExecutionLevel admin

SetCompressor /SOLID lzma
ShowInstDetails show
ShowUnInstDetails show

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName"     "${APPNAME}"
VIAddVersionKey "FileDescription" "${DESCRIPTION} installer"
VIAddVersionKey "FileVersion"     "${VERSION}.0"
VIAddVersionKey "ProductVersion"  "${VERSION}"
VIAddVersionKey "CompanyName"     "${PUBLISHER}"
VIAddVersionKey "LegalCopyright"  "See LICENCE"

; --- pages --------------------------------------------------------------------

!define MUI_ABORTWARNING

; Drop an mse.ico in this directory and these two come to life; without one NSIS
; uses its own rather than refusing to build.
!if /FileExists "${__FILEDIR__}\mse.ico"
  !define MUI_ICON   "${__FILEDIR__}\mse.ico"
  !define MUI_UNICON "${__FILEDIR__}\mse.ico"
!endif

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${LICENCE}"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES

; No "run now" checkbox on purpose. The installer is elevated, so anything it
; launches is elevated too, and the first thing the app does is write its config
; into %APPDATA% -- which would be the administrator's, not the user's.
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; --- install ------------------------------------------------------------------

; NSIS builds a 32-bit installer even for a 64-bit application, and a 32-bit
; process on 64-bit Windows has its HKLM\Software writes redirected into
; WOW6432Node. Left alone, this installs a 64-bit program into Program Files and
; then registers it in the 32-bit half of the registry: Add/Remove Programs
; finds it, but a 64-bit process looking the install up does not, and the file
; association lands somewhere the shell only half reads.
!macro UseNativeRegistry
    ${If} ${RunningX64}
        SetRegView 64
    ${EndIf}
!macroend

Function .onInit
    ${IfNot} ${AtLeastWin10}
        MessageBox MB_OK|MB_ICONSTOP "${APPNAME} needs Windows 10 or newer."
        Abort
    ${EndIf}

    !insertmacro UseNativeRegistry

    ; What InstallDirRegKey would do, except that it runs before this function
    ; and so before the registry view is right. An upgrade lands on top of the
    ; previous install rather than beside it.
    ReadRegStr $0 HKLM "${APP_KEY}" "InstallDir"
    ${If} $0 != ""
        StrCpy $INSTDIR $0
    ${EndIf}
FunctionEnd

Function un.onInit
    !insertmacro UseNativeRegistry
FunctionEnd

Section "!${APPNAME}" SecCore
    SectionIn RO

    ; Windows will not let a running executable be replaced, and an install that
    ; carries on regardless leaves a mix of old and new files behind. Deleting
    ; the old exe is a cheap way to ask: it fails exactly when the app is up,
    ; and it is about to be overwritten anyway.
    ${If} ${FileExists} "$INSTDIR\${APPEXE}"
        ClearErrors
        Delete "$INSTDIR\${APPEXE}"
        ${If} ${Errors}
            MessageBox MB_OK|MB_ICONSTOP \
                "${APPNAME} is running. Close it and start this installer again."
            Abort
        ${EndIf}
    ${EndIf}

    SetOutPath "$INSTDIR"
    File /r "${STAGING}\*.*"

    WriteUninstaller "$INSTDIR\uninstall.exe"

    WriteRegStr HKLM "${APP_KEY}" "InstallDir" "$INSTDIR"
    WriteRegStr HKLM "${APP_KEY}" "Version"    "${VERSION}"

    WriteRegStr   HKLM "${ARP_KEY}" "DisplayName"     "${APPNAME}"
    WriteRegStr   HKLM "${ARP_KEY}" "DisplayVersion"  "${VERSION}"
    WriteRegStr   HKLM "${ARP_KEY}" "DisplayIcon"     "$INSTDIR\${APPEXE}"
    WriteRegStr   HKLM "${ARP_KEY}" "Publisher"       "${PUBLISHER}"
    WriteRegStr   HKLM "${ARP_KEY}" "URLInfoAbout"    "${HOMEPAGE}"
    WriteRegStr   HKLM "${ARP_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr   HKLM "${ARP_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
    WriteRegStr   HKLM "${ARP_KEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
    WriteRegDWORD HKLM "${ARP_KEY}" "NoModify" 1
    WriteRegDWORD HKLM "${ARP_KEY}" "NoRepair" 1

    ; What Add/Remove Programs shows as the size, in KB.
    ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
    IntFmt $0 "0x%08X" $0
    WriteRegDWORD HKLM "${ARP_KEY}" "EstimatedSize" "$0"

    ; Every path inside the application is relative to the working directory, so
    ; a shortcut that starts somewhere else finds no backends, no themes and no
    ; Lua. A shortcut takes its working directory from the last SetOutPath, so
    ; this line is load-bearing rather than tidiness.
    SetOutPath "$INSTDIR"
    CreateShortcut "$SMPROGRAMS\${APPNAME}.lnk" "$INSTDIR\${APPEXE}" "" \
        "$INSTDIR\${APPEXE}" 0 SW_SHOWNORMAL "" "${DESCRIPTION}"
SectionEnd

Section "Desktop shortcut" SecDesktop
    SetOutPath "$INSTDIR"
    CreateShortcut "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\${APPEXE}" "" \
        "$INSTDIR\${APPEXE}" 0 SW_SHOWNORMAL "" "${DESCRIPTION}"
SectionEnd

; Off by default: anyone installing this probably already has a NES emulator
; holding .nes, and quietly taking it is not the installer's call to make.
Section /o "Open .nes files with ${APPNAME}" SecAssoc
    WriteRegStr HKLM "Software\Classes\.nes" "" "${ROM_PROGID}"
    WriteRegStr HKLM "Software\Classes\${ROM_PROGID}" "" "NES ROM"
    WriteRegStr HKLM "Software\Classes\${ROM_PROGID}\DefaultIcon" "" "$INSTDIR\${APPEXE},0"
    WriteRegStr HKLM "Software\Classes\${ROM_PROGID}\shell\open\command" "" \
        '"$INSTDIR\${APPEXE}" "%1"'

    ; Without this the new association does not show up until the shell is
    ; restarted.
    System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecCore} \
        "${DESCRIPTION}, the cNES backend, and the Lua panels and themes they use."
    !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} \
        "Put a shortcut on the desktop."
    !insertmacro MUI_DESCRIPTION_TEXT ${SecAssoc} \
        "Make ${APPNAME} the program that opens .nes files."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; --- uninstall ----------------------------------------------------------------

Section "Uninstall"
    ${If} ${FileExists} "$INSTDIR\${APPEXE}"
        ClearErrors
        Delete "$INSTDIR\${APPEXE}"
        ${If} ${Errors}
            MessageBox MB_OK|MB_ICONSTOP \
                "${APPNAME} is running. Close it and uninstall again."
            Abort
        ${EndIf}
    ${EndIf}

    Delete "$SMPROGRAMS\${APPNAME}.lnk"
    Delete "$DESKTOP\${APPNAME}.lnk"

    ; Only if it is still ours. Another emulator installed after us will have
    ; taken .nes over, and standing on its association on the way out would be
    ; worse than leaving a stale key behind.
    ReadRegStr $0 HKLM "Software\Classes\.nes" ""
    ${If} $0 == "${ROM_PROGID}"
        DeleteRegKey HKLM "Software\Classes\.nes"
    ${EndIf}
    DeleteRegKey HKLM "Software\Classes\${ROM_PROGID}"
    System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'

    DeleteRegKey HKLM "${ARP_KEY}"
    DeleteRegKey HKLM "${APP_KEY}"

    ; Named rather than RMDir /r on $INSTDIR: someone who put a ROM folder
    ; inside the install directory should not lose it to an uninstall.
    RMDir /r "$INSTDIR\data"
    RMDir /r "$INSTDIR\themes"
    RMDir /r "$INSTDIR\cnes"
    Delete "$INSTDIR\*.dll"
    Delete "$INSTDIR\LICENCE"
    Delete "$INSTDIR\CREDITS.md"
    Delete "$INSTDIR\README.md"
    Delete "$INSTDIR\uninstall.exe"
    RMDir "$INSTDIR"

    ; Settings, themes and the library database live outside the install
    ; directory and outlive it, which is what you want for a reinstall and not
    ; what you want when you are done with it. So it is asked, not assumed --
    ; and skipped entirely on a silent uninstall, where there is nobody to ask.
    ${IfNot} ${Silent}
    ${AndIf} ${FileExists} "$APPDATA\libmse\*.*"
        MessageBox MB_YESNO|MB_ICONQUESTION \
            "Remove ${APPNAME}'s settings, themes and game library as well?$\n$\n$APPDATA\libmse" \
            /SD IDNO IDNO SkipUserData
        RMDir /r "$APPDATA\libmse"
        SkipUserData:
    ${EndIf}
SectionEnd
