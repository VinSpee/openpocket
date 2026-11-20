# The rescue kit

The zip a person downloads to restore a BlackBox that no longer starts. The guide for that person is
[docs/RESCUE.md](../docs/RESCUE.md). This page is for whoever changes or builds the kit.

## What is here

| Path | What it is |
| --- | --- |
| `build_kit.py` | Assembles the zip and its `manifest.json` |
| `kit/` | The files that exist only in the kit: its `README.txt`, the two helpers and their launchers, `firmware/README.txt` |
| `VERSION` | The kit's number. It is in the zip's name and in the kit's `README.txt` |
| `kit.sha256` | The pin: the SHA-256 and the name of the kit this tree builds |

The rescue tool itself is not copied here. The build takes `RESCUE.bat`, `tools/uboot_rescue.py`, `tools/pocketx_fw.py`
and `LICENSE` from the repository.

## Building

```
node tools/op.mjs kit            # or: make kit, or: python3 rescue/build_kit.py [OUT_DIR]
```

It writes `docs/rescue/OpenPocket-rescue-kit-<version>.zip` and `docs/rescue/manifest.json`. Both are ignored by git.
The installer page (`docs/index.html`) reads the manifest and offers the zip with its SHA-256. The build needs only
Python 3.

The zip unpacks to one folder, `OpenPocket-rescue-kit`. That name is fixed, because the guides tell the user to go to
it. The number is in the zip's name, so a new kit is never served under the name of an old one.

## The pin

The same sources give the same bytes on every computer: the files are stored in a fixed order, with fixed times and
fixed line endings, and without compression, because compressed bytes depend on the zlib that wrote them.
`tests/test_rescue_kit.py` builds the kit twice and compares its SHA-256 with `kit.sha256`.

A change to any file of the kit, including the rescue tool, is a new kit:

1. Raise `VERSION` by one.
2. Run `python3 rescue/build_kit.py --pin`, which writes the new sum to `kit.sha256`.
3. Commit both with the change. The pull request then shows that the published kit changes.

## M-VAVE's firmware

The kit of this repository holds none of M-VAVE's files, and no M-VAVE file belongs in this repository or in its
releases. The user adds the official file. `kit/firmware/README.txt` names the three files and their SHA-256.

The full kit, for the project's own site, also holds M-VAVE's V20, V22 and V23 packages:

```
python3 rescue/build_kit.py --official DIR [OUT_DIR]
```

`DIR` is a folder on your computer that holds the three packages under any names. Each is found by its SHA-256, and
the build stops when one is missing. The result is `OpenPocket-rescue-kit-<version>-full.zip`, with a manifest that
says it holds firmware. Give `OUT_DIR` when the result must not replace the manifest in `docs/rescue/`.

## Tests

```
python3 tests/test_rescue_kit.py          # part of tests/run_tests.sh
python3 tests/test_rescue_kit.py DIR      # the same with the full kit and M-VAVE's real packages from DIR
```

The test unzips the kit and runs the commands of the guide with the kit's own `tools/uboot_rescue.py` against the
simulated pedal of `tests/uboot_sim.py`. Only the USB transport and JieLi's loader are replaced. It covers a pedal on
a V20, a V22 and a V23 flash head: the read-only run, the restore, the refusal of the other path's file, the `--image`
route and the two helpers. Without `DIR` the firmware files are packages built by the test to the two layouts
(`tests/fw_layouts.py`), because the official ones are not in the repository.

Nothing here runs on a real pedal or on Windows. `docs/RESCUE.md` states what has been run on hardware.
