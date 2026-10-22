# Testing on Windows 95

CI builds and tests Tachyon under Wine, but Wine is not Windows 95: it has
APIs that Windows 95 lacks and behaves differently in many small ways. A
sprint is only finished once its demo has run on real Windows 95, in a
virtual machine or on real hardware.

## 1. Get a build

Each sprint's test build is on the repository's
[Releases](https://github.com/georgemathieson/Windows-95-modern-browser/releases)
page. For a branch with no release yet, open the **Actions** tab, pick the
latest successful **CI** run on that branch, and download the
**tachyon-win95** artifact (GitHub wraps it in a zip). Either way you get:

| File | Use it with |
|---|---|
| `tachyon-VERSION-win95-setup.exe` | The installer: copy it in through a shared folder, or use the CD image, which carries it as `SETUP.EXE` |
| `tachyon-VERSION-win95.iso` | A CD-ROM image with `SETUP.EXE` and the programs, if your VM has a CD drive and driver |
| `tachyon-VERSION-win95.img` | A 1.44 MB floppy image with the installer as `SETUP.EXE`: works in every emulator, and every Windows 95 install can read it |
| `tachyon-VERSION-win95.zip` | The programs, for shared folders or real hardware |

Or build it yourself (see the README) and run `tools/package.sh`.

## 2. Get it into Windows 95

Mount the ISO as the CD drive and run `SETUP.EXE` from it (or run the setup
program from wherever you copied it). Without a CD drive, mount the floppy
image as drive A: and run `SETUP.EXE` from there. (Since Sprint 3,
`TACHYON.EXE` is too big for a floppy by itself.) Once installed,
`TACHYON.EXE` needs no other files, so you can also copy it anywhere.

## 3. Which emulator

Any of these work. Use whichever you already have.

- **86Box or PCem.** Emulates period hardware accurately, so it's the best
  test of real-world speed. A Pentium II at 233–300 MHz with 64–128 MB is a
  good match for our main target. Remember that today's builds need a
  Pentium Pro or newer (see the backlog item on the toolchain).
- **VirtualBox, QEMU, VMware.** Fast and convenient. For QEMU, something like:

  ```sh
  qemu-system-i386 -cpu pentium2 -m 128 -hda win95.img \
    -fda tachyon-0.0.1-win95.img -vga cirrus -nic user,model=ne2k_pci
  ```

  Windows 95 can stop at boot with "Windows protection error" on fast modern
  CPUs. That's a timing bug in Windows 95 itself; the community FIX95CPU
  patch fixes it, or use 86Box/PCem with a period CPU.

From Sprint 1, Tachyon needs the network: install a network card driver
(Windows 95 has NE2000 and AMD PCnet drivers built in) and TCP/IP, and **set
the correct date and time**, because certificate checks fail if the clock
is wrong.

We can't distribute Windows 95. You need your own copy.

## 4. This sprint's test (Sprint 3: First Light)

The demo: **read a Wikipedia article in Tachyon on Windows 95.**

1. In Windows 95, check that networking works (**Start > Run**, `winipcfg`
   should show an IP address) and that the date and time are today's.
2. Install Tachyon with the setup program (see section 2), over the old
   version if you have it, and start it. The home page is now drawn by
   Tachyon itself: a navy banner, then text and links.
3. Click **Fonts** at the bottom of the home page (or type `about:fonts`).
   It lists the fonts Tachyon found in Windows. Arial, Times New Roman and
   Courier New should be there. Please send us the list.
4. Click **Windows 95 on Wikipedia** (or type
   `en.wikipedia.org/wiki/Windows_95`). While it loads, the status bar
   names each style sheet it fetches. Then the article appears, in
   Wikipedia's colours and fonts. Expected for now: pictures are empty
   frames, or just their descriptions (Sprint 4), and tables, side columns
   and floating boxes, like the infobox, are laid out one under another
   rather than side by side (Sprint 7).
5. Read some of it. Scroll with the scroll bar, Page Up and Page Down, the
   arrow keys, Home and End, and the mouse wheel.
6. Point at a link: the pointer becomes a hand and the status bar shows
   where it goes. Click a heading in the article's contents: the page
   jumps there and the address ends in `#...`. Click a link to another
   article, then press **Back**.
7. Resize the window, and maximise it: the text reflows to the new width
   a moment after you stop.
8. **View > Document Tree** shows the page's tree, **View > Page Source**
   its source, and **View > Page** the page again. **Ctrl+U** goes back
   and forth between the page and its source.
9. Press **F5** to reload, and press **Stop** (or Esc) while a page loads.
10. If your mouse has a wheel, or your virtual machine passes a trackpad's
    two-finger scrolling on as a wheel: Sprint 2's crash on two-finger
    scrolling (see the backlog) was in drawing code that has been
    rewritten. Please try it again on a long page.
11. Optional: in an MS-DOS Prompt in `C:\Program Files\Tachyon`, run
    `FETCH -q --render https://en.wikipedia.org/wiki/Windows_95`. It loads
    and lays out the page without showing it, and prints how long each
    step took and how much memory the page used.

Please report:

- For the Wikipedia article and one or two other sites: the status bar's
  "Done: N KB in N s; N style sheets; shown in N s" (the time to download,
  then the time to fetch the style sheets and lay the page out), and how
  long scrolling a screen takes, roughly.
- Anything drawn wrongly, with a screenshot if you can (Print Screen, then
  paste into Paint), and anything slow.
- Any crash, with the end of `TACHYON.LOG` (section 5).

## 5. What to report

Open an issue or tell Claude in the session:

- Your Windows 95 version: **Start > Settings > Control Panel > System**
  shows `4.00.950` (original), `950 A`, `950 B` (OSR2) or `950 C` (OSR2.5).
- The emulator or hardware: CPU, RAM, display colour depth.
- What you did, what you expected, and what happened. Screenshots help
  (Print Screen, then paste into Paint).
- For a crash, the end of `TACHYON.LOG` (Help > View Log opens it in
  Notepad; it's in `C:\Program Files\Tachyon`). Its crash report says what
  Tachyon was doing, down to the tree rows it was drawing. A photo of the
  screen is fine. Windows' own dialog (**Details >>**) helps too.

## 6. Optional: check the export lists

`win95check` decides which Windows functions Tachyon may use from lists that
are partly hand-curated. To verify them against your copy of Windows:

1. Run `EXPORTS.EXE` from `C:\Program Files\Tachyon` (or from the Tachyon
   floppy). It writes `EXPORTS.TXT` next to itself (or to `C:\EXPORTS.TXT`).
2. Get the file back to your PC, for example by copying it to the Tachyon
   floppy and using mtools:
   `mcopy -i tachyon-0.2.0-win95.img ::EXPORTS.TXT .`
3. Run `tools/win95check/dump_exports.py EXPORTS.TXT`, or just attach the file
   to an issue.

Any name reported as "listed but missing" is a mistake in our lists, and
exactly the kind of bug that would stop Tachyon starting on Windows 95.
`EXPORTS.TXT` contains function names only.
