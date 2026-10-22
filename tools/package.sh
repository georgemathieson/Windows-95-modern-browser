#!/bin/sh
# Package a Windows 95 build for testers.
#
#   tools/package.sh [BUILD_DIR] [OUT_DIR]     (defaults: build/win95 dist)
#
# Produces, in OUT_DIR:
#   tachyon-VERSION-win95-setup.exe  the installer (needs NSIS's makensis)
#   tachyon-VERSION-win95.zip   everything, for copying into a VM or machine
#   tachyon-VERSION-win95.img   a 1.44 MB floppy image with SETUP.EXE, the installer
#                               (needs mtools; TACHYON.EXE alone no longer fits)
#   tachyon-VERSION-win95.iso   a CD-ROM image with everything and SETUP.EXE (needs genisoimage)
#   tachyon-VERSION-win95-symbols.txt  TACHYON.EXE's function addresses, for reading
#                               crash reports with tools/symbolize.py
#
# The packaged programs are stripped; the build directory keeps the symbols
# for reading crash addresses.
set -eu

build=${1:-build/win95}
out=${2:-dist}
root=$(cd "$(dirname "$0")/.." && pwd)
strip=${STRIP:-i686-w64-mingw32-strip}
objdump=${OBJDUMP:-i686-w64-mingw32-objdump}
nm=${NM:-i686-w64-mingw32-nm}
version_h="$build/src/core/generated/tc_version.h"

[ -f "$build/src/platform/win32/TACHYON.exe" ] || { echo "package.sh: build the win95 preset first" >&2; exit 1; }
version=$(sed -n 's/^#define TC_VERSION *"\(.*\)"/\1/p' "$version_h")
sprint=$(sed -n 's/^#define TC_SPRINT *\([0-9]*\)/\1/p' "$version_h")
sprint_name=$(sed -n 's/^#define TC_SPRINT_NAME *"\(.*\)"/\1/p' "$version_h")
commit=$(sed -n 's/^#define TC_BUILD_COMMIT *"\(.*\)"/\1/p' "$version_h")
isa=$(sed -n 's/^TACHYON_ISA_FLOOR:[A-Z]*=//p' "$build/CMakeCache.txt")
name="tachyon-$version-win95"

rm -rf "$out/TACHYON"
mkdir -p "$out/TACHYON"
"$strip" -o "$out/TACHYON/TACHYON.EXE" "$build/src/platform/win32/TACHYON.exe"
"$strip" -o "$out/TACHYON/FETCH.EXE" "$build/tools/fetch/FETCH.exe"
"$strip" -o "$out/TACHYON/EXPORTS.EXE" "$build/tools/win95exports/EXPORTS.exe"
# DOS line endings so Notepad shows the text files properly.
sed 's/$/\r/' "$root/LICENSE" > "$out/TACHYON/LICENSE.TXT"
sed 's/$/\r/' > "$out/TACHYON/README.TXT" <<TXT
TACHYON $version (Sprint $sprint: $sprint_name)
A modern web browser for Windows 95.

REQUIREMENTS
  Windows 95, any release. No extra DLLs are needed.
  A Pentium Pro, Pentium II or newer CPU (for now: the original Pentium,
  Pentium MMX and AMD K6 are not supported yet).
  TCP/IP with a network card or Dial-Up Networking, and the correct date
  and time: secure websites can't be checked with a wrong clock.

INSTALLING
  Run the setup program (SETUP.EXE on the CD and the floppy, or
  $name-setup.exe).
  It installs Tachyon in C:\Program Files\Tachyon with Start menu and
  desktop shortcuts. To remove it, use Add/Remove Programs in the Control
  Panel. Or don't install it at all: TACHYON.EXE needs no other files, so
  you can copy it anywhere and double-click it.

RUNNING
  Type an address such as en.wikipedia.org/wiki/Windows_95 and press
  Enter. Click links to follow them; Back and Forward go through the
  pages you've seen.

