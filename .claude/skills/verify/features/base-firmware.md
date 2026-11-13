# Base firmware and apps

## Sub-features
- The base (`firmware/base/`, `docs/BASE-FIRMWARE.md`): the code every image shares.
- OpenPocket built on it: the same bytes as before a change that only moves code.
- THRU (`firmware/apps/thru`): the second app, `build/apps/thru/thru.bin` and its two packages.

## How to get to it (user POV)
A maintainer changes a file under `firmware/base/`, or `px_usb.h`, `app.ld`, `build.sh`. A user sees no difference
in OpenPocket.

## Driving it with op.mjs
1. Before the change: `node tools/op.mjs package`, keep `build/SHA256SUMS`.
2. After it: `node tools/op.mjs package`, compare. The same sums prove that OpenPocket did not change.
3. `node tools/op.mjs app thru`: fails when a base file needs a name from OpenPocket. Read its memory report.
4. `node tools/op.mjs test`: the section `base audio` runs the base's audio stages on the PC (`tests/test_base_audio.c`).

## Gotchas
- `tools/sim.mjs` and `tests/edsim.c` simulate OpenPocket only. No simulator runs THRU.
- THRU's start, its sound, and its USB are verifiable only on a pedal (`verify-pedal`, the captain's call).
- Extracting a function from `app_main` can change OpenPocket's bytes even when the statements are the same. Check
  the sums after every step.
