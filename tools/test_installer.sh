#!/bin/sh
# Install a packaged SETUP.EXE under Wine, check what it did, then uninstall
# and check that everything has gone. CI runs this after tools/package.sh.
#
#   tools/test_installer.sh dist/tachyon-VERSION-win95-setup.exe
set -eu

setup=$1
root=$(cd "$(dirname "$0")/.." && pwd)
prefix=${WINEPREFIX:-$HOME/.wine}
dir="$prefix/drive_c/TachyonTest"
fail() { echo "test_installer.sh: $*" >&2; exit 1; }
key='HKLM\Software\Microsoft\Windows\CurrentVersion\Uninstall\Tachyon'

# /D must come last and can't be quoted, so the test folder has no spaces.
wine "$setup" /S '/D=C:\TachyonTest'
wineserver -w
for f in TACHYON.EXE FETCH.EXE EXPORTS.EXE README.TXT LICENSE.TXT UNINSTAL.EXE; do
  [ -f "$dir/$f" ] || fail "$f was not installed"
done
cmp -s "$dir/TACHYON.EXE" "$(dirname "$setup")/TACHYON/TACHYON.EXE" || fail "TACHYON.EXE differs from the packaged one"
wine reg query "$key" /v UninstallString | grep -q 'C:\\TachyonTest\\UNINSTAL.EXE' || fail "no Add/Remove Programs entry"
programs=$(find "$prefix/drive_c/users" -path '*Start Menu/Programs/Tachyon' -type d)
[ -n "$programs" ] && [ -f "$programs/Tachyon.lnk" ] || fail "no Start menu shortcut"
# The uninstaller is written at install time, so check it too.
python3 "$root/tools/win95check/win95check.py" --quiet "$dir/UNINSTAL.EXE"
# With no arguments FETCH.EXE prints its usage and exits with 1.
status=0
wine "$dir/FETCH.EXE" >/dev/null 2>&1 || status=$?
[ "$status" = 1 ] || fail "the installed FETCH.EXE doesn't run (exit status $status)"

# Files Tachyon creates for itself must go too.
touch "$dir/TACHYON.RND" "$dir/TACHYON.LOG" "$dir/TACHYON.OLD"
wine "$dir/UNINSTAL.EXE" /S
wineserver -w
[ ! -e "$dir" ] || fail "the uninstaller left $dir: $(ls "$dir")"
[ ! -e "$programs" ] || fail "the uninstaller left the Start menu folder"
if wine reg query "$key" >/dev/null 2>&1; then fail "the uninstaller left the Add/Remove Programs entry"; fi
echo "test_installer.sh: installed and uninstalled $(basename "$setup")"
