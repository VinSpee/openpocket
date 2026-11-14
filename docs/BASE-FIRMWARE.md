# The base firmware

The firmware tree builds more than one image for the pedal. The **base** is the code that every image has, whatever
the image does with the sound. An **app** is one image built on the base. OpenPocket is the first app. THRU
(`firmware/apps/thru`) is the second: the guitar straight to the output, with no effects.

The base exists so that an experiment that does not fit in OpenPocket can be its own image. OpenPocket's engine
region has 40 bytes free, so a different engine needs a different memory layout. An app is installed with the same
installer as OpenPocket, and OpenPocket is put back the same way.

The split did not change OpenPocket. `firmware/openpocket.bin` and both packages are the same bytes as v42 before the
split. Section 8 says how to check that after a change to the base.

## 1. What the base holds

| Part | Files |
| --- | --- |
| The chip: start-up code, vectors, clocks, flash, USB, the second core | `firmware/bsp/`, `firmware/inc/`, `firmware/px_math.c`, `firmware/sdk/` |
| The board: pins, time, the boot guard's record, the screen, rectangles, text | `firmware/base/board.h`, `ui_text.h`, `fonts.h`, `font8x8.h` |
| The controls: the pins of the encoder, buttons, and footswitches, the ADC, the battery, the debounce | `firmware/base/controls.h` |
| Diagnostics kept across a reset: the crash screen, the boot stages, the stacks' depth | `firmware/base/diag.h` |
| The audio's way in and out: the I2S, the block's stages, the load and its bypass, the tuner | `firmware/base/audio.h`, `px_aorder.h`, `px_tuner.h` |
| A schedule for an app without one: the sound made in the interrupt, on one core | `firmware/base/audio_plain.h` |
| The start: `fm1_cstart`, the boot guard, the boot screen, the report of a start that failed, safe mode | `firmware/base/start.h`, `firmware/base/boot_screens.h` |
| USB-MIDI, the update entry for the installer, the UBOOT soft key, USB audio | `firmware/px_usb.h`, `px_uaudio.h`, `px_uaconf.h` |
| The button lights' driver, the cache RAM | `firmware/px_leds.h`, `firmware/px_cacheram.h` |
| The link and the build: sections, fixed regions, RAM code, the memory report | `firmware/app.ld`, `firmware/build.sh`, `tools/ramcode.py`, `tools/check_calls.py` |
| The package and the update loader | `tools/pxpkg.py`, `tools/mkloader.py`, `loader/` |

OpenPocket's own files are `firmware/openpocket.c`, `px_audio.h`, `ui.h`, `ui_colors.h`, `px_lights.h`,
`px_store.h`, `px_models.h`, `px_irs.h`, `px_editor.h`, `px_midi.h`, `px_midi_io.h`, `px_ble_midi.h`,
`firmware/ble/`, `firmware/memory.ld`, and `engine/`.

Only the code that was cut out of `openpocket.c` and `px_audio.h` is in `firmware/base/`. A base file that was already
a file of its own, such as `px_usb.h`, stays at its path. The tests include those files by path, and a file that does
not move merges with upstream changes as before.

THRU is the check on the boundary. It includes the base's files and none of OpenPocket's. A base file that needs a
name from OpenPocket's engine, presets, or screens stops the THRU build.

## 2. What an app is

An app is one C file that includes the base's files in order and puts its own code between them, as `openpocket.c`
does. `firmware/apps/thru/thru.c` is the shortest example. The firmware is one translation unit, and every function
below is `static`.

The app's file defines these names before `base/board.h`:

| Name | Meaning |
| --- | --- |
| `AUDIO_PROBE` | `0`. `1` builds the audio test in place of the app. |
| `DSP_CPU1` | `1` starts the second core for the DSP. `build.sh` reads this line to link the second core's entry. |
| `OP_NAME` | The name on the boot screen. |
| `OP_VERSION_NUM`, `OP_VERSION` | The package's number (`"42"` gives `PocketX_942`) and the text on the screens. |

The app's file provides these functions:

| Function | When the base calls it |
| --- | --- |
| `app_main()` | Once, from `fm1_cstart`. It does not return. |
| `a_block(data, len)` | In the I2S interrupt, with a guitar block. It calls `a_input`, then makes the sound from `a_in` to `a_out`, or starts it on another core. |
| `a_send(d, n)` | In the I2S interrupt, with an output block to fill. It finishes that sound, then calls `a_output`. |
| `a_collect()` | Before a flash write or an update. The sound in progress outside the interrupt is finished when it returns. |
| `ed_service(now)` | In every main-loop pass, before the installer's service. It takes the SysEx frames that are the app's. |

An app that makes its sound in the interrupt includes `base/audio_plain.h` and provides one function in place of the
first three: `app_sound(in, out)`, 128 frames of mono float in each direction. OpenPocket provides the three itself,
in `px_audio.h`, because it shares a block between two cores.

