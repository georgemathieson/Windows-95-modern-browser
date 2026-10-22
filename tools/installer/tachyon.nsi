; SETUP.EXE: installs Tachyon test builds on Windows 95.
;
; tools/package.sh builds it with NSIS 3, whose ANSI installers still run on
; Windows 95 (Unicode ones need NT). The classic interface and no plug-ins
; keep the only code in it NSIS's own installer stub, which win95check
; checks like any other executable. Sprint 9 brings the full installer.
;
;   makensis -DVERSION=0.2.0 -DSRCDIR=dist/TACHYON -DICON=tachyon.ico
;            -DOUTFILE=setup.exe tools/installer/tachyon.nsi
;
; Silent install and uninstall, as tools/test_installer.sh does:
;   SETUP.EXE /S /D=C:\Tachyon       UNINSTAL.EXE /S

Unicode false

!macro REQUIRE NAME
  !ifndef ${NAME}
    !error "Define VERSION, SRCDIR, ICON and OUTFILE (see the top of this file)"
  !endif
!macroend
!insertmacro REQUIRE VERSION
!insertmacro REQUIRE SRCDIR
!insertmacro REQUIRE ICON
!insertmacro REQUIRE OUTFILE

!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Tachyon"
!define WEBSITE "https://github.com/georgemathieson/windows-95-modern-browser"

Name "Tachyon"
Caption "Tachyon ${VERSION} Setup"
UninstallCaption "Uninstall Tachyon"
BrandingText "Tachyon ${VERSION}"
OutFile "${OUTFILE}"
Icon "${ICON}"
UninstallIcon "${ICON}"
SetCompressor /SOLID lzma
XPStyle off
RequestExecutionLevel admin
ShowInstDetails show
ShowUninstDetails show
InstallColors /windows

InstallDir "$PROGRAMFILES\Tachyon"
InstallDirRegKey HKLM "Software\Tachyon" "InstallDir"

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "Tachyon"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "FileDescription" "Tachyon ${VERSION} Setup"
VIAddVersionKey "LegalCopyright" "Free software under the GNU General Public License v3"

; The GPL isn't an agreement to accept, so the licence page only informs.
SubCaption 0 ": Licence"
LicenseData "${SRCDIR}\LICENSE.TXT"
LicenseText "Tachyon is free software. You can use it for any purpose; this licence says how you may share and change it." "&Next >"
ComponentText "Choose what to install."
DirText "Setup will install Tachyon in this folder. To install it somewhere else, click Browse."

Page license
Page components
Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

Section "Tachyon (required)"
  SectionIn RO
  SetOutPath "$INSTDIR"
  File "${SRCDIR}\TACHYON.EXE"
  File "${SRCDIR}\FETCH.EXE"
  File "${SRCDIR}\EXPORTS.EXE"
  File "${SRCDIR}\README.TXT"
  File "${SRCDIR}\LICENSE.TXT"
  WriteUninstaller "$INSTDIR\UNINSTAL.EXE"

  WriteRegStr HKLM "Software\Tachyon" "InstallDir" "$INSTDIR"
  ; DisplayName and UninstallString are all Windows 95's Add/Remove
  ; Programs reads; later Windows shows the rest.
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayName" "Tachyon ${VERSION}"
  WriteRegStr HKLM "${UNINST_KEY}" "UninstallString" '"$INSTDIR\UNINSTAL.EXE"'
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\TACHYON.EXE"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${UNINST_KEY}" "Publisher" "The Tachyon project"
  WriteRegStr HKLM "${UNINST_KEY}" "URLInfoAbout" "${WEBSITE}"
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair" 1
SectionEnd

Section "Start menu shortcuts"
  CreateDirectory "$SMPROGRAMS\Tachyon"
  CreateShortcut "$SMPROGRAMS\Tachyon\Tachyon.lnk" "$INSTDIR\TACHYON.EXE"
  CreateShortcut "$SMPROGRAMS\Tachyon\Read Me.lnk" "$INSTDIR\README.TXT"
  CreateShortcut "$SMPROGRAMS\Tachyon\Uninstall Tachyon.lnk" "$INSTDIR\UNINSTAL.EXE"
SectionEnd

Section "Desktop shortcut"
  CreateShortcut "$DESKTOP\Tachyon.lnk" "$INSTDIR\TACHYON.EXE"
SectionEnd

; The font pack (decision 36) isn't inside SETUP.EXE, which must fit on a
; floppy: it's in FONTS beside it on the CD and in the zip.
Section "Fonts for other languages" SecFonts
  CreateDirectory "$INSTDIR\FONTS"
  CopyFiles /SILENT "$EXEDIR\FONTS\*.*" "$INSTDIR\FONTS" 6700
SectionEnd

Function .onInit
  IfFileExists "$EXEDIR\FONTS\*.*" fonts_here
  ; Read-only and not selected (SF_RO): there's nothing to copy.
  SectionSetFlags ${SecFonts} 16
  SectionSetText ${SecFonts} "Fonts for other languages (on the CD)"
fonts_here:
FunctionEnd

Function .onInstSuccess
  IfSilent done
  MessageBox MB_YESNO|MB_ICONQUESTION "Tachyon is installed. Start it now?" IDNO done
  Exec '"$INSTDIR\TACHYON.EXE"'
done:
FunctionEnd

Section "Uninstall"
  ; Files Tachyon makes for itself: the randomness seed, its log, and
  ; EXPORTS.EXE's list.
  Delete "$INSTDIR\TACHYON.RND"
  Delete "$INSTDIR\TACHYON.LOG"
  Delete "$INSTDIR\TACHYON.OLD"
  Delete "$INSTDIR\EXPORTS.TXT"
  Delete "$INSTDIR\TACHYON.EXE"
  Delete "$INSTDIR\FETCH.EXE"
  Delete "$INSTDIR\EXPORTS.EXE"
  Delete "$INSTDIR\README.TXT"
  Delete "$INSTDIR\LICENSE.TXT"
  Delete "$INSTDIR\UNINSTAL.EXE"
  Delete "$INSTDIR\FONTS\WQYMICRO.TTC"
  Delete "$INSTDIR\FONTS\DEJAVU.TTF"
  Delete "$INSTDIR\FONTS\DEJAVUB.TTF"
  Delete "$INSTDIR\FONTS\FONTS.TXT"
  ; Not /r: anything else in the folder is the user's.
  RMDir "$INSTDIR\FONTS"
  RMDir "$INSTDIR"

  Delete "$SMPROGRAMS\Tachyon\Tachyon.lnk"
  Delete "$SMPROGRAMS\Tachyon\Read Me.lnk"
  Delete "$SMPROGRAMS\Tachyon\Uninstall Tachyon.lnk"
  RMDir "$SMPROGRAMS\Tachyon"
  Delete "$DESKTOP\Tachyon.lnk"

  DeleteRegKey HKLM "${UNINST_KEY}"
  DeleteRegKey HKLM "Software\Tachyon"
SectionEnd
