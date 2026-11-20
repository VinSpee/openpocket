# Findings

The models in this folder were checked with everystate 0.4.1 against the code at commit `5fce8f5`.
Two findings are real in the code. One note describes a promise the store does not make. Three models passed.
Nothing in the firmware was changed. A fix changes the firmware's bytes, and the release pin allows that only in a release.

| Id | What | Verdict | Found by |
|---|---|---|---|
| F1 | Saving or erasing a NAM model or an IR reads the flash after the audio hold is released | Real in the code. The effect on a pedal is not tested. | `FlashHold.cfg`, `FlashHoldOneShotGap.cfg` |
| F2 | A request from the editor makes the pedal drop a push it owes | Real in the code. Reproduced on the firmware's editor code. | `EditorWatch.cfg` |
| N1 | A save that returned an error can be the one the next start loads | True of the code. Not a loss. | `PresetStoreStrict.cfg` |

## F1: the model and IR scans read the flash while CPU1 runs a job

**The promise.** While the SFC is off, CPU1 is in its RAM loop.
`fl_enter` switches the SFC off for every erase, program, and SPI read (`firmware/bsp/chip/fm1_flash.h:120`), with the interrupts of CPU0 off.
CPU1 has no interrupts to switch off. Its idle loop is in RAM, and a job is not all in RAM.
`firmware/bsp/chip/fm1_cpu1.h:21` states the rule: "CPU0 turns the flash off only with no job outstanding".
`st_audio_hold` (`firmware/px_store.h:169`) keeps the rule. It masks the audio interrupt and calls `a_collect`.

**The shortest trace.** `FlashHold.cfg` models `nm_commit` as the code runs it. The check stops after 12 states.

| Step | Model action | In the code |
|---|---|---|
| 1 | `Mask` | `nm_commit` calls `st_audio_hold(1)`, which masks the audio interrupt. |
| 2 | `CollectBegin` | `a_collect` finds no job outstanding. |
| 3 | `FlashOff`, `FlashOnThen` | The erases, the programs, and the read back run with the audio held. |
| 4 | `Unmask` | `st_audio_hold(0)` at `firmware/px_models.h:128`. |
| 5 | `BlockDue`, `EncEnter` | The audio interrupt enters. A guitar block comes every 2.9 ms. |
| 6 | `EncRun` | `a_block` reaches `dsp_lp_run`, which starts `lp_job` on CPU1 (`firmware/px_audio.h:248`). |
| 7 | `WorkerServe`, `DecRun` | `lp_job` makes the output. `a_send` takes it and the interrupt returns. `lp_job` keeps running. |
| 8 | `FlashOff` | `nm_scan` at `firmware/px_models.h:129` calls `ota_fread`. The SFC goes off while CPU1 is in `lp_job`. |

`nm_scan` makes 16 such reads and `ir_scan` makes 8. The interrupts of CPU0 come back on between two reads.

**Where.** Four functions release the hold and then scan.
They are `nm_commit` (`firmware/px_models.h:128`), `nm_erase` (`firmware/px_models.h:141`), `ir_commit` (`firmware/px_irs.h:98`), and `ir_erase` (`firmware/px_irs.h:110`).
The editor reaches them with NAM_COMMIT, NAM_ERASE, IR_COMMIT, and IR_ERASE.
`st_commit` and `bs_save` keep every flash access inside the hold.

**Real, or the model's doing?** Real. Two checks tie the trace to the code.

- `formal/conformance/flash_trace.c` runs the firmware's own `px_store.h`, `px_models.h`, and `px_irs.h` on a PC and records each mask, each `a_collect`, each flash access, and each unmask. `everystate conform` accepts all five recorded runs as behaviours of the model of the code. Against the model of the fix it accepts `st_save` and rejects the other four at their `Unmask` line. `node formal/check.mjs` repeats this check.
- The steps of the audio interrupt and of `lp_job` come from reading `firmware/px_audio.h`. No test runs them. `a_send` waits only for the output of `lp_job`, not for its return (`a_lp_post`), so the job is still outstanding when the main loop runs again.

**When it happens.** With a NAM amp on two cores (the layer split), `lp_job` is one job for as long as the preset plays. Every scan read then falls inside it.
Without the layer split, `a_send` waits for the block's job. If the main loop never runs between `a_enc` and `a_dec`, CPU1 is then idle whenever the main loop runs. `FlashHoldOneShot.cfg` checks that case and passes.
`firmware/px_aorder.h` says that the interrupt normally enters once for each callback, a few microseconds apart. If the main loop runs in between, the same violation appears, and `FlashHoldOneShotGap.cfg` shows it in 10 states. Whether the main loop runs there on a pedal was not measured.

**What it does on a pedal.** Not tested. That needs a pedal, and this work did not touch one.
Since v40 most of the per-block code runs from RAM (`firmware/build.sh`, `RAM_DSP` and `RAM_FW`), including `lp_job` and the network.
A job reads the flash only for the code and the constants outside those lists. `px_engine_knobs` is one example, and a block's helper functions and tables are another.
So the scan can pass unnoticed many times. When a job does reach the flash during a read, the outcome is the one `st_audio_hold` exists to prevent.
A fault on CPU1 retires it, and the pedal runs on one core until it is switched off.