WHAT WORKS IN THIS BUILD
  Tachyon fetches pages over HTTP and HTTPS (TLS 1.2 and 1.3, checked
  against Mozilla's trusted certificate authorities), parses their HTML
  the way today's browsers do, loads their style sheets, and draws them
  with its own layout engine in the TrueType fonts Windows 95 already has.
  View > Document Tree and View > Page Source (Ctrl+U) show what's
  underneath. Not yet: pictures (they show as frames or their
  descriptions), tables and side-by-side layouts (laid out one under
  another), scripts, forms and cookies. Those arrive in later sprints.

  Options: TACHYON.EXE [address] [--proxy HOST:PORT] [--cacert FILE.PEM]
           [--fonts FOLDER]   (more TrueType fonts to use)

FETCH.EXE (on the CD and in the zip)
  Downloads a page from an MS-DOS Prompt, for testing:
    FETCH -i https://example.com
  -i shows the response headers; -q hides the page; --dom prints the
  page's document tree instead, with a summary of the parse; --render
  loads its style sheets and lays it out, and says how long that took.

IF TACHYON CRASHES
  Tachyon keeps a log, TACHYON.LOG, next to TACHYON.EXE, and writes a
  report into it when it crashes. Help > View Log opens it in Notepad.
  Please send it to us, or a photo of the end of it, and say what you
  were doing.

HELPING TEST
  EXPORTS.EXE writes EXPORTS.TXT, a list of the functions your copy of
  Windows provides. Sending it to us helps check that Tachyon only uses
  what Windows 95 really has. It reads names only and changes nothing.

FEEDBACK
  https://github.com/georgemathieson/windows-95-modern-browser/issues

Tachyon is free software under the GNU General Public License v3
(see LICENSE.TXT).
TXT

rm -f "$out/$name-setup.exe" "$out/$name.zip" "$out/$name.img" "$out/$name.iso" "$out/$name-symbols.txt"
(cd "$out" && zip -q -r "$name.zip" TACHYON)

# The packaged TACHYON.EXE is stripped; crash reports give addresses that
# this file turns back into function names.
{
  echo "# Tachyon $version, build $commit: nm -n TACHYON.EXE (see tools/symbolize.py)"
  "$nm" -n --defined-only "$build/src/platform/win32/TACHYON.exe"
} > "$out/$name-symbols.txt"

# The installer gets the same Windows 95 check as everything we build.
if command -v makensis >/dev/null 2>&1; then
  abs_out=$(cd "$out" && pwd)
  makensis -V2 -DVERSION="$version" -DSRCDIR="$abs_out/TACHYON" \
           -DICON="$root/src/platform/win32/res/tachyon.ico" \
           -DOUTFILE="$abs_out/$name-setup.exe" "$root/tools/installer/tachyon.nsi"
  python3 "$root/tools/win95check/win95check.py" --quiet --objdump "$objdump" --isa "${isa:-i586}" \
          "$out/$name-setup.exe"
else
  echo "package.sh: NSIS (makensis) not installed, skipping the installer" >&2
fi

# TACHYON.EXE is bigger than a floppy since Sprint 3; the installer is
# compressed and fits.
if ! command -v mformat >/dev/null 2>&1; then
  echo "package.sh: mtools not installed, skipping the floppy image" >&2
elif [ ! -f "$out/$name-setup.exe" ]; then
  echo "package.sh: no installer, so no floppy image (TACHYON.EXE doesn't fit)" >&2
else
  mformat -C -f 1440 -v TACHYON -i "$out/$name.img" ::
  mcopy -i "$out/$name.img" "$out/$name-setup.exe" ::SETUP.EXE
  mcopy -i "$out/$name.img" "$out/TACHYON/EXPORTS.EXE" "$out/TACHYON/README.TXT" "$out/TACHYON/LICENSE.TXT" ::
fi

if command -v genisoimage >/dev/null 2>&1; then
  setup=
  [ -f "$out/$name-setup.exe" ] && setup="SETUP.EXE=$out/$name-setup.exe"
  genisoimage -quiet -J -r -V TACHYON -graft-points -o "$out/$name.iso" "$out/TACHYON" $setup
else
  echo "package.sh: genisoimage not installed, skipping the ISO" >&2
fi

ls -l "$out"/"$name"*.*
