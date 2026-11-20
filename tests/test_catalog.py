#!/usr/bin/env python3
"""tools/catalog.py: the catalog of the firmwares a site offers (release.json), from firmware/catalog.json and the
manifests and packages the build made. No official file needed.
  python3 tests/test_catalog.py [--built]
--built: also the catalog the build wrote (docs/firmware/release.json): every firmware of firmware/catalog.json is on
it, and every package it names is there with the catalog's SHA-256 (`node tools/op.mjs all` runs it so)."""
import hashlib
import json
import shutil
import struct
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import catalog  # noqa: E402
import mkloader  # noqa: E402
import pxpkg  # noqa: E402

fails = 0


def ok(cond, what):
    global fails
    print(f"{what:<100} {'ok' if cond else 'FAIL'}")
    fails += 0 if cond else 1


def refused(fn):
    try:
        fn()
    except SystemExit as e:
        return str(e)
    return ""


sdk = ROOT / "firmware" / "sdk" / "packager"
ota = mkloader.wrap(b"\x00\x00FELUCCA-LOADER-1" + bytes(900))
plain = b"\x00" * 64 + b"OP-PRODUCT:PocketX_900" + b"\0" * 10 + bytes(range(256)) * 40
alt = plain + struct.pack("<8s16s16s8sBB2x", b"OP-APP:", b"starter", b"STARTER", b"v01", 0x0D, 1)
with tempfile.TemporaryDirectory() as t:
    t = Path(t)
    site, apps, source = t / "site", t / "apps", t / "catalog.json"
    two = {"firmwares": [{"id": "openpocket", "name": "OpenPocket", "what": "The first firmware."},
                         {"id": "starter", "what": "The second firmware.", "note": "A warning."}]}
    source.write_text(json.dumps(two))
    pxpkg.build(plain, ota, site, 42, None, sdk)
    cat = catalog.build(source, site, apps)
    ok([f["id"] for f in cat["firmwares"]] == ["openpocket"], "a firmware that is not built is left out of the catalog")
    pxpkg.build(alt, ota, apps / "starter", 1, None, sdk, "starter")
    cat = catalog.build(source, site, apps)
    op, st = cat["firmwares"]
    ok(cat["catalog"] == 1 and (op["id"], op["name"], op["version"], op["what"]) == ("openpocket", "OpenPocket", "v42", "The first firmware."),
       "the first firmware: its name from the source, its version from its manifest, its line")
    ok((st["id"], st["name"], st["version"], st["what"]) == ("starter", "STARTER", "v01", "The second firmware.")
       and st["note"] == "A warning." and "note" not in op,
       "an app on the base: its name and version from its product descriptor, in the source's order; its warning, when it has one")
    ok(op["manifest"] == "manifest.json" and st["manifest"] == "apps/starter/manifest.json"
       and [p["file"] for p in st["packages"]] == ["apps/starter/starter-PocketX_901.fwsc", "apps/starter/starter-BlackBox_901.fwsc"],
       "every path is relative to the catalog, as a site serves the files")
    ok(all((site / p["file"]).stat().st_size == p["size"] and hashlib.sha256((site / p["file"]).read_bytes()).hexdigest() == p["sha256"]
           and p["product"] == f"{p['model']}_942" for p in op["packages"]), "each package: its model, identity, size and SHA-256")
    pkg = apps / "starter" / "starter-PocketX_901.fwsc"
    good = pkg.read_bytes()
    pkg.write_bytes(good[:-1] + bytes([good[-1] ^ 1]))
    ok("not the package its manifest describes" in refused(lambda: catalog.build(source, site, apps)), "a package that is not its manifest's: refused")
    pkg.write_bytes(good)
    shutil.copytree(apps / "starter", apps / "palette")
    source.write_text(json.dumps({"firmwares": [{"id": "palette", "what": "x"}]}))
    ok("is the manifest of 'starter', not of 'palette'" in refused(lambda: catalog.build(source, site, apps)), "another firmware's manifest in a firmware's folder: refused")
    source.write_text(json.dumps({"firmwares": [{"id": "openpocket", "what": "x"}]}))
    ok("needs a \"name\"" in refused(lambda: catalog.build(source, site, apps)), "the first firmware without a name in the source: refused")
    source.write_text(json.dumps({"firmwares": [{"id": "../x", "what": "x"}]}))
    ok("needs an id" in refused(lambda: catalog.build(source, site, apps)), "an id that is a path: refused")

if "--built" in sys.argv:
    site = ROOT / "docs" / "firmware"
    want = [f["id"] for f in json.loads((ROOT / "firmware" / "catalog.json").read_text())["firmwares"]]
    cat = json.loads((site / "release.json").read_text())
    ok([f["id"] for f in cat["firmwares"]] == want, f"the build's catalog lists every firmware of the release: {', '.join(want)}")
    served = lambda f: ROOT / "build" / f if f.startswith("apps/") else site / f   # (tools/serve.py: firmware/apps/ is build/apps/)
    ok(all(hashlib.sha256(served(p["file"]).read_bytes()).hexdigest() == p["sha256"] for f in cat["firmwares"] for p in f["packages"]),
       "the build's catalog: every package is there, with the catalog's SHA-256")
print("catalog: " + (f"{fails} FAILED" if fails else "ok"))
sys.exit(1 if fails else 0)
