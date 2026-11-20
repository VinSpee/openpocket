THE FIRMWARE FOLDER
===================

The rescue tool restores M-VAVE's official BlackBox firmware. The commands in
README.txt read the file from this folder, under one of these names:

  BlackBox_020.fwsc   M-VAVE V20   path A
      SHA-256 c0fef191b860d3b20f77fc492a24986eef2d12df44bb5e38355684820e26cb08
  BlackBox_022.fwsc   M-VAVE V22   path B
      SHA-256 9515f98361988ccd0b4e20c3568140397089712d33ebb0909934567e0b264ebc
  BlackBox_023.fwsc   M-VAVE V23   path B
      SHA-256 67b087c9bd2aae98f09d63d70a6ab7fbe54826c4389b0dc874a4f183ab84415c

If this folder holds only this file, the kit came without M-VAVE's firmware.
Get the file for your path and put it here under the name above:

- The full kit at https://openpocket.vinspee.me/rescue/ holds all three files.
- M-VAVE's download page, https://www.m-vave.com/appdownload, offers the
  current BlackBox file. In November 2026 that was V23. It is called
  BlackBox.fwsc: rename it.

To see which firmware a file is, run in the kit folder:

  Windows:        fingerprint.bat firmware\BlackBox_023.fwsc
  Mac or Linux:   python3 tools/head_fingerprint.py firmware/BlackBox_023.fwsc

It prints the file's SHA-256, says whether it is one of the three official
files above, and names the path it belongs to. A file that is not one of the
three can still be a correct official file of another version. The rescue tool
checks every file again before it writes: a damaged file stops with ERROR, and
a file for another firmware family stops with REFUSED. Nothing is written in
either case.
