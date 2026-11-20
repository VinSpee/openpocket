#!/usr/bin/env python3
"""tools/pocketx_fw.py reads the app area from the address the flash head names (app_dir_head): 0x4000 up to V20, 0x6000
from V22. Packages built here to both layouts; the official packages too when the files are given (skipped otherwise).
  python3 tests/test_pocketx_fw.py [OFFICIAL.fwsc ...]"""
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import pocketx_fw as fw  # noqa: E402
import pxpkg  # noqa: E402
import uboot_rescue as ur  # noqa: E402

FLASH_LEN = 0x9F000
APP = bytes(range(256)) * 40
CFG = b"cfg_tool" * 8

fails = 0


def ok(cond, what):
    global fails
    print(f"{what:<96} {'ok' if cond else 'FAIL'}")
    fails += 0 if cond else 1


def entry(name, addr, ln, attr, dcrc):
    e = bytearray(32)
    struct.pack_into("<HHIIBBH", e, 0, 0, dcrc, addr, ln, attr, 0, 0)
    e[16:16 + len(name)] = name.encode()
    struct.pack_into("<H", e, 0, fw.crc16(e[2:32]))
    return bytes(e)


def flash_image(base):
    """A flash with its app area at base: head entries (ENC), then the area (SFC) with the files and the regions."""
    head = bytearray(b"\xFF" * base)
    head[:0x20] = b"\x20\x02" + bytes(30)
    for i, e in enumerate((entry("uboot.boot", 0xA0, 0x100, 0, 0xFFFF), entry("isd_config.ini", 0x1A0, 0x20, 0, 0xFFFF),
                           entry("app_dir_head", base, 0xFFFFFFFF, 0x81, 0xFFFF), entry("key_mac", 0xFF000, 0x1000, 0, 0xFFFF))):
        head[0x20 + i * 32:0x40 + i * 32] = fw.enc(e)
    area = bytearray(b"\xFF" * (FLASH_LEN - base))
    app_at, cfg_at, end = 0x120, 0x120 + len(APP), 0x120 + len(APP) + len(CFG)
    area[app_at:app_at + len(APP)] = APP
    area[cfg_at:cfg_at + len(CFG)] = CFG
    area[0x20:0x40] = entry("app.bin", app_at, len(APP), 0x82, fw.crc16(APP))
    area[0x40:0x60] = entry("cfg_tool.bin", cfg_at, len(CFG), 0x82, fw.crc16(CFG))
    area[0x60:0x80] = entry("VM", 0x9F000, 0x1F000, 0x12, 0x01FF)       # a region: the CRC is for data outside the package
    area[0x80:0xA0] = entry("PRCT", 0, FLASH_LEN, 0x92, 0xFFFF)
    area[0:0x20] = entry("app_area_head", 0x2000120, end, 0x83, fw.crc16(area[0x20:end]))
    return bytes(head) + fw.sfc(bytes(area), 0)


def run(*args):
    return subprocess.run([sys.executable, str(ROOT / "tools" / "pocketx_fw.py"), *map(str, args)], capture_output=True, text=True)


def package_roundtrip(pkg, base, what, tmp):
    """info passes every check, unpack writes the files, a changed app.bin packs and still passes, an untouched one is byte for byte."""
    path = tmp / f"{what}.fwsc"
    path.write_bytes(pkg)
    r = run("info", path)
    ok(r.returncode == 0 and f"app area at {base:#x}" in r.stdout and "ALL CHECKS PASS" in r.stdout, f"{what}: info reads the area at {base:#x}, every check passes")
    out = tmp / what
    run("unpack", path, out)
    ok((out / "app.bin").read_bytes() == APP and (out / "cfg_tool.bin").read_bytes() == CFG and not (out / "VM").exists(),
       f"{what}: unpack writes app.bin and cfg_tool.bin from the area, no region")
    run("pack", out, tmp / f"{what}.same.fwsc")
    ok((tmp / f"{what}.same.fwsc").read_bytes() == pkg, f"{what}: pack of an untouched unpack is the package byte for byte")
    (out / "app.bin").write_bytes(b"\xA5" + APP[1:])
    run("pack", out, tmp / f"{what}.edit.fwsc")
    r = run("info", tmp / f"{what}.edit.fwsc")
    ok(r.returncode == 0 and "ALL CHECKS PASS" in r.stdout, f"{what}: a changed app.bin packs and every check still passes")
    _, L, _, u = fw.load(tmp / f"{what}.edit.fwsc")
    flash = next(L[e["off"]:e["off"] + e["len"]] for e in u["ents"] if e["name"] == "flash.bin")
    ok(fw.flash_decrypt(flash)[base + 0x120] == 0xA5, f"{what}: the changed byte sits at {base + 0x120:#x}")


with tempfile.TemporaryDirectory() as t:
    tmp = Path(t)
    for base, what in ((0x4000, "old layout"), (0x6000, "V22 layout")):
        flash = flash_image(base)
        ok(fw.app_base(flash) == base, f"{what}: app_base names {base:#x}")
        package_roundtrip(pxpkg.ufw(flash, b"OTA" * 100, "BlackBox_023"), base, what, tmp)
        if base == 0x6000:
            _, L, _, u = fw.load(tmp / f"{what}.fwsc")
            image = ur.load_image(package=tmp / f"{what}.fwsc")
            ok(image == next(L[e["off"]:e["off"] + e["len"]] for e in u["ents"] if e["name"] == "flash.bin"),
               f"{what}: uboot_rescue loads the package's flash.bin")

    nohead = bytearray(flash_image(0x6000))
    nohead[0x60:0x80] = b"\xFF" * 32
    try:
        fw.app_base(bytes(nohead))
        ok(False, "no app_dir_head: refused")
    except ValueError:
        ok(True, "no app_dir_head: refused")
    _, L, _, u = fw.load(tmp / "V22 layout.fwsc")
    bad = [k for k, v in fw.package_checks(L, u).items() if not v]
    ok(not bad, "package_checks of the V22 layout: no failed check")

    for f in sys.argv[1:]:
        r = run("info", f)
        ok(r.returncode == 0 and "ALL CHECKS PASS" in r.stdout, f"{Path(f).name}: info, every check passes")
        run("unpack", f, tmp / "official")
        run("pack", tmp / "official", tmp / "official.fwsc")
        ok((tmp / "official.fwsc").read_bytes() == Path(f).read_bytes(), f"{Path(f).name}: unpack then pack is the file byte for byte")

print("OK" if not fails else f"{fails} FAILED")
sys.exit(1 if fails else 0)
