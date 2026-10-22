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

## 4. This sprint's test (Sprint 4: Images and fonts)

The demo: **read Wikipedia's Windows 95 article with its pictures, and the
same article on the Japanese Wikipedia, in Japanese.**

1. In Windows 95, check that networking works (**Start > Run**, `winipcfg`
   should show an IP address) and that the date and time are today's.
2. Install Tachyon from the CD image or the zip (the floppy has no room for
   the fonts). The installer now has a component, **Fonts for other
   languages** (DejaVu Sans and WenQuanYi Micro Hei, about 6 MB): leave it
   ticked. With the zip, keep the `FONTS` folder beside `TACHYON.EXE`.
3. Type `about:fonts`. After Windows' own fonts come the pack's, DejaVu
   Sans and WenQuanYi Micro Hei, last. Please send us the list.
4. Open **Windows 95 on Wikipedia** from the home page (or type
   `en.wikipedia.org/wiki/Windows_95`). The text appears first; then the
   status bar counts "Loading pictures: N of M" while the pictures arrive,
   those on screen first. The page only moves when a picture's size
   wasn't in the page. Scroll down while they load: the ones you scroll to
   come next. Expected for now: the infobox and other side boxes still sit
   one under another rather than beside the text (Sprint 7).
5. Open the same article in Japanese: `ja.wikipedia.org/wiki/Windows_95`.
   The text should be Japanese characters, not boxes, with lines breaking
   between characters. Without the font pack (or a Japanese Windows or
   Internet Explorer's Japanese language support), the characters are
   boxes: that's expected.
6. An animated picture: `en.wikipedia.org/wiki/GIF` has a rotating globe
   near the top. It turns while it's on screen, and stops (to save the
   processor) when you scroll it away or minimise the window.
7. **256 colours.** In **Control Panel > Display > Settings**, set the
   colour palette to **256 Color** (restart if Windows asks), and reload
   the Windows 95 article. Photographs should look finely dotted, like a
   newspaper picture, not blotchy or striped. Then set it back. (Wine
   can't do 256 colours, so this is the only test of it.) If something
   looks wrong, `TACHYON.EXE --dither` at any colour depth shows what
   Tachyon means to draw.
8. **A web font:** `www.gov.uk` sets its headings and text in its own
   font, downloaded with the page: letters should look rounder than
   Arial's, a moment after the page appears.
9. If your mouse has a wheel, or your virtual machine passes a trackpad's
   two-finger scrolling on as a wheel, scroll a long page with it (Sprint
   2's crash, in the backlog, was in drawing code since rewritten).
10. Optional: in an MS-DOS Prompt in `C:\Program Files\Tachyon`, run
    `FETCH -q --render https://ja.wikipedia.org/wiki/Windows_95`. It loads
    the page with its pictures and fonts without showing it, and prints
    how long each step took and how much memory the page used.

Please report:

- For both articles: the status bar's "Done: N KB in N s; ... shown in N s"
  line, and roughly how long the pictures took.
- A screenshot of the Japanese article, and of the English one at 256
  colours (Print Screen, then paste into Paint).
- Anything drawn wrongly, and anything slow.
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
