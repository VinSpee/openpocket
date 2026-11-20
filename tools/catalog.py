#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The catalog of the firmwares a site offers: release.json, which the installer page reads to show one card per
firmware (docs/index.html). firmware/catalog.json names the firmwares of a release, in the order of their cards, with
one line on what each is and, when there is one, a warning to show with it ("note"). Everything else is read from
what the build made: a firmware's name and version from its manifest, and each package's size and SHA-256 from the
package itself, which must be the manifest's.

  tools/catalog.py [--source firmware/catalog.json] [--site docs/firmware] [--apps build/apps] [--out SITE/release.json]

SITE holds the packages and the manifest of the first firmware, the one with no product descriptor. A firmware that is
an app on the base firmware has its own in APPS/<id>/, which a site serves as SITE/apps/<id>/ (tools/serve.py). Every
path in the catalog is relative to the catalog. A firmware that has not been built is left out, with a line that says
so; firmware/build.sh runs this after every build, so the catalog always describes the packages that are there."""
import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PRIMARY = "openpocket"                  # the id of the firmware with no product descriptor (docs/pxota.js OPENPOCKET)


def entry(src, folder, prefix):
    """one firmware of the catalog from its manifest in folder, or None when it has not been built"""
    path = folder / "manifest.json"
    if not path.is_file():
        return None
    man = json.loads(path.read_text())
    app = man.get("app")
    if (app["id"] if app else PRIMARY) != src["id"]:
        raise SystemExit(f"catalog: {path} is the manifest of {app['id'] if app else PRIMARY!r}, not of {src['id']!r}")
    packages = []
    for p in man["packages"]:
        data = (folder / p["file"]).read_bytes()
        if len(data) != p["size"] or hashlib.sha256(data).hexdigest() != p["sha256"]:
            raise SystemExit(f"catalog: {folder / p['file']} is not the package its manifest describes: build it again")
        packages.append({**p, "file": prefix + p["file"]})
    return {"id": src["id"], "name": app["name"] if app else src["name"],
            "version": app["version"] if app else f"v{man['version']}", "what": src["what"],
            **({"note": src["note"]} if src.get("note") else {}),
            "manifest": prefix + "manifest.json", "packages": packages}


def build(source, site, apps):
    firmwares = []
    for src in json.loads(source.read_text())["firmwares"]:
        if not re.fullmatch(r"[a-z0-9-]{1,15}", src.get("id", "")) or not src.get("what"):
            raise SystemExit(f"catalog: {source}: every firmware needs an id (lowercase letters, digits, -) and a \"what\" line")
        if src["id"] == PRIMARY and not src.get("name"):
            raise SystemExit(f"catalog: {source}: {PRIMARY!r} has no product descriptor, so it needs a \"name\" here")
        e = entry(src, site, "") if src["id"] == PRIMARY else entry(src, apps / src["id"], f"apps/{src['id']}/")
        if e:
            firmwares.append(e)
        else:
            print(f"catalog: {src['id']} is not built, left out")
    return {"catalog": 1, "firmwares": firmwares}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--source", type=Path, default=ROOT / "firmware" / "catalog.json")
    ap.add_argument("--site", type=Path, default=ROOT / "docs" / "firmware")
    ap.add_argument("--apps", type=Path, default=ROOT / "build" / "apps")
    ap.add_argument("--out", type=Path)
    a = ap.parse_args()
    out = a.out or a.site / "release.json"
    cat = build(a.source, a.site, a.apps)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(cat, indent=2) + "\n")
    print(f"catalog {out}: " + (", ".join(f"{f['name']} {f['version']}" for f in cat["firmwares"]) or "no firmware"))


if __name__ == "__main__":
    sys.exit(main())
