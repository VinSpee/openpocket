#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""extract_flash.py - take flash.bin out of an official BlackBox package (.fwsc), for uboot_rescue.py --image.
Reads and writes files on this computer only; nothing goes to the pedal.

  python3 tools/extract_flash.py firmware/BlackBox_023.fwsc             writes BlackBox_023-flash.bin in this folder
  python3 tools/extract_flash.py firmware/BlackBox_023.fwsc OUT.bin     writes OUT.bin

Every CRC of the package is checked first; a damaged package is not used.
"""
import hashlib, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from head_fingerprint import describe, read_package


def main(argv=None):
    args = sys.argv[1:] if argv is None else argv
    if not 1 <= len(args) <= 2 or args[0] in ("-h", "--help"):
        print(__doc__.strip())
        return 0 if args and args[0] in ("-h", "--help") else 1
    package = Path(args[0]).expanduser()
    tag, flash = read_package(package)
    out = Path(args[1]).expanduser() if len(args) == 2 else Path(f"{tag}-flash.bin")
    out.write_bytes(flash)
    fp, family, _ = describe(flash)
    print(f"package:     {package} (identity {tag}, every CRC passed)")
    print(f"written:     {out} ({len(flash)} bytes, SHA-256 {hashlib.sha256(flash).hexdigest()})")
    print(f"fingerprint: {fp[:12]}  ({family})")
    print(f"next:        run the rescue tool with  --image {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
