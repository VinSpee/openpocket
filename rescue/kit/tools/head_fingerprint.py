#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""head_fingerprint.py - say which firmware family a BlackBox backup or firmware file belongs to. Reads a file; nothing
goes to the pedal.

  python3 tools/head_fingerprint.py             the newest backup in the OpenPocket-rescue folder of your home folder
  python3 tools/head_fingerprint.py FILE        a backup (blackbox-<time>.dump.bin), a flash.bin, or a package (.fwsc)

The fingerprint is the SHA-256 of the first 0x4000 bytes (the flash head), the part uboot_rescue.py compares with the
firmware file before it writes anything. For a package it also says whether the file is one of M-VAVE's official ones.
"""
import hashlib, sys
from pathlib import Path

HEAD = 0x4000
# SHA-256 of [0, 0x4000) of each official flash.bin, and of OpenPocket's own packages
HEADS = {
    "d540d2217091c3eb8948bcdcc7133375256cf2d764cf9d024870255e1554ddcf":
        ("M-VAVE V16, V17 or V20", "Path A: use firmware/BlackBox_020.fwsc"),
    "ef3dfea17ea81ba38a9a58f89f3d53f65a487d548d438db2ff0708b3c18daa29":
        ("M-VAVE V22 or V23", "Path B: use firmware/BlackBox_023.fwsc (or BlackBox_022.fwsc)"),
    "dd49e3c322bd57c5aab62d70e7d6512344b0c39fa56253c5ec04f6984229e452":
        ("M-VAVE V8 or V9", "Path C: no file of this kit matches. Stop and keep the backup"),
    "f2a7de218f81711034e88066c295cb77338592719cc08a53433c0d7874220f72":
        ("an OpenPocket package's head", "Path C: no file of this kit matches. Stop and keep the backup"),
}
UNKNOWN = ("not known to this kit", "Path C: stop, keep the backup and send this output to whoever is helping you")
# SHA-256 of M-VAVE's official BlackBox packages, and the name each has in this kit's firmware folder
OFFICIAL = {
    "c0fef191b860d3b20f77fc492a24986eef2d12df44bb5e38355684820e26cb08": ("M-VAVE V20", "BlackBox_020.fwsc"),
    "9515f98361988ccd0b4e20c3568140397089712d33ebb0909934567e0b264ebc": ("M-VAVE V22", "BlackBox_022.fwsc"),
    "67b087c9bd2aae98f09d63d70a6ab7fbe54826c4389b0dc874a4f183ab84415c": ("M-VAVE V23", "BlackBox_023.fwsc"),
}


def fingerprint(data):
    return hashlib.sha256(data[:HEAD]).hexdigest()


def describe(data):
    """(fingerprint, family, what to do) for the start of a flash image."""
    fp = fingerprint(data)
    return (fp,) + HEADS.get(fp, UNKNOWN)


def read_package(path):
    """(identity, flash.bin) of a package whose every CRC passed. Anything else ends the program with the reason."""
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import pocketx_fw
    try:
        _, L, tag, u = pocketx_fw.load(path)
    except OSError as e:
        sys.exit(f"ERROR: cannot read {path}: {e.strerror}")
    except Exception as e:
        sys.exit(f"ERROR: {path} is not a firmware package ({e})")
    bad = [k for k, v in pocketx_fw.package_checks(L, u).items() if not v]
    if bad:
        sys.exit(f"ERROR: {path}: CRC check failed ({', '.join(bad)}); not using it")
    entry = next(e for e in u["ents"] if e["name"] == "flash.bin")
    return tag, L[entry["off"]:entry["off"] + entry["len"]]


def newest_backup():
    folder = Path.home() / "OpenPocket-rescue"
    found = sorted(folder.glob("blackbox-*.dump.bin"))
    if not found:
        sys.exit(f"ERROR: no backup (blackbox-*.dump.bin) in {folder}. Run the read-only check first, or name a file.")
    return found[-1]


def main(argv=None):
    args = sys.argv[1:] if argv is None else argv
    if len(args) > 1 or (args and args[0] in ("-h", "--help")):
        print(__doc__.strip())
        return 0 if args and args[0] in ("-h", "--help") else 1
    path = Path(args[0]).expanduser() if args else newest_backup()
    try:
        data = path.read_bytes()
    except OSError as e:
        sys.exit(f"ERROR: cannot read {path}: {e.strerror}")
    print(f"file:        {path} ({len(data)} bytes)")
    if path.suffix.lower() == ".fwsc":
        digest = hashlib.sha256(data).hexdigest()
        tag, data = read_package(path)
        what, name = OFFICIAL.get(digest, ("not one of the three official files this kit knows", None))
        print(f"package:     identity {tag}, every CRC passed, SHA-256 {digest}")
        print(f"official:    {what}" + (f". Its name in the firmware folder: {name}" if name else ""))
    if len(data) < HEAD:
        sys.exit(f"ERROR: {path} is only {len(data)} bytes: it is not a flash backup")
    fp, family, action = describe(data)
    print(f"fingerprint: {fp[:12]}  (full: {fp})")
    print(f"head:        {family}")
    print(f"next:        {action}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
