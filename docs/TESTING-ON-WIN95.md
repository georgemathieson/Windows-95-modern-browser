# Testing on Windows 95

CI builds and tests Tachyon under Wine, but Wine is not Windows 95: it has
APIs that Windows 95 lacks and behaves differently in many small ways. A
sprint is only finished once its demo has run on real Windows 95, in a
virtual machine or on real hardware.

## 1. Get a build

Open the repository's **Actions** tab, pick the latest successful **CI**
run on the branch you want, and download the **tachyon-win95** artifact.
GitHub wraps it in a zip; inside are:

| File | Use it with |
|---|---|
| `tachyon-VERSION-win95.img` | A 1.44 MB floppy image: works in every emulator, and every Windows 95 install can read it |
| `tachyon-VERSION-win95.iso` | A CD-ROM image, if your VM has a CD drive and driver |
| `tachyon-VERSION-win95.zip` | The same files, for shared folders or real hardware |

Or build it yourself (see the README) and run `tools/package.sh`.

## 2. Get it into Windows 95

Mount the floppy image as drive A: (or the ISO as the CD drive), open
**My Computer**, and either run `TACHYON.EXE` from there or copy it to the
hard disk first.

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

## 4. This sprint's test (Sprint 1: Hello, HTTPS)

1. In Windows 95, check that networking works: **Start > Run**, `winipcfg`
   should show an IP address. Set the date and time to today's.
2. Start `TACHYON.EXE`. If the home page shows a yellow warning about the
   clock, fix the clock first.
3. Type `example.com` and press Enter. You should see `HTTP/1.1 200 OK`,
   the headers in blue, and the page's HTML below them. The status bar says
   how long it took and which TLS version was used.
4. Try `en.wikipedia.org` and `news.ycombinator.com`. Scroll with Page Down,
   the scroll bar or the mouse wheel. Press Back.
5. Load a page and press **Stop** (or Esc) while it loads.
6. Open **Help > About Tachyon** and note the "Randomness" line.
7. Optional: open an MS-DOS Prompt on the CD or zip copy and run
   `FETCH -i https://example.com`.

Please report the status bar's "Done: ... in N s" for each site (it tells us
how fast TLS is on your hardware) and the About box's "Randomness" line.

## 5. What to report

Open an issue or tell Claude in the session:

- Your Windows 95 version: **Start > Settings > Control Panel > System**
  shows `4.00.950` (original), `950 A`, `950 B` (OSR2) or `950 C` (OSR2.5).
- The emulator or hardware: CPU, RAM, display colour depth.
- What you did, what you expected, and what happened. Screenshots help
  (Print Screen, then paste into Paint).
- For a crash, the text of the error dialog. **Details >>** shows the
  module and address.

## 6. Optional: check the export lists

`win95check` decides which Windows functions Tachyon may use from lists that
are partly hand-curated. To verify them against your copy of Windows:

1. Run `EXPORTS.EXE` from the Tachyon floppy. It writes `EXPORTS.TXT` next to
   itself (or to `C:\EXPORTS.TXT`).
2. Get the file back to your PC, for example with mtools:
   `mcopy -i tachyon-0.0.1-win95.img ::EXPORTS.TXT .`
3. Run `tools/win95check/dump_exports.py EXPORTS.TXT`, or just attach the file
   to an issue.

Any name reported as "listed but missing" is a mistake in our lists, and
exactly the kind of bug that would stop Tachyon starting on Windows 95.
`EXPORTS.TXT` contains function names only.
