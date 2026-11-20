#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""build_kit.py - assemble the BlackBox rescue kit, a zip a user unpacks next to nothing else (docs/RESCUE.md).

  python3 rescue/build_kit.py [OUT_DIR]                  OUT_DIR/OpenPocket-rescue-kit-<version>.zip and OUT_DIR/manifest.json
  python3 rescue/build_kit.py --official DIR [OUT_DIR]   the full kit, OpenPocket-rescue-kit-<version>-full.zip
  python3 rescue/build_kit.py --pin                      also write the kit's sum to rescue/kit.sha256

OUT_DIR is docs/rescue by default, where the installer page looks for the kit. The kit holds this repository's rescue
tool and the files of rescue/kit. It holds none of M-VAVE's firmware: the user adds the official file. --official adds
M-VAVE's V20, V22 and V23 packages from DIR, each found by its SHA-256 whatever its name; that kit is not for this
repository or its releases.

The same sources give the same bytes on every computer: fixed order, fixed times, fixed line endings, no compression
(a compressed zip depends on the zlib that wrote it). rescue/kit.sha256 pins the kit, and tests/test_rescue_kit.py
compares. A change to any file of the kit is a new kit: raise rescue/VERSION and run this with --pin.
"""
import argparse, hashlib, json, sys, zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
sys.path.insert(0, str(HERE / "kit" / "tools"))
from head_fingerprint import OFFICIAL  # noqa: E402

TOP = "OpenPocket-rescue-kit"                  # the folder in the zip: the guides name it, so it never changes
STAMP = (2026, 11, 20, 0, 0, 0)
LF, CRLF = b"\n", b"\r\n"
# name in the kit -> (source, line ending); Windows opens the .bat and .txt files
FILES = {
    "README.txt": (HERE / "kit/README.txt", CRLF),
    "RESCUE.bat": (REPO / "RESCUE.bat", CRLF),
    "extract-flash.bat": (HERE / "kit/extract-flash.bat", CRLF),
    "fingerprint.bat": (HERE / "kit/fingerprint.bat", CRLF),
    "LICENSE.txt": (REPO / "LICENSE", LF),
    "tools/uboot_rescue.py": (REPO / "tools/uboot_rescue.py", LF),
    "tools/pocketx_fw.py": (REPO / "tools/pocketx_fw.py", LF),
    "tools/extract_flash.py": (HERE / "kit/tools/extract_flash.py", LF),
    "tools/head_fingerprint.py": (HERE / "kit/tools/head_fingerprint.py", LF),
    "firmware/README.txt": (HERE / "kit/firmware/README.txt", CRLF),
}


def version():
    return int((HERE / "VERSION").read_text())


def official_files(folder):
    """M-VAVE's three packages from a folder, by SHA-256: {name in the kit: bytes}. All three, or the build stops."""
    by_sum = {hashlib.sha256(p.read_bytes()).hexdigest(): p for p in sorted(Path(folder).glob("*.fwsc"))}
    missing = [f"{name} ({what}, SHA-256 {digest})" for digest, (what, name) in OFFICIAL.items() if digest not in by_sum]
    if missing:
        sys.exit(f"ERROR: {folder} lacks " + "; ".join(missing))
    return {f"firmware/{name}": by_sum[digest].read_bytes() for digest, (_, name) in OFFICIAL.items()}


def contents(official=None):
    """{name in the kit: bytes}, in the zip's order, SHA256SUMS.txt last."""
    blobs = {}
    for name, (src, eol) in FILES.items():
        text = src.read_bytes().replace(b"\r\n", b"\n").replace(b"{{VERSION}}", str(version()).encode())
        blobs[name] = text.replace(b"\n", eol)
    if official:
        blobs.update(official_files(official))
    sums = "".join(f"{hashlib.sha256(d).hexdigest()}  {n}\n" for n, d in blobs.items())
    blobs["SHA256SUMS.txt"] = sums.encode()
    return blobs


def build(out, official=None):
    """Write the zip and manifest.json into out. Returns (path of the zip, its SHA-256)."""
    out.mkdir(parents=True, exist_ok=True)
    path = out / f"{TOP}-{version()}{'-full' if official else ''}.zip"
    with zipfile.ZipFile(path, "w", zipfile.ZIP_STORED) as z:
        for name, data in contents(official).items():
            info = zipfile.ZipInfo(f"{TOP}/{name}", STAMP)
            info.external_attr = 0o644 << 16
            info.create_system = 3
            z.writestr(info, data)
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    manifest = {"kit": TOP, "version": version(), "file": path.name, "size": path.stat().st_size, "sha256": digest,
                "firmware": bool(official)}
    (out / "manifest.json").write_text(json.dumps(manifest, indent=1) + "\n")
    return path, digest


def main(argv=None):
    ap = argparse.ArgumentParser(description="Assemble the BlackBox rescue kit.")
    ap.add_argument("out", nargs="?", default=str(REPO / "docs" / "rescue"), help="output folder (default docs/rescue)")
    ap.add_argument("--official", metavar="DIR", help="add M-VAVE's V20, V22 and V23 packages from DIR (the full kit)")
    ap.add_argument("--pin", action="store_true", help="write the kit's sum to rescue/kit.sha256")
    a = ap.parse_args(argv)
    if a.pin and a.official:
        ap.error("--pin is for the kit without M-VAVE's files")
    path, digest = build(Path(a.out), a.official)
    line = f"{digest}  {path.name}\n"
    if a.pin:
        (HERE / "kit.sha256").write_text(line)
    print(f"rescue kit: {path} ({path.stat().st_size} bytes)\n{line}", end="")
    return 0


if __name__ == "__main__":
    sys.exit(main())
