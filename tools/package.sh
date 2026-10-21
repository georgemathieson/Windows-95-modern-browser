#!/bin/sh
# Package a Windows 95 build for testers.
#
#   tools/package.sh [BUILD_DIR] [OUT_DIR]     (defaults: build/win95 dist)
#
# Produces, in OUT_DIR:
#   tachyon-VERSION-win95.zip   the files, for copying into a VM or machine
#   tachyon-VERSION-win95.img   a 1.44 MB floppy image (needs mtools)
#   tachyon-VERSION-win95.iso   a CD-ROM image (needs genisoimage)
set -eu

build=${1:-build/win95}
out=${2:-dist}
root=$(cd "$(dirname "$0")/.." && pwd)
exe="$build/src/platform/win32/TACHYON.exe"
exports_exe="$build/tools/win95exports/EXPORTS.exe"
version_h="$build/src/core/generated/tc_version.h"

[ -f "$exe" ] || { echo "package.sh: $exe not found; build the win95 preset first" >&2; exit 1; }
version=$(sed -n 's/^#define TC_VERSION *"\(.*\)"/\1/p' "$version_h")
sprint=$(sed -n 's/^#define TC_SPRINT *\([0-9]*\)/\1/p' "$version_h")
sprint_name=$(sed -n 's/^#define TC_SPRINT_NAME *"\(.*\)"/\1/p' "$version_h")
name="tachyon-$version-win95"

rm -rf "$out/TACHYON"
mkdir -p "$out/TACHYON"
cp "$exe" "$out/TACHYON/TACHYON.EXE"
cp "$exports_exe" "$out/TACHYON/EXPORTS.EXE"
# DOS line endings so Notepad shows the text files properly.
sed 's/$/\r/' "$root/LICENSE" > "$out/TACHYON/LICENSE.TXT"
sed 's/$/\r/' > "$out/TACHYON/README.TXT" <<EOF
TACHYON $version (Sprint $sprint: $sprint_name)
A modern web browser for Windows 95.

REQUIREMENTS
  Windows 95, any release. No extra DLLs are needed.
  A Pentium Pro, Pentium II or newer CPU (for now: the original Pentium,
  Pentium MMX and AMD K6 are not supported yet).

RUNNING
  Copy TACHYON.EXE anywhere and double-click it. There is no installer yet.

WHAT WORKS IN THIS BUILD
  The browser window, menus, address bar and the start page. Tachyon cannot
  load web pages yet: HTTP and TLS arrive in Sprint 1.

HELPING TEST
  EXPORTS.EXE writes EXPORTS.TXT, a list of the functions your copy of
  Windows provides. Sending it to us helps check that Tachyon only uses
  what Windows 95 really has. It reads names only and changes nothing.

FEEDBACK
  https://github.com/georgemathieson/windows-95-modern-browser/issues

Tachyon is free software under the GNU General Public License v3
(see LICENSE.TXT).
EOF

rm -f "$out/$name.zip" "$out/$name.img" "$out/$name.iso"
(cd "$out" && zip -q -r "$name.zip" TACHYON)

if command -v mformat >/dev/null 2>&1; then
  mformat -C -f 1440 -v TACHYON -i "$out/$name.img" ::
  mcopy -i "$out/$name.img" "$out"/TACHYON/* ::
else
  echo "package.sh: mtools not installed, skipping the floppy image" >&2
fi

if command -v genisoimage >/dev/null 2>&1; then
  genisoimage -quiet -J -r -V TACHYON -o "$out/$name.iso" "$out/TACHYON"
else
  echo "package.sh: genisoimage not installed, skipping the ISO" >&2
fi

ls -l "$out"/"$name".*
