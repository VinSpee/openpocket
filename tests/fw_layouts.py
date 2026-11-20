# SPDX-License-Identifier: GPL-3.0-only
"""Flash images built to the two layouts of the official BlackBox firmware, for tests that have no official file:
the app area at 0x4000 (up to V20) or at 0x6000 (V22 and V23), named by the head's app_dir_head entry."""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import pocketx_fw as fw  # noqa: E402

FLASH_LEN = 0x9F000
APP = bytes(range(256)) * 40
CFG = b"cfg_tool" * 8


def entry(name, addr, ln, attr, dcrc):
    e = bytearray(32)
    struct.pack_into("<HHIIBBH", e, 0, 0, dcrc, addr, ln, attr, 0, 0)
    e[16:16 + len(name)] = name.encode()
    struct.pack_into("<H", e, 0, fw.crc16(e[2:32]))
    return bytes(e)


def flash_image(base, app=APP, length=FLASH_LEN):
    """A flash with its app area at base: head entries (ENC), then the area (SFC) with the files and the regions."""
    head = bytearray(b"\xFF" * base)
    head[:0x20] = b"\x20\x02" + bytes(30)
    for i, e in enumerate((entry("uboot.boot", 0xA0, 0x100, 0, 0xFFFF), entry("isd_config.ini", 0x1A0, 0x20, 0, 0xFFFF),
                           entry("app_dir_head", base, 0xFFFFFFFF, 0x81, 0xFFFF), entry("key_mac", 0xFF000, 0x1000, 0, 0xFFFF))):
        head[0x20 + i * 32:0x40 + i * 32] = fw.enc(e)
    area = bytearray(b"\xFF" * (length - base))
    app_at, cfg_at, end = 0x120, 0x120 + len(app), 0x120 + len(app) + len(CFG)
    area[app_at:app_at + len(app)] = app
    area[cfg_at:cfg_at + len(CFG)] = CFG
    area[0x20:0x40] = entry("app.bin", app_at, len(app), 0x82, fw.crc16(app))
    area[0x40:0x60] = entry("cfg_tool.bin", cfg_at, len(CFG), 0x82, fw.crc16(CFG))
    area[0x60:0x80] = entry("VM", length, 0xBE000 - length, 0x12, 0x01FF)   # a region: the CRC is for data outside the package
    area[0x80:0xA0] = entry("PRCT", 0, length, 0x92, 0xFFFF)
    area[0:0x20] = entry("app_area_head", 0x2000120, end, 0x83, fw.crc16(area[0x20:end]))
    return bytes(head) + fw.sfc(bytes(area), 0)