The app may define these names:

| Name | Meaning | Default |
| --- | --- | --- |
| `OP_FLASH_OWN(off, n)` | The flash ranges the app writes its own data to. | None: the app may write the update's staging area only. |
| `OTA_SCRATCH` | A buffer of 1536 bytes lent to an update. | A buffer of its own in `ota.c`. |
| `OP_RAM_CODE` | A label in the app's RAM code. A crash report shows its address. | No such line. |
| `PX_LIGHTS_OFF()` | Turns the button lights off before an update. | Nothing. |
| `OP_UAUDIO`, `px_ua_on` | `1` and the setting's variable, for USB audio. | No USB audio. |

`app_main` calls the base in this order, as both apps do:

1. `lcd_init()`, then `#include "base/boot_screens.h"` in the function's body. The included statements draw the boot
   screen and the report of a failed start, and they declare `int safe`.
2. `input_init()`, `adc_pins_analog()`, `input_baseline()`.
3. `a_hold = 1`, `px_flash_boot()`, `px_usb_start()`, then `audio_io_start()`.
4. In the loop: `fm1_wdt_feed()` and `px_usb_service(now)`.
5. After 6 seconds in the loop: `guard.pending = 0`, `guard.failed = 0`, `op_diag.stage = ST_OK`. The start then
   counts as finished.
6. After 1.5 seconds, and never when `safe` is set: `a_hold = 0`. The app's sound starts.

Steps 4 and 5 keep the pedal recoverable. A start that does not reach step 5 counts as failed. The next start runs in
safe mode with USB up, and two failed starts in a row enter the chip's UBOOT.

## 3. Memory regions

`firmware/app.ld` holds the regions that are the chip's or the boot chain's. They are the same for every app.

| Region | Address | Size | Use |
| --- | --- | --- | --- |
| `XIP` | `0x02000120` | 581,564 | Code and constants, run from the flash. |
| `LOWRAM` | `0x01C00000` | 24,576 | Section `.bss.lo.*`, set to zero at the start. |
| `CRAM` | `0x01F28000` | 28,672 | The cache RAM, section `.cram*`. |
| Stacks | `0x01C74000` | 32,768 | Two stacks with guard bands. |
| `NOINIT` | `0x01C7C000` | 15,696 | Section `.noinit`. It survives a reset. |

The app declares the rest in a file `memory.ld`: the 432 KiB of SRAM from `0x01C08000` up to the stacks, as three
regions in this order:

| Region | Use |
| --- | --- |
| `RAM` | `.data` and `.bss`. |
| `POOL` | Section `.pool`, the engine's memory. The start does not set it to zero. Section `.pool.tail.*` follows it and is set to zero. |
| `RAMTEXT` | Code that runs from RAM. Section `.bss.hi.*` uses its free end and is set to zero. |

`app.ld` includes `memory.ld` and refuses the link when the three regions overlap, are out of order, or reach the
stacks. It also refuses a `RAMTEXT` that the flash code cannot call. `firmware/memory.ld` is OpenPocket's.

| | `RAM` | `POOL` | `RAMTEXT` |
| --- | --- | --- | --- |
| OpenPocket | 96 KiB | 248 KiB | 88 KiB |
| THRU | 64 KiB | 320 KiB | 48 KiB |

`build.sh` prints how much of each region the image uses. It reads the regions from the linker's map, so the report
follows the app's `memory.ld`.

Code runs from RAM when its function is named in a list. `tools/ramcode.py` renames the function's section, and the
code itself does not change. The base's audio path is in the list for every app. The app adds its own names in
`app.sh`.

## 4. Controls

The base reads the controls. It does not decide what a press means.

`base/controls.h` gives the pins (`PA_FX`, `PA_HOME`, `PA_FS_UP`, `PA_FS_DN`, `PH_PUSH`, `PH_MOD`, `PH_AMP`, the encoder
on `PC0` and `PC1`), the idle state taken at the start (`base_pa`, `base_ph`), the debounce (`dbn_press`), the analog
keypad for CAB, DLY, and REV (`keys_decode`), the encoder's table (`QDEC`), the MASTER knob's smoothing
(`master_follow`), and the battery gauge (`bat_read`).

The app's main loop polls them and maps them to its own actions. OpenPocket's loop is in `openpocket.c` and its
actions are in `ui.h`. THRU reads the MASTER knob only.

## 5. Build a second image

An app other than OpenPocket is a folder `firmware/apps/<name>/` with three files:

