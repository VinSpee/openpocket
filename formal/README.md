# Formal models of OpenPocket's concurrent parts

This folder holds five TLA+ models of the places in OpenPocket where steps can happen in more than one order.
The places are the flash and the second core, the editor's watch, the preset store, the hand-over of a change of settings to the audio side, and the NAM network on two cores.
[everystate](https://github.com/mattzcarey/everystate) checks every reachable state of each model.

A model checker verifies the model, not the code. A pass says that the design keeps its promise at the model's bounds.
`FINDINGS.md` lists what the checks found, with each trace told in the code's words.

## Run the checks

The checker is pinned in `package.json` and `package-lock.json`. It needs Node 20 or later and no Java.

```sh
npm ci --prefix formal
node formal/check.mjs
```

`check.mjs` runs every configuration, then builds `conformance/flash_trace.c` with `cc` and checks the runs it records.
The whole run takes about 5 seconds on a laptop. The `formal` job of `.github/workflows/build.yml` runs the same two commands.

To run one configuration and read its trace, call the checker from this folder.

```sh
cd formal
npx everystate -config FlashHold.cfg FlashHold.tla
```

### Read the result

Each line that `check.mjs` prints names a configuration, its result, and whether that result is the expected one.

```
ok    FlashHoldFixed.cfg                 ok, 250 states
ok    PresetStoreStrict.cfg              invariant OnlyConfirmedOrInProgress  (expected: note N1)
ok    nm_commit : FlashHoldFixed.cfg     conforms
```

Some configurations are expected to fail.
A configuration that models a finding fails until the code is fixed. A mutation breaks the model on purpose, to show that the check can fail.
`check.mjs` exits with 1 when any result differs from the expected one, including a configuration that passes when it is expected to fail.

## The models

| Model | Promise | Code it stands for |
|---|---|---|
| `FlashHold.tla` | While the SFC is off, CPU1 is in its RAM loop. | `st_audio_hold` (`firmware/px_store.h`), `nm_commit` (`firmware/px_models.h`), `ir_commit` (`firmware/px_irs.h`), `a_block`, `a_send`, `a_collect`, and `lp_job` (`firmware/px_audio.h`), `fl_enter` (`firmware/bsp/chip/fm1_flash.h`) |
| `EditorWatch.tla` | Once nothing is in flight and no push is owed, the editor shows the pedal's sound. | `ed_service` and `ed_handle` (`firmware/px_editor.h`), `Editor.request` and `Editor._onMessage` (`docs/pxedit.js`), `ping` (`docs/editor.js`), `sendParam` (`docs/edsound.js`) |
| `PresetStore.tla` | After a power cut at any step, the next start loads the last confirmed save or a later one. | `st_commit` and `st_load` (`firmware/px_store.h`) |
| `KnobHandoff.tla` | `a_kp`, the cabinet store, and the engine's preset are never read by one side while the other writes them. | `audio_apply` and `a_commit` (`firmware/px_audio.h`), `px_engine_prepare` and `px_engine_knobs` (`engine/px_dsp.c`) |
| `LayerSplit.tla` | Each ring of the NAM network belongs to one core at a time, and every layer runs every block once, in order. | `pxn_lp_a` and `pxn_lp_b` (`engine/px_nam.c`) |

In each `.tla` file, the comment on an action names the function the action stands for.

The table below lists every configuration. A row that starts with "The code" models the code as it is.

| Configuration | What it models | Expected |
|---|---|---|
| `FlashHold.cfg` | The code: `nm_scan` before `st_audio_hold(0)`. The interrupt's two callbacks run back to back. | Passes |
| `FlashHoldFixed.cfg` | The code, and the main loop may run between the two callbacks | Passes |
| `FlashHoldOneShot.cfg` | The code without the NAM layer split, assuming that the main loop never runs between `a_enc` and `a_dec` | Passes |
| `FlashHoldOneShotGap.cfg` | The code without the NAM layer split, without that assumption | Passes |
| `FlashHoldNoCollect.cfg` | Mutation: a hold without `a_collect` | `XipExclusion` fails |
| `EditorWatch.cfg` | The code: a command other than STATE keeps a push that is owed (`ed_w.owed`) | Passes |
| `EditorWatchNoOwed.cfg` | Mutation: every answered command resets `ed_w.hash`, as the code did before the fix of F2 | `InSync` fails |
| `PresetStore.cfg` | The code | Passes |
| `PresetStoreStrict.cfg` | The code, against a stricter promise | `OnlyConfirmedOrInProgress` fails (N1) |
| `PresetStoreOverwrite.cfg` | Mutation: a save writes over the copy in use | `NoLostSave` fails |
| `KnobHandoff.cfg` | The code | Passes |
| `KnobHandoffNoWait.cfg` | Mutation: `audio_apply` does not wait for `a_kpend` | `NoTornHandoff` fails |
| `LayerSplit.cfg` | The code | Passes |
| `LayerSplitNoCheck.cfg` | Mutation: `pxn_lp_a` does not read `ld` | `RingExclusive` fails |

### Check the code against the flash model

`conformance/flash_trace.c` runs the firmware's own `px_store.h`, `px_models.h`, and `px_irs.h` on a PC, in the way `tests/test_store.c` runs `px_store.h`.
It records each mask, each `a_collect`, each flash access, and each unmask of `st_save`, `nm_commit`, `nm_erase`, `ir_commit`, and `ir_erase`.
`everystate conform` then checks that each recorded run is a behaviour of `FlashHold.tla`.
All five are behaviours of `FlashHold.cfg` and of `FlashHoldFixed.cfg`.
Before the fix of finding F1, `FlashHoldFixed.cfg` accepted only `st_save`, which was F1 seen from the code's side.
`tests/test_flash_hold.c` checks the same order without the checker, in the host tests.

`repro/editor_watch.mjs` reproduced finding F2 on the simulated pedal of the editor tests. `FINDINGS.md` gives the command. Since the fix both of its runs end the same, and `tests/test_editor.mjs` holds the case as a test. It depends on timing, so `check.mjs` does not run it.

## Why these parts

A part is modelled when more than one actor touches the same state, the code already shows a lock, a flag, or an order it relies on, and a small model can hold it.

- **The flash and the second core.** Three actors: the main loop, the audio interrupt, and CPU1. The code names its own rule and has a hold for it.
- **The editor's watch.** The pedal and the editor each keep a copy of the sound. A hash and a 40 ms limit decide what the pedal sends.
- **The preset store.** A power cut can fall on any step of a save, and the code that recovers (`st_load`) is in the repository.
- **The hand-over of settings.** `a_kpend` is a flag between the main loop and an audio side that also runs on CPU1, with no lock.
- **The NAM network on two cores.** Two cores share the network's rings, and the layer where they split moves from block to block.

These parts are left out.

- **The order of `a_enc` and `a_dec`** (`firmware/px_aorder.h`). The two callbacks never run at the same time, and `tests/test_aorder.c` runs the file itself. `FlashHold.tla` keeps one fact about them: the interrupt can enter once for each, and the main loop can run in between.
- **The request counters of `lp_job`.** `FlashHold.tla` reduces `a_lpreq` and `a_lpdone` to one boolean, `served`, and holds `a_lpquit` as `quit`. `a_lparg` and the timing fields that set the split are not modelled.
- **The USB driver** (`firmware/bsp/chip/usb.c`, MIDI and USB audio). It is felucca-bsp's with small local changes, its rings have one writer each, and `tests/test_usb_audio.c` runs it.
- **The Bluetooth test image** (`firmware/ble`). It is not the release, and its tasks run from the main loop.
- **The update** (`firmware/bsp/chip/ota.c`, `loader/`, `docs/pxota.js`, `tools/flash.mjs`). One side acts at a time. The orders that matter are power cuts, and the chip's boot code decides what follows one. That code is not in the repository, so a model would only restate guesses about it. A model of the update should first look at `ota_unstage`, which erases the first sector of the staged loader before the sector that holds the update record.
- **The sound across a preset change.** A separate change to it (trails that ring on after the change) is in progress. `KnobHandoff.tla` models a full change only as mask, `a_collect`, and `px_engine_set`.
- **The load figures** (`audio_prof`). The main loop reads and clears counters that the audio side adds to. A lost count changes a percentage on a diagnostics page.
- **Two editors on one pedal.** The editor matches a reply by its command only. The models assume one editor.

## Bounds and assumptions

| Model | Bounds | Assumed, or left out |
|---|---|---|
| `FlashHold.tla` | One commit, any number of blocks | A job on CPU1 may read the flash at any time. CPU1 never fails. USB's interrupt is off with the others. Whether the main loop runs between `a_enc` and `a_dec` is a constant, `Gap`. |
| `EditorWatch.tla` | 3 values of one setting. `EditorWatch.cfg` has 3 changes on the pedal, 2 PINGs, and 2 changes in the editor. `EditorWatchNoOwed.cfg` has 2, 1, and 1. | One setting stands for the preset. The editor watches all the time (WATCH and its 5 s limit are left out). The USB link loses nothing and keeps the order. The editor's timeout and retry are left out. |
| `PresetStore.tla` | 4 saves, any number of power cuts and flash errors | A cut erase or program leaves junk that fails the CRC. A header cut in its program is taken as no header, although the CRC covers the slots and not the header. |
| `KnobHandoff.tla` | 3 calls of `audio_apply` | CPU1 runs the whole chain (`dsp_job`). With a NAM amp on two cores `a_commit` runs only on CPU0, and that case is not modelled. The fade around a full change is left out. |
| `LayerSplit.tla` | 4 layers, 4 blocks, a queue of 2 (the pedal has 23 layers and a queue of 6) | CPU1 never stops. The wait that gives up (`pxn_lp_late`) is left out. |

Every model takes a write by one core as seen by the other in the order it was made. The code relies on `csync` and, between `a_kp` and `a_kpend`, on a compiler barrier for that.

## Keep the models true to the code

The models were last confirmed against the code at commit `5fce8f5`. `EditorWatch.tla` was read again against the fix of F2, and `FlashHold.tla` and its configurations follow the fix of F1.

When you change one of the files below, read the model's actions against the new code and run `node formal/check.mjs`.

| When you change | Look at |
|---|---|
| `st_audio_hold`, or any code that erases, programs, or reads the flash at run time | `FlashHold.tla`, `conformance/flash_trace.c`, and `tests/test_flash_hold.c` |
| `a_enc`, `a_dec`, `a_block`, `a_send`, `a_collect`, `lp_job`, or the mailbox of `fm1_cpu1.h` | `FlashHold.tla` |
| `ed_service`, `ed_handle`, or the editor's `request`, `_onMessage`, `ping`, and `sendParam` | `EditorWatch.tla` |
| `st_commit` or `st_load` | `PresetStore.tla` |
| `audio_apply`, `a_commit`, `dsp_block`, `px_engine_change`, `px_engine_prepare`, or `px_engine_knobs` | `KnobHandoff.tla` |
| `pxn_lp_a`, `pxn_lp_b`, `pxn_lp_split`, or `lp_split` | `LayerSplit.tla` |

When you fix a finding, the configurations that model the code as it was no longer describe the code.
Set their constant to the fixed value, change their rows in `check.mjs` to `ok`, and say in `FINDINGS.md` which commit fixed the finding.
F1 was fixed this way: `FlashHold.cfg`, `FlashHoldOneShot.cfg`, and `FlashHoldOneShotGap.cfg` have `ScanInsideHold = TRUE`, and every row of `TRACES` in `check.mjs` expects `conforms`.
