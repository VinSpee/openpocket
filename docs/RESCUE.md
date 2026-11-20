# Rescuing a BlackBox over USB from a Mac or Windows (no extra hardware)

## Quick start: rescue a BlackBox that will not boot

Use this when the pedal shows a black screen or keeps restarting, and the installer page cannot reach it. You need the
USB cable and a Mac or a Windows PC. The first run only checks the pedal and saves a backup. Nothing is written until the
second run.

> **Warning.** Do not install OpenPocket on a pedal that ran M-VAVE V22 or V23, not even after a successful rescue.
> Today's OpenPocket installer writes from `0x4000`, over the end of the V22/V23 boot loader, and the pedal goes dark
> again. Wait for an installer that supports V22 and V23. It is not released yet.

> **Status.** Path A's tool has restored one real pedal, from a Mac. Path B (V22 and V23) is verified in simulation
> only: the kit's files were run against a simulated pedal with M-VAVE's real V22 and V23 packages, and the pedal ended
> on the exact official image. This matches one community report of a real pedal restored this way
> ([issue 31](https://github.com/isod89/openpocket/issues/31)). The OpenPocket team has not run path B on a real pedal,
> and has not run the Windows launcher on a real pedal.

### 1. Find your path

The rescue writes M-VAVE's official firmware back, and the firmware file must match the firmware the pedal had before
OpenPocket. Ask one question: did this pedal ever run M-VAVE V22 or V23? It did if you updated it with M-VAVE's own
updater after July 2026, or if it came with V22 or V23.

| Path | The pedal had | Firmware file | The pedal ends on |
| --- | --- | --- | --- |
| A | V16, V17 or V20, and never V22 or V23 | `BlackBox_020.fwsc` | Official V20 |
| B | V22 or V23 at any time, or you are not sure | `BlackBox_023.fwsc` | Official V23. No OpenPocket yet |
| C | Something else: both files are refused | None | Stop. Keep the backup |

The check in step 4 is the test, and it writes nothing. With the right file it prints
`head [0, 0x4000) matches the image`. With the wrong file it ends with `REFUSED`. Then run the check again with the
other path's file. If both files are refused, you are on path C: stop, do not try other files, and keep the backup.

V22 and V23 are one group. `BlackBox_022.fwsc` works the same way on either pedal, and the pedal then ends on V22.

### 2. Downloads

- [Python 3](https://www.python.org/downloads/). On Windows, tick "Add python.exe to PATH" on the installer's first
  screen. No driver is needed.
- The rescue kit, `OpenPocket-rescue-kit-<number>.zip`, from the Rescue card of the installer page, or built from
  this repository. Unzip it. It makes
  one folder, `OpenPocket-rescue-kit`. [The rescue kit](#the-rescue-kit) below says what is in it.
- The firmware file for your path, in the kit's `firmware` folder under the name in the table above. The kit from this
  repository does not hold M-VAVE's firmware. Two places have it:
  - The full kit at [openpocket.vinspee.me/rescue](https://openpocket.vinspee.me/rescue/) holds the V20, V22 and V23
    files in its `firmware` folder. Use that kit as it is, or copy the file from it.
  - [M-VAVE's download page](https://www.m-vave.com/appdownload) offers the current BlackBox file. In November 2026 that
    was V23. It is called `BlackBox.fwsc`: rename it to `BlackBox_023.fwsc`.
- Mac only: [Homebrew](https://brew.sh), then `brew install libusb` in Terminal, once. Linux needs `python3` and
  `libusb-1.0` and has not been tested.

### 3. Open a prompt in the kit folder

**Windows.** Press the Windows key, type `cmd`, right-click "Command Prompt" and choose "Run as administrator". Click
Yes. Use Command Prompt, not PowerShell: the lines below do not work in PowerShell. Open the `OpenPocket-rescue-kit`
folder in File Explorer until you see `RESCUE.bat`, click the address bar and copy the location. In the prompt type
`cd /d "`, right-click to paste, type `"` and press Enter.

**Mac.** Open Terminal. Type `cd ` (with a space after it), drag the `OpenPocket-rescue-kit` folder onto the Terminal
window, and press Enter.

### 4. Check and back up. Nothing is written

Unplug the pedal. Type the line for your path and press Enter. When it says it is waiting for the pedal, plug the pedal
straight into the computer, with no hub.

| | Path B: V22 or V23, or not sure | Path A: V16, V17 or V20 |
| --- | --- | --- |
| Windows | `RESCUE.bat --package firmware\BlackBox_023.fwsc` | `RESCUE.bat --package firmware\BlackBox_020.fwsc` |
| Mac | `sudo python3 tools/uboot_rescue.py --package firmware/BlackBox_023.fwsc` | `sudo python3 tools/uboot_rescue.py --package firmware/BlackBox_020.fwsc` |

On Windows, if it offers to format a drive, click Cancel. Never click Format. On a Mac, type your password when asked
(nothing shows while you type). If the Mac offers to install developer tools, click Install, then type the line again.
The first run downloads one small loader file, so it needs internet once.

The right file ends like this. The times, the count and the backup's name differ on your pedal:

```
[   0.0s] backup saved: ...OpenPocket-rescue/blackbox-20261119-230057.dump.bin
[   0.0s] head [0, 0x4000) matches the image
[   0.0s] 155 of 155 sectors in [0x4000, 0x9f000) differ
[   0.0s] run-app sent: the pedal restarts

Read-only run finished, nothing was written. 155 sectors differ from the image. Backup: ...
```

The wrong file ends like this instead. The pedal is not harmed. Unplug it and run the other path's line:

```
REFUSED: the flash head [0, 0x4000) is not the image's head: this package belongs to another layout; nothing was written
```

The backup is in the `OpenPocket-rescue` folder of your home (user) folder. Keep it: it is the only copy of what the
pedal held before the rescue.

### 5. Restore

Only after the check printed `head [0, 0x4000) matches the image`. Unplug the pedal, type the line with the same file,
and plug the pedal in when it is waiting. Do not touch the cable while it writes.

| | Path B | Path A |
| --- | --- | --- |
| Windows | `RESCUE.bat --write --package firmware\BlackBox_023.fwsc` | `RESCUE.bat --write --package firmware\BlackBox_020.fwsc` |
| Mac | `sudo python3 tools/uboot_rescue.py --write --package firmware/BlackBox_023.fwsc` | `sudo python3 tools/uboot_rescue.py --write --package firmware/BlackBox_020.fwsc` |

It shows the same lines as the check, counts the sectors it writes, and ends with `Done`. If it stops in the middle,
run the same line again: it continues where it stopped.

### 6. Restart

Unplug the cable. Switch the pedal off, wait 5 seconds, switch it on. The pedal should start on M-VAVE's official
firmware: V20 after path A, V23 after path B. Settings saved on the pedal may be reset. The backup holds what was there
before.

**Path B: stop here. Do not install OpenPocket on this pedal yet.** It would go dark again, for the reason in the
warning at the top of this page.

### If it does not work

| What you see | What to do |
| --- | --- |
| `REFUSED`, one file | The file is for another firmware than your pedal had. Nothing was written. Run the check with the other path's file. |
| `REFUSED`, both files (path C) | Stop. Do not try other files and do not use `--write`. Keep the backup. Run the fingerprint helper below and [open an issue](https://github.com/VinSpee/undertone/issues) with its output and the window's text. The other route is the SD-card recovery in `flash/`, which needs an ESP32-S3 board. |
| `ERROR ... CRC check failed`, or `not a firmware package` | The firmware file is damaged or is not a BlackBox firmware file. Nothing was sent to the pedal. Get the file again. |
| `ERROR ... not found` | The firmware file is not in the kit's `firmware` folder under the name in the line you typed. |
| "not recognized", or `cd /d` fails | The window is PowerShell. Open Command Prompt as in step 3. |
| It stops in the middle, or says it gave up | Run the same line again. It continues where it stopped. |
| No screen after step 6 | Keep the backup and [open an issue](https://github.com/VinSpee/undertone/issues) with the window's text. |

[Other symptoms](#other-symptoms), near the end of this page, lists more.

## The rescue kit

The kit is one zip that unpacks to one folder, `OpenPocket-rescue-kit`. Its number is in the zip's name and in its
`README.txt`. The folder's name never changes, so the commands above stay the same from kit to kit.

| File | What it is |
| --- | --- |
| `RESCUE.bat`, `tools/uboot_rescue.py`, `tools/pocketx_fw.py` | The rescue tool of this repository, unchanged |
| `fingerprint.bat`, `tools/head_fingerprint.py` | Says which firmware a backup or a firmware file belongs to |
| `extract-flash.bat`, `tools/extract_flash.py` | Takes `flash.bin` out of a package, for `--image` |
| `firmware/README.txt` | The three file names, their SHA-256, and where to get the files |
| `README.txt`, `LICENSE.txt`, `SHA256SUMS.txt` | One page of instructions, the GPL-3.0 text, the SHA-256 of every file |

The kit holds none of M-VAVE's firmware. A kit with M-VAVE's V20, V22 and V23 packages, the full kit, is published on
the project's own site only and never in this repository or its releases.

**The two helpers.** Both only read files on the computer. They need no pedal, no administrator and no `sudo`.

| | Windows | Mac |
| --- | --- | --- |
| Which firmware the pedal had, from the newest backup | `fingerprint.bat` | `python3 tools/head_fingerprint.py` |
| Which firmware a file is | `fingerprint.bat firmware\BlackBox_023.fwsc` | `python3 tools/head_fingerprint.py firmware/BlackBox_023.fwsc` |
| `flash.bin` out of a package | `extract-flash.bat firmware\BlackBox_023.fwsc` | `python3 tools/extract_flash.py firmware/BlackBox_023.fwsc` |

The fingerprint is the SHA-256 of the first `0x4000` bytes of the flash. `d540d2217091` is a V16, V17 or V20 pedal
(path A). `ef3dfea17ea8` is a V22 or V23 pedal (path B). You need the extraction only if `--package` stops with an
`ERROR` about the file: run steps 4 and 5 with `--image BlackBox_023-flash.bin` in place of `--package ...`.

**Checking the download.** The installer page shows the kit's SHA-256 beside its download button. Compare it with
`certutil -hashfile OpenPocket-rescue-kit-2.zip SHA256` on Windows or `shasum -a 256 OpenPocket-rescue-kit-2.zip` on a
Mac, with your kit's number in the name.

**Building it.** `node tools/op.mjs kit` (or `make kit`) writes the zip and `manifest.json` into `docs/rescue/`, where
the installer page looks for them. It needs only Python 3. The same sources give the same bytes on every computer, and
`rescue/kit.sha256` pins them. [`rescue/README.md`](../rescue/README.md) has the rules for changing the kit and for
building the full kit.

## How it works

Use this when the pedal restarts in a loop or shows a black screen after an install, and the installer's rescue mode
cannot finish. Between crashes the pedal sits
about 44 seconds in the JieLi mask-ROM USB boot state ("UBOOT": USB 4C4A:8057, shown by macOS as `WL80UBOOT1.00`). The
mask ROM is chip ROM, so it cannot be damaged. `tools/uboot_rescue.py` waits for that state, loads JieLi's RAM loader and
rewrites the app area of the flash from the official package, then restarts the pedal.

The installer page cannot do this part. In this state the pedal is a USB mass-storage device, and browsers refuse web pages
access to every mass-storage interface (WebUSB's protected classes), on every system and whatever driver is installed. The
page can only reach a pedal that answers over MIDI; the rescue below is a command that runs on the computer.

## One-time setup (macOS)

```
brew install libusb
```

That is all. The tool is one Python 3 file that calls libusb through `ctypes` (no `pip install`). It needs two files:

- The official package, `~/Documents/BlackBox (2).fwsc` (BlackBox_016) by default, or `--package FILE.fwsc`, or a raw flash
  image with `--image flash.bin`. Every CRC of a package is checked before it is used.
- JieLi's `wl82loader.bin`, downloaded once into the git-ignored `build/cache/` from kagaimiq/jl-uboot-tool (commit
  `c32b28e`, the same pin as `flash/installer-fm1buddy-pocketx.bat`) and checked against SHA-256
  `d41da6126760c9d66660bcc0cac8d27d221806c5e369a8036921efe68dca5376` on every run. It is not in this repository. `make clean`
  removes the cache; `--loader FILE` uses a copy you downloaded yourself.

## One-time setup (Windows)

Install Python 3 from [python.org](https://www.python.org/downloads/) and tick "Add python.exe to PATH". Nothing else: no
libusb, no Zadig, no driver change. Windows keeps its own mass-storage driver on the pedal, and the tool sends the same
commands through it (SCSI pass-through, the way kagaimiq/jl-uboot-tool reaches these chips on Windows). Windows lets only an
administrator do that, so the tool runs in a Command Prompt opened as administrator. The files are the same as on a Mac:
the official package (pass it with `--package`) and `wl82loader.bin`, downloaded and checked as above.

## The commands

`make rescue` and `make rescue-write` (or `node tools/op.mjs rescue [--write]`, the entry point next to `flash`) run the tool with
`sudo` for you and pass options on (`make rescue ARGS="--package FILE.fwsc"`). `make doctor` shows whether libusb, the package and
the loader are in place. The `sudo python3 tools/uboot_rescue.py` lines below are the same thing written out.

macOS keeps its own mass-storage driver on the pedal in this state, and libusb needs root to take the interface from it.
Plug the pedal straight into the Mac (no hub), start the command, and it waits for the pedal and starts by itself the moment
it appears.

1. **Read-only first.** Nothing is written to the pedal's flash; the loader goes to RAM only.

```
make rescue                              # = sudo python3 tools/uboot_rescue.py
```

It checks that the pedal answers INQUIRY as `WL82 UBOOT1.00`, loads the loader, reads the chip key (must be `0x980F`) and
the flash id (must be a 1 MiB flash), saves a backup of the whole flash to `~/OpenPocket-rescue/blackbox-<time>.dump.bin`,
compares the pedal's head `[0, 0x4000)` with the image's, reports how many sectors differ, and sends run-app. Keep this
backup: it is the only copy of what the pedal held before the rescue.

On Windows, open a Command Prompt as administrator (press the Windows key, type `cmd`, right-click "Command Prompt",
"Run as administrator"; not PowerShell), go to the OpenPocket folder and run the same tool through `RESCUE.bat`:

```
cd /d C:\path\to\openpocket
RESCUE.bat --package "C:\Users\you\Downloads\BlackBox.fwsc"            (read-only)
RESCUE.bat --write --package "C:\Users\you\Downloads\BlackBox.fwsc"    (the rescue, after the read-only run passed)
```

The backup goes to `%USERPROFILE%\OpenPocket-rescue`. Windows may say it found a drive and offer to format it while the
pedal is in this state: click **Cancel**, never Format. The tool finds the pedal among the disks by asking each one to name
itself (SCSI INQUIRY, which reads nothing and changes nothing) and only goes on with the one that answers `WL82 UBOOT1.00`.

2. **The rescue**, only after the read-only run has passed:

```
make rescue-write                        # = sudo python3 tools/uboot_rescue.py --write
```

Same steps, then for every 4 KiB sector in `[0x4000, end of the image)` that differs: erase, write, read back, compare. When it prints
`Done`, power-cycle the pedal. If the pedal drops out of UBOOT in the middle (it reboots into the crashing app and comes
back), the tool waits and starts again by itself; running it again later also resumes. Only the sectors that still differ are
written, so a rerun is short.

Options (after `ARGS=` with make): `--check` (prerequisites only), `--package`, `--image`, `--loader`, `--backup-dir DIR`, `--attempts N` (UBOOT windows to try, default 10).
Exit codes: 0 done, 1 setup problem (libusb, root, files), 2 refused by a safety check, 130 stopped by Ctrl-C.

## BlackBox V21 factory sounds

The official BlackBox V21 download installs the firmware but contains no sound bank.
OpenPocket's NAM, preset and loader storage overlap parts of the stock sound bank.
V21 checks all 20 amp and 20 cabinet records at startup and disables the sound bank
when any of them are invalid. In that state M-EFCS cannot restore it one model at a time.

Use the web installer to install the official [BlackBox V21 package](https://www.m-vave.com/appdownload)
if the pedal is running another version. When it shows `BlackBox_021`, click **Verify factory sounds**.
If the check fails, use the steps below on a Mac with M-EFCS, Python 3 and libusb installed
(`brew install libusb`). Connect only the BlackBox and close other apps using the pedal.
The recovery port uses USB Mass Storage, which [WebUSB protects](https://wicg.github.io/webusb/#has-a-protected-interface-class).
The installer can enter recovery mode over MIDI, and the Terminal tool handles the flash transfer.

1. In Terminal, from the OpenPocket repository, start the read-only check with the same V21 file.
   Replace the example path with the location of your download.

   ```sh
   sudo python3 tools/blackbox_v21_factory.py --package ~/Downloads/BlackBox.fwsc
   ```

2. When Terminal is waiting for UBOOT, click **Enter BlackBox USB recovery** under step 3
   in the installer. The tool checks the complete installed V21 firmware against the download,
   saves a flash backup in `~/OpenPocket-rescue`, and reboots the pedal.
3. Once the read-only check passes, run the same command with `--write` and click the
   recovery button again when Terminal is ready. This replaces the 20 factory amps,
   20 cabs and 80 presets. The tool checks the entire flash after writing. It only
   writes sectors in the factory bank at `0xBF000..0xFCFFF`.
4. After the pedal returns, click **Verify factory sounds** in the installer. This
   compares every record the running firmware returns with the vendor's factory bank.

The tool uses the AM4 files included in the installed M-EFCS app. If they are stored
elsewhere, pass `--assets DIRECTORY` containing `blackbox_am4_ampCab.bin` and
`blackbox_am4_preset.bin`. The files must match the AM4 bank in M-VAVE's official Windows
updater. If USB disconnects during a write, re-enter recovery and rerun the write.
Keep the first flash backup.

## What the first real run showed

The first run on a real, looping BlackBox was done by the captain, who reported that the rescue worked ("the rescue worked!").
Only the files the tool left behind are recorded here; the captain's terminal output was not kept.

| Item | What the files show |
| --- | --- |
| Backups | Two, in `~/OpenPocket-rescue`: `blackbox-20261105-213039.dump.bin` and `blackbox-20261105-213135.dump.bin`, 1,048,576 bytes each (a 1 MiB flash), written 56 s apart. They are byte for byte identical (same SHA-256), so both hold the flash from before any write. |
| What the pedal held | Application `BlackBox_936`, the Bluetooth test image that caused the loop. Its app area passes the package's own CRC checks. |
| Head `[0, 0x4000)` | Identical to the head of the official BlackBox_016 `flash.bin`, so the head check passes. |
| Sectors to write | 159 of the 160 sectors in `[0x4000, 0xA3000)` differed from BlackBox_016 (first `0x4000`, last `0xA2000`). |
| Restored package | BlackBox_016 (`BlackBox (2).fwsc`, the tool's default image). |
| Chip key, flash id | Unknown: they are printed, not saved. The tool's checks (key `0x980F`, 1 MiB flash) passed, because it went on to write. |
| Length of the UBOOT window, sectors written, time taken | Unknown: not saved. A later run should keep its output (`make rescue 2>&1 \| tee rescue.log`). |

The two backups' times are consistent with a read-only run followed by the `--write` run, but no file records which was which.

## Safety rules

- The default is read-only. Writing needs `--write`.
- Writes are limited to whole 4 KiB sectors in `[0x4000, end of the image)`; that is `[0x4000, 0xA5000)` for the V20
  package and `[0x4000, 0x9F000)` for the V22 and V23 packages. The end comes from the package and can never be above `0xFF000`. The head `[0, 0x4000)` (header,
  second-stage boot loader, chip key) and everything above the image (Bluetooth trims, user data) are never touched.
- Only a fixed list of loader commands can be sent. Chip erase (`FB02`) and key burn (`FC12`) are not on it. In a read-only
  run the erase and write commands are not on it either.
- It stops without writing when the device is not `WL82 UBOOT1.00`, the chip key is not `0x980F`, the flash is not 1 MiB, or
  the pedal's head differs from the image's head (a package for another layout: the V20 file on a pedal that ran V22
  or V23, or the other way round).
- The backup is complete on disk before the first erase.
- After a restore, do not reinstall the Bluetooth test image until the cause of the crash loop is understood: the loop came
  from that image.

## Other symptoms

| Symptom | Cause and next step |
| --- | --- |
| `libusb not found` | `brew install libusb` (or set `LIBUSB_PATH`). |
| Windows: `only an administrator` | Open the Command Prompt as administrator and run `RESCUE.bat` there. |
| Windows: `Python 3 is not installed` | Install it from python.org (tick "Add python.exe to PATH"). If typing `python` opens the Microsoft Store, install from python.org instead; `RESCUE.bat` uses its `py` launcher. |
| `cannot take the interface from macOS (LIBUSB_ERROR_ACCESS)` | Run the command with `sudo`. |
| Waits forever | The pedal is not in UBOOT right now. It is there about 44 s out of every 70 s while it loops; check `ioreg -p IOUSB` for `WL80UBOOT1.00` (Windows: a new disk drive in Device Manager), a data cable and a direct port. |
| `REFUSED` | A safety check stopped it; the message names which. Nothing was written. `the flash head [0, 0x4000) is not the image's head` means the file of the other path: see the quick start, step 1. |
| `gave up after N tries` | The pedal kept leaving UBOOT before the run could finish. Rerun: it resumes. |
| Still nothing | The SD-card recovery in `flash/` (fm1buddy on an ESP32-S3) is the other route. |

## Credits

The USB boot protocol is documented by [kagaimiq/jl-uboot-tool](https://github.com/kagaimiq/jl-uboot-tool) (MIT) and
[konsumer/fm1buddy](https://github.com/konsumer/fm1buddy) (GPL-3.0); `tools/uboot_rescue.py` is a new implementation of the
same commands for macOS and Windows. Tests: `make rescue-check` runs it against a simulated pedal (`tests/uboot_sim.py`), with no hardware.
`tests/test_rescue_kit.py` runs the kit's own files against the same simulated pedal on a V20, a V22 and a V23 flash head.