| File | Content |
| --- | --- |
| `<name>.c` | The app's C file. |
| `memory.ld` | Its three regions. |
| `app.sh` | What `build.sh` needs: `APP_MAIN`, `APP_ENGINE` (the engine's C file, when the app has one of its own), `APP_DEFS` (compiler options), `APP_RAM_DSP` and `APP_RAM_FW` (the functions that run from RAM, in the engine's file and in the app's file). |

Build the app in the pinned container:

```
node tools/op.mjs app thru        # or: make app APP=thru
```

The outputs go to `build/apps/thru/`: `thru.bin`, `PocketX_901.fwsc`, `BlackBox_901.fwsc`, and `manifest.json`. The
OpenPocket build's outputs are not touched, and the public installer never offers the app.

Install the app as you install any package. The identity is `PocketX_9NN` or `BlackBox_9NN` for every app, so the
installer cannot tell apps apart. The boot screen shows `OP_NAME`. To put OpenPocket back, install its package.

```
python3 tools/serve.py index.html build/apps/thru                  # the installer page with the app's packages
node tools/op.mjs flash --dry-run --package build/apps/thru/PocketX_901.fwsc --manifest build/apps/thru/manifest.json
```

THRU has not run on a pedal.

## 6. What the base leaves out

- **The main loop.** Each app writes its own. The polling of the buttons and the encoder is still in OpenPocket's
  `app_main`.
- **Presets.** `px_store.h` is OpenPocket's store, at OpenPocket's addresses, with OpenPocket's preset type. The base
  gives the flash's functions and `OP_FLASH_OWN`. An app that saves anything needs its own store in its own range, and
  that range must not be `0xC0000..0xE0000` or `0xF0000..0xFC000`, which hold OpenPocket's presets, models, and IRs.
- **The editor's protocol and the MIDI map.** `px_editor.h`, `px_midi.h`, and `px_midi_io.h` are OpenPocket's. The web
  editor does not work with another app.
- **Two cores.** The base's schedule uses one core. The schedule that shares a block between two cores is
  OpenPocket's, in `px_audio.h`.
- **The Bluetooth test image and the test channel.** `OP_BLE` and `OP_CHANNEL` build OpenPocket only.
- **The simulator.** `tools/sim.mjs` and `tests/edsim.c` simulate OpenPocket only.
- **The USB device's name.** Every app is the USB device "OpenPocket", because the installer finds the pedal by that
  name.

## 7. Two compromises

Both exist so that OpenPocket's compiled code stays the same.

`base/boot_screens.h` holds statements, not functions, and `app_main` includes it in its body. As functions, the same
lines changed the stack layout of OpenPocket's `app_main`. They can become functions when a changed start can be
checked on a pedal.

The encoder's and the buttons' polling stays in OpenPocket's main loop for the same reason. `master_follow` is the
one piece of the loop that moved, because the compiler made the same code from it.

## 8. Check that OpenPocket did not change

A change to a base file changes every app. A change that only moves code leaves the image the same bytes.

1. Before the change, run `node tools/op.mjs package` and keep `build/SHA256SUMS`.
2. Make the change.
3. Run `node tools/op.mjs package` again and compare `build/SHA256SUMS` with the kept file.
4. Run `node tools/op.mjs app thru`. It fails when a base file has come to need a name from OpenPocket.

The same sums mean that nothing needs a check on a pedal. Different sums mean that the start, the sound, or the
update may have changed, and the change needs the checks of `.claude/skills/verify` and a pedal.

## 9. Merge upstream changes

OpenPocket's upstream does not have `firmware/base/`.

- A file that kept its path merges as before.
- A change upstream to lines that moved conflicts as "deleted by us". Apply the change by hand to the line's new
  file.

  | Moved from | Lines | To |
  | --- | --- | --- |
  | `openpocket.c` | The chip's includes, the pins, the screen, the 8x8 text | `base/board.h` |
  | `openpocket.c` | The controls' pins, the ADC, the battery, the keypad, the debounce, `QDEC`, the MASTER knob's smoothing | `base/controls.h` |
  | `openpocket.c` | The crash screen, `op_diag`, the boot stages, `stack_free` | `base/diag.h` |
  | `openpocket.c` | The boot screen and the report of a failed start, from `app_main` | `base/boot_screens.h` |
  | `openpocket.c` | `rst_names`, `fm1_cstart` | `base/start.h` |
  | `px_audio.h` | The I2S driver's glue, the audio's variables, `dsp_out`, `dsp_load`, the tuner, the input's conversion (now `a_input`), the output's copy (now `a_output`), the I2S start (now `audio_io_start`), `audio_volume` | `base/audio.h` |
  | `px_audio.h` | `b_num`, `b_str`, `b_fix2` | `base/board.h` |
  | `px_usb.h` | The three flash ranges of `FL_RANGE_OK`, the `OTA_SCRATCH` lines | `openpocket.c` |
  | `app.ld` | The regions `RAM`, `POOL`, `RAMTEXT` | `firmware/memory.ld` |

- The version line, the `app_main` loop, and the lists of RAM code in `build.sh` did not move. A release upstream
  merges into them without a conflict.
- A new file upstream is OpenPocket's until someone moves it.

After a merge, do the check of section 8 against upstream's own build of the same release.