**Suggested fix.** Move the scan above the release in the four functions, so that it runs with the audio held.

```c
	nm_scan();
	st_audio_hold(0);
```

`FlashHoldFixed.cfg` models that order and passes, with both ways of using CPU1 and with the gap between the two callbacks.
After the fix, the four recorded runs conform to `FlashHoldFixed.cfg`, and `check.mjs` reports it. Change the four `mismatch` rows in `check.mjs` to `conforms` in the same change.

## F2: a request from the editor makes the pedal drop a push it owes

**The promise.** Once nothing is in flight and no push is owed, the editor shows the sound the pedal has.

**The shortest trace.** `EditorWatch.cfg` stops after 8 states.

| Step | Model action | In the code |
|---|---|---|
| 1 | `Knob` | A control on the pedal changes a setting. |
| 2 | `Push` | `ed_service` sees that the state differs from `ed_w.hash` and sends a STATE frame (`firmware/px_editor.h:336`). The next push is at least 40 ms away. |
| 3 | `Knob` | The control changes the setting again inside those 40 ms. This change waits for its push. |
| 4 | `SendPing` | The editor sends its PING. It sends one every second (`docs/editor.js:55`). |
| 5 | `Handle` | `ed_handle` answers and ends with `ed_w.hash = ed_state_hash()` (`firmware/px_editor.h:324`). The hash now covers the change of step 3. |
| 6 | `Recv`, `Recv` | The editor takes the STATE frame of step 2 and the PING reply. |

After step 6 nothing is in flight and the pedal sees no difference, so it never sends the second change. The editor shows the value of step 1.

**Where.** `firmware/px_editor.h:324` runs for every command the pedal answers, also for the ones that change nothing and carry no state.
PING is the common one. The NAM_WRITE and IR_WRITE frames of an upload and the SLOT_GET frames that read the bank do the same.

**Real, or the model's doing?** Real, and reproduced. `formal/repro/editor_watch.mjs` drives the simulated pedal of the editor tests, which runs the firmware's own `px_editor.h`.

```sh
cc -O1 -o build/edsim tests/edsim.c engine/px_dsp.c firmware/px_math.c -lm
node formal/repro/editor_watch.mjs build/edsim
```

```
a PING in the 40 ms: the pedal then sent [PING]; the editor shows 30, the pedal has 77: the editor is stale
no request in the 40 ms: the pedal then sent [STATE]; the editor shows 77, the pedal has 77: the same
```

It gave these two lines on each of three runs on this Mac.

**When it happens.** The last step of a knob turned on the pedal must fall within 40 ms of a push, and a request must arrive before those 40 ms end.
With one PING a second that is rare for one turn. During a model upload or while the editor reads the bank, requests arrive all the time and most changes made on the pedal are dropped.

**What it does.** The editor shows an old value until the next change on either side.
The sound and a save on the pedal are right, because `SAVE` stores the pedal's own preset.
What the editor does with its own copy is wrong meanwhile. An export of the current sound writes the old value to the file (`docs/edpresets.js`, `exportOne`).

**Suggested fix.** In `ed_handle`, note before the command whether a push is owed, which is `ed_state_hash() != ed_w.hash`.
After the command, set `ed_w.hash` to the state only when no push was owed or the reply is a STATE frame, which carries the whole state.
Otherwise mark the push as owed, with a flag that `ed_service` also tests, so that the next push carries both changes.

`EditorWatchFixed.cfg` models that rule and passes, with the property that the editor ends up showing the pedal's sound.
The model also refuted a simpler rule, which leaves the hash alone for every command, a STATE request included, while a push is owed. With that rule the hash can stay at a value the editor no longer shows, and a change on the pedal back to that value is never sent.

## N1: a save that returned an error can be the one the next start loads

`PresetStore.cfg` passes. After a power cut at any step, with a cut erase or program leaving junk, the next start loads the last confirmed save or a later one.
A stricter promise fails, and `PresetStoreStrict.cfg` records it. The stricter promise says that a start never loads a save that returned an error.
The trace is one save whose header program reports an error after the header is whole on the flash. `st_commit` returns an error and `st_cur` stays. The copy is valid, and `st_load` takes it at the next start.
The user is told the save failed and finds it saved. Nothing is lost, and the next save writes over that same copy. No change is suggested.

## What held

| Model | Promise | States | Mutation that fails |
|---|---|---|---|
| `PresetStore.cfg` | A power cut never loses a confirmed save. | 812 | A save that writes over the copy in use (`PresetStoreOverwrite.cfg`). |
| `KnobHandoff.cfg` | `a_kp`, the cabinet store, and the engine's preset are never read while written, and `px_engine_set` runs alone. | 124 | `audio_apply` without `a_wait` (`KnobHandoffNoWait.cfg`). |
| `LayerSplit.cfg` | Each ring of the NAM network belongs to one core at a time, and every layer runs every block once, in order. | 5975 | `pxn_lp_a` taking layers without reading `ld` (`LayerSplitNoCheck.cfg`). |
| `FlashHoldFixed.cfg` | F1's fix keeps the SFC rule, and `a_collect` always ends. | 250 | A hold without `a_collect` (`FlashHoldNoCollect.cfg`). |

A pass covers the model at its bounds. `formal/README.md` lists the bounds and what each model leaves out.
