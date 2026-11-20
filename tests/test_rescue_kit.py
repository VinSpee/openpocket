#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The rescue kit (rescue/build_kit.py): the build gives the pinned bytes, and the files of an unzipped kit restore a
simulated UBOOT pedal (tests/uboot_sim.py) on a V20, a V22 and a V23 flash head. No hardware.
  python3 tests/test_rescue_kit.py [DIR]

The commands are the ones docs/RESCUE.md tells a user to type, run in the kit folder by the kit's own
tools/uboot_rescue.py main(). Only the USB transport and JieLi's loader are replaced. Without DIR the three firmware
files are packages built here to the two layouts (tests/fw_layouts.py). With DIR, a folder that holds M-VAVE's official
V20, V22 and V23 packages, the full kit is built and its real files are used.
"""
import contextlib, hashlib, io, json, os, random, subprocess, sys, tempfile, unittest, zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OFFICIAL_DIR = Path(sys.argv.pop(1)) if len(sys.argv) > 1 and Path(sys.argv[1]).is_dir() else None
sys.path.insert(0, str(ROOT / "rescue"))
import build_kit

TMP = tempfile.TemporaryDirectory()
WORK = Path(TMP.name)
ZIP, SUM = build_kit.build(WORK / "out", OFFICIAL_DIR)
with zipfile.ZipFile(ZIP) as z:
    z.extractall(WORK)
KIT = WORK / build_kit.TOP

# the kit's own tools, not the repository's (uboot_sim then takes the same module)
sys.path.insert(0, str(KIT / "tools"))
import uboot_rescue as ur
sys.path.insert(0, str(ROOT / "tests"))
import uboot_sim
from fw_layouts import flash_image
import pxpkg

assert Path(ur.__file__).resolve().parent == (KIT / "tools").resolve() and uboot_sim.ur is ur, "not the kit's tool"


def noise(seed, n):
    return random.Random(seed).randbytes(n)


LOADER = noise(1, ur.LOADER_SIZE)
(WORK / "loader.bin").write_bytes(LOADER)
ur.LOADER_SHA256 = hashlib.sha256(LOADER).hexdigest()
SECTOR = ur.SECTOR
DARK = (0x4000, 0xA3000)                       # what an OpenPocket install writes, whatever head the pedal has
VERSIONS = {"020": (0x4000, 0xA5000), "022": (0x6000, 0x9F000), "023": (0x6000, 0x9F000)}
if not OFFICIAL_DIR:
    for n, (v, (base, length)) in enumerate(VERSIONS.items()):
        flash = flash_image(base, noise(10 + n, 0x80000), length)
        (KIT / f"firmware/BlackBox_{v}.fwsc").write_bytes(pxpkg.ufw(flash, b"OTA" * 100, f"BlackBox_{v}"))
IMAGE = {v: ur.load_image(package=KIT / f"firmware/BlackBox_{v}.fwsc") for v in VERSIONS}


def tearDownModule():
    TMP.cleanup()


def pedal(version):
    """A pedal that had this official firmware and went dark after an OpenPocket install."""
    flash = bytearray(noise(20, ur.FLASH_SIZE))
    flash[:len(IMAGE[version])] = IMAGE[version]
    flash[DARK[0]:DARK[1]] = noise(21, DARK[1] - DARK[0])
    return bytes(flash)


class SimUsb:
    """The pedal is not plugged in at first (the tool prints its waiting line), then it is there."""
    POLL = 0

    def __init__(self, sim):
        self.sim, self.calls = sim, 0

    def open_pedal(self):
        self.calls += 1
        if self.calls == 1:
            return None
        return self.sim if self.calls == 2 else self.sim.reenumerate()


class Kit(unittest.TestCase):
    def setUp(self):
        self.home = Path(tempfile.mkdtemp(dir=WORK))
        self.backups = self.home / "OpenPocket-rescue"
        self.addCleanup(os.chdir, os.getcwd())
        os.chdir(KIT)

    def rescue(self, flash, *argv):
        """A line of the guide, typed in the kit folder. Returns (exit code, what the window showed, the pedal)."""
        sim = uboot_sim.SimPedal(flash, LOADER)
        self.addCleanup(setattr, ur, "transport", ur.transport)
        ur.transport = lambda: SimUsb(sim)
        shown = io.StringIO()
        with contextlib.redirect_stdout(shown), contextlib.redirect_stderr(shown):
            rc = ur.main([*argv, "--loader", str(WORK / "loader.bin"), "--backup-dir", str(self.backups)])
        return rc, shown.getvalue(), sim

    def helper(self, *argv):
        return subprocess.run([sys.executable, *argv], cwd=KIT, capture_output=True, text=True,
                              env={**os.environ, "HOME": str(self.home), "USERPROFILE": str(self.home)})

    def assert_untouched(self, sim, before, shown):
        self.assertEqual((sim.erases(), sim.writes()), ([], []), shown)
        self.assertTrue(bytes(sim.flash) == before, "the flash changed")

    def restores(self, version, file, source="--package"):
        """The read-only run writes nothing and saves the backup; --write leaves exactly the file's flash.bin."""
        before = pedal(version)
        for old in self.backups.glob("*"):
            old.unlink()
        image = Path(file).read_bytes() if source == "--image" else IMAGE[Path(file).stem[-3:]]
        rc, shown, sim = self.rescue(before, source, file)
        self.assertEqual(rc, 0, shown)
        self.assertIn("head [0, 0x4000) matches the image", shown)
        self.assertIn("Read-only run finished, nothing was written.", shown)
        self.assert_untouched(sim, before, shown)
        self.assertTrue([p.read_bytes() for p in self.backups.glob("blackbox-*.dump.bin")] == [before], "the backup")
        rc, shown, sim = self.rescue(before, "--write", source, file)
        self.assertEqual(rc, 0, shown)
        self.assertIn("sectors written and verified", shown)
        after = bytes(sim.flash)
        self.assertTrue(after[:len(image)] == image, "the flash is not the file's flash.bin")
        self.assertTrue(after[len(image):] == before[len(image):], "the flash above the image changed")
        self.assertTrue(all(DARK[0] <= a < len(image) for a in sim.erases() + sim.writes()))

    def refuses(self, version, file, source="--package"):
        before = pedal(version)
        for write in ([], ["--write"]):
            rc, shown, sim = self.rescue(before, *write, source, file)
            self.assertEqual(rc, 2, shown)
            self.assertIn("REFUSED: the flash head [0, 0x4000) is not the image's head", shown)
            self.assertIn("backup saved", shown)
            self.assert_untouched(sim, before, shown)

    # ---- the build
    def test_the_build_gives_the_same_bytes_and_they_are_the_pinned_ones(self):
        again, _ = build_kit.build(WORK / "again", OFFICIAL_DIR)
        self.assertTrue(again.read_bytes() == ZIP.read_bytes(), "two builds differ")
        plain, digest = (ZIP, SUM) if not OFFICIAL_DIR else build_kit.build(WORK / "plain")
        pin = (ROOT / "rescue/kit.sha256").read_text()
        self.assertEqual(f"{digest}  {plain.name}\n", pin, "the kit's files changed: raise rescue/VERSION and run "
                         "python3 rescue/build_kit.py --pin (rescue/README.md)")
        manifest = json.loads((plain.parent / "manifest.json").read_text())
        self.assertEqual(manifest, {"kit": "OpenPocket-rescue-kit", "version": build_kit.version(), "file": plain.name,
                                    "size": plain.stat().st_size, "sha256": digest, "firmware": False})
        self.assertEqual(plain.name, f"OpenPocket-rescue-kit-{build_kit.version()}.zip")

    def test_the_kit_without_firmware_holds_no_file_of_m_vave(self):
        plain, _ = build_kit.build(WORK / "plain")
        with zipfile.ZipFile(plain) as z:
            names = z.namelist()
            self.assertEqual(names, [f"OpenPocket-rescue-kit/{n}" for n in (
                "README.txt", "RESCUE.bat", "extract-flash.bat", "fingerprint.bat", "LICENSE.txt", "tools/uboot_rescue.py",
                "tools/pocketx_fw.py", "tools/extract_flash.py", "tools/head_fingerprint.py", "firmware/README.txt",
                "SHA256SUMS.txt")])
            self.assertTrue(all(i.compress_type == zipfile.ZIP_STORED and i.date_time == build_kit.STAMP for i in z.infolist()))
            for line in z.read("OpenPocket-rescue-kit/SHA256SUMS.txt").decode().splitlines():
                digest, name = line.split("  ")
                self.assertEqual(hashlib.sha256(z.read(f"OpenPocket-rescue-kit/{name}")).hexdigest(), digest, name)
            for name in ("RESCUE.bat", "tools/uboot_rescue.py", "tools/pocketx_fw.py"):
                self.assertTrue(z.read(f"OpenPocket-rescue-kit/{name}") == (ROOT / name).read_bytes(), name)
            for name, (_, eol) in build_kit.FILES.items():
                data = z.read(f"OpenPocket-rescue-kit/{name}")
                self.assertEqual(data.count(b"\r\n"), data.count(b"\n") if eol == b"\r\n" else 0, name)
            self.assertIn(f"Kit version {build_kit.version()}\r\n".encode(), z.read(names[0]))

    def test_the_full_kit_needs_all_three_official_files(self):
        with self.assertRaises(SystemExit) as stop:
            build_kit.build(WORK / "none", self.home)
        self.assertIn("BlackBox_020.fwsc", str(stop.exception))
        if OFFICIAL_DIR:
            with zipfile.ZipFile(ZIP) as z:
                for digest, (_, name) in build_kit.OFFICIAL.items():
                    self.assertEqual(hashlib.sha256(z.read(f"OpenPocket-rescue-kit/firmware/{name}")).hexdigest(), digest)
            self.assertTrue(ZIP.name.endswith("-full.zip"))

    # ---- the three paths of the guide
    def test_path_a_a_v20_pedal_with_the_v20_file(self):
        self.restores("020", "firmware/BlackBox_020.fwsc")

    def test_path_b_a_v23_pedal_with_the_v23_file(self):
        self.restores("023", "firmware/BlackBox_023.fwsc")

    def test_path_b_a_v22_pedal_with_the_v22_file(self):
        self.restores("022", "firmware/BlackBox_022.fwsc")

    def test_v22_and_v23_are_one_group(self):
        self.restores("022", "firmware/BlackBox_023.fwsc")
        self.restores("023", "firmware/BlackBox_022.fwsc")

    def test_the_file_of_the_other_path_is_refused_and_nothing_is_written(self):
        self.refuses("023", "firmware/BlackBox_020.fwsc")
        self.refuses("022", "firmware/BlackBox_020.fwsc")
        self.refuses("020", "firmware/BlackBox_023.fwsc")
        self.refuses("020", "firmware/BlackBox_022.fwsc")

    def test_a_damaged_or_missing_file_stops_before_the_pedal_is_asked(self):
        raw = bytearray((KIT / "firmware/BlackBox_023.fwsc").read_bytes())
        raw[len(raw) // 2] ^= 1
        (self.home / "damaged.fwsc").write_bytes(raw)
        for file, said in ((self.home / "damaged.fwsc", "CRC check failed"), ("firmware/BlackBox_099.fwsc", "not found")):
            rc, shown, sim = self.rescue(pedal("023"), "--write", "--package", str(file))
            self.assertEqual((rc, sim.seen), (1, []), shown)
            self.assertIn(said, shown)
        r = self.helper("tools/extract_flash.py", str(self.home / "damaged.fwsc"))
        self.assertEqual(r.returncode, 1)
        self.assertIn("CRC check failed", r.stderr)

    # ---- the two helpers
    def test_extract_flash_gives_the_image_for_the_image_route(self):
        out = self.home / "BlackBox_023-flash.bin"
        r = self.helper("tools/extract_flash.py", "firmware/BlackBox_023.fwsc", str(out))
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("identity BlackBox_023, every CRC passed", r.stdout)
        self.assertTrue(out.read_bytes() == IMAGE["023"])
        self.restores("023", str(out), "--image")
        self.restores("022", str(out), "--image")
        self.refuses("020", str(out), "--image")
        self.assertEqual(self.helper("tools/extract_flash.py").returncode, 1)

    def test_fingerprint_names_the_head_of_the_newest_backup(self):
        r = self.helper("tools/head_fingerprint.py")
        self.assertEqual(r.returncode, 1)
        self.assertIn("no backup", r.stderr)
        seen = {}
        for version in VERSIONS:
            for p in self.backups.glob("*"):
                p.unlink()
            self.rescue(pedal(version), "--package", f"firmware/BlackBox_{version}.fwsc")
            r = self.helper("tools/head_fingerprint.py")
            self.assertEqual(r.returncode, 0, r.stderr)
            seen[version] = hashlib.sha256(IMAGE[version][:0x4000]).hexdigest()
            self.assertIn(f"fingerprint: {seen[version][:12]}  (full: {seen[version]})", r.stdout)
            if OFFICIAL_DIR:
                self.assertIn("Path A: use firmware/BlackBox_020.fwsc" if version == "020" else
                              "Path B: use firmware/BlackBox_023.fwsc", r.stdout)
        self.assertEqual(seen["022"], seen["023"])
        self.assertNotEqual(seen["020"], seen["023"])
        self.assertEqual(self.helper("tools/head_fingerprint.py", "no-such-file.bin").returncode, 1)

    def test_fingerprint_reads_a_firmware_file_and_says_whether_it_is_official(self):
        r = self.helper("tools/head_fingerprint.py", "firmware/BlackBox_023.fwsc")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("identity BlackBox_023, every CRC passed", r.stdout)
        self.assertIn(hashlib.sha256(IMAGE["023"][:0x4000]).hexdigest(), r.stdout)
        self.assertIn("official:    M-VAVE V23. Its name in the firmware folder: BlackBox_023.fwsc" if OFFICIAL_DIR else
                      "official:    not one of the three official files this kit knows", r.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=1)
