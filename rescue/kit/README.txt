OPENPOCKET RESCUE KIT FOR THE M-VAVE BLACKBOX
=============================================
Kit version {{VERSION}}

What this is
------------
The tool that restores the official firmware of a BlackBox that shows a black
screen or restarts in a loop, over its USB cable, with no extra hardware.
The guide, with what each step shows on screen:
https://github.com/VinSpee/undertone/blob/main/docs/RESCUE.md

The first run only reads the pedal and saves a backup. Nothing is written
until you add --write. A wrong firmware file is refused before anything is
written.

You need Python 3 (https://www.python.org/downloads/, tick "Add python.exe to
PATH"). On a Mac you also need libusb: brew install libusb. The first run
downloads one small loader file, so it needs internet once.

The firmware file
-----------------
The tool writes M-VAVE's official firmware back, so it needs M-VAVE's file.
Look in the firmware folder. If it holds no .fwsc file, this kit came without
M-VAVE's firmware: firmware\README.txt says where to get the file and what to
call it.

Which path is yours
-------------------
Path A. The pedal never ran M-VAVE V22 or V23 (it had V16, V17 or V20).
        File: firmware\BlackBox_020.fwsc
Path B. The pedal ran M-VAVE V22 or V23 at any time, or you are not sure.
        File: firmware\BlackBox_023.fwsc  (BlackBox_022.fwsc works the same)
Path C. Both files are refused. Stop. Do not try other files. Keep the backup.

If you do not know, start with path B. The check tells you: it either prints
"head [0, 0x4000) matches the image" or ends with "REFUSED". After REFUSED,
run the check again with the other file.

The commands
------------
Unplug the pedal, type the line, and plug the pedal in when it says it is
waiting. Use the path A file name in place of the path B one if that is yours.

Windows (Command Prompt opened as administrator, in this folder; not
PowerShell):
  1. Check:    RESCUE.bat --package firmware\BlackBox_023.fwsc
  2. Restore:  RESCUE.bat --write --package firmware\BlackBox_023.fwsc
If Windows offers to format a drive, click Cancel. Never click Format.

Mac or Linux (Terminal, in this folder; Linux has not been tested):
  1. Check:    sudo python3 tools/uboot_rescue.py --package firmware/BlackBox_023.fwsc
  2. Restore:  sudo python3 tools/uboot_rescue.py --write --package firmware/BlackBox_023.fwsc

Run the restore only after the check printed "head [0, 0x4000) matches the
image" and ended with "Read-only run finished, nothing was written". The
restore ends with "Done". Then unplug the cable, switch the pedal off, wait
5 seconds and switch it on. If a run stops in the middle, run the same line
again: it continues.

After path B, DO NOT INSTALL OPENPOCKET ON THE PEDAL. Today's OpenPocket
installer writes over the end of the V22/V23 boot loader, and the pedal goes
dark again. Wait for an installer that supports V22 and V23.

Other files here
----------------
fingerprint.bat  (Mac: python3 tools/head_fingerprint.py)
    Reads your newest backup and says which firmware the pedal had. With a
    file name it reads that file: a backup, or a firmware file (.fwsc).
extract-flash.bat firmware\BlackBox_023.fwsc
    (Mac: python3 tools/extract_flash.py firmware/BlackBox_023.fwsc)
    Takes flash.bin out of a package, for use with --image in place of
    --package. You only need this if --package reports an error.
SHA256SUMS.txt
    The SHA-256 of every file in this kit. Check one on Windows with
    certutil -hashfile RESCUE.bat SHA256
    and all of them on a Mac with  shasum -a 256 -c SHA256SUMS.txt

Backups are saved in the OpenPocket-rescue folder of your user (home) folder.

Status
------
The tool has restored one real pedal on path A, from a Mac. Path B (V22 and
V23) was tested against a simulated pedal with M-VAVE's real V22 and V23
files, and it matches one community report of a real pedal restored this way.
The OpenPocket team has not yet run path B on a real pedal, and has not run
the Windows launcher on a real pedal.

Credits and licences
--------------------
BlackBox_020.fwsc, BlackBox_022.fwsc and BlackBox_023.fwsc are the official
BlackBox firmware V20, V22 and V23, published by M-VAVE. They remain M-VAVE's
property. OpenPocket is not affiliated with M-VAVE or Donner.
RESCUE.bat and tools\ come from OpenPocket
(https://github.com/VinSpee/undertone, GPL-3.0-only; LICENSE.txt). The USB
boot protocol is documented by kagaimiq/jl-uboot-tool and konsumer/fm1buddy.
Flashing is at your own risk.
