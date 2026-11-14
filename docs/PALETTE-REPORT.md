# PALETTE: measured levels, costs, and the checks left for a pedal

This report belongs to [PALETTE.md](PALETTE.md). Nothing in it was measured on a pedal. Every figure comes from the
engine's own code on a computer or from the pedal compiler's output. Section 5 lists what a pedal must still show.

## 1. Result

- All twenty effects stay within 1.5 dB of the guitar's loudness at AMOUNT 0, 50, and 100, except where the effect is a
  change of level by its nature (section 2).
- The heaviest chain of four effects is estimated at 25 % to 38 % of one core's block. The pedal has two cores, and
  PALETTE uses one (section 3).
- The engine uses 126,240 bytes of the 311,296 bytes the product declares for it. Its per-sample code is 17,504 bytes
  in a RAM code window of 65,536 bytes (section 4).
- OpenPocket's image and packages are still the release's bytes. `node tools/op.mjs all` ends with the release pin.

## 2. Each effect on the standard signal

The signal is the base firmware's test guitar: a note and its fifth, sawtooth, plucked every 0.4 s at about -12 dBFS
and decaying, the notes walking two octaves. It lasts twelve seconds, and the first two are left out of the levels.
One effect runs alone, with the shared knobs at their centres. PITCH is set a fifth up, and FILTER at AMOUNT 50 is
measured at 30, because their centres do nothing.

Loudness is the RMS of both outputs against the guitar's, in dB. Peak is the highest sample of the three runs, as a
part of full scale. The guitar's own peak is 0.197.

| Module | Effect | Loudness at AMOUNT 0 | at 50 | at 100 | Peak | Cycles a sample | % of a block (A) | Host, ns a sample | % of a block (B) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Character | Drive | 0.0 | 0.0 | +0.1 | 0.395 | 449 | 5.5 | 13.03 | 5.0 |
|  | Sweeten | 0.0 | -0.2 | -0.3 | 0.214 | 126 | 1.5 | 3.52 | 1.3 |
|  | Fuzz | -1.5 | +0.1 | +0.8 | 0.196 | 503 | 6.2 | 16.06 | 6.1 |
|  | Howl | -1.3 | +0.1 | +0.5 | 0.324 | 565 | 6.9 | 25.75 | 9.8 |
|  | Swell | -0.3 | -3.8 | -6.4 | 0.197 | 84 | 1.0 | 3.01 | 1.1 |
| Movement | Doubler | 0.0 | -0.6 | 0.0 | 0.224 | 111 | 1.4 | 2.71 | 1.0 |
|  | Vibrato | 0.0 | 0.0 | 0.0 | 0.197 | 184 | 2.3 | 4.66 | 1.8 |
|  | Phaser | +0.1 | -0.7 | -0.1 | 0.351 | 346 | 4.2 | 36.58 | 13.9 |
|  | Tremolo | 0.0 | -0.3 | +0.3 | 0.290 | 104 | 1.3 | 3.50 | 1.3 |
|  | Pitch | 0.0 | -0.2 | 0.0 | 0.278 | 251 | 3.1 | 6.63 | 2.5 |
| Diffusion | Cascade | +0.9 | +1.2 | +3.0 | 0.320 | 118 | 1.4 | 3.40 | 1.3 |
|  | Reels | +0.9 | +1.2 | +3.1 | 0.321 | 123 | 1.5 | 3.71 | 1.4 |
|  | Space | 0.0 | +0.1 | -0.1 | 0.255 | 679 | 8.3 | 8.47 | 3.2 |
|  | Collage | +0.9 | +1.2 | +3.2 | 0.332 | 90 | 1.1 | 2.47 | 0.9 |
|  | Reverse | 0.0 | +0.5 | -0.1 | 0.300 | 113 | 1.4 | 2.78 | 1.1 |
| Texture | Filter | -2.5 | -0.2 | -12.7 | 0.307 | 90 | 1.1 | 7.02 | 2.7 |
|  | Squash | -0.7 | -0.5 | +0.8 | 0.203 | 66 | 0.8 | 2.70 | 1.0 |
|  | Cassette | -0.1 | 0.0 | 0.0 | 0.303 | 315 | 3.9 | 11.64 | 4.4 |
|  | Broken | 0.0 | 0.0 | 0.0 | 0.245 | 169 | 2.1 | 7.46 | 2.8 |
|  | Interference | 0.0 | -0.7 | -0.1 | 0.480 | 161 | 2.0 | 4.48 | 1.7 |
| The chain | Copy, MIX, level, fades, ceiling | | | | | 144 | 1.8 | 3.33 | 1.3 |

Three rows differ from the guitar's loudness on purpose:

- **Swell** takes the start of each note away, which is the effect.
- **Filter** at its ends cuts most of the signal. At 100 the tilt style is a high-pass at 2.6 kHz.
- **Cascade, Reels, and Collage** add repeats to the guitar, and at AMOUNT 100 they feed back until they oscillate.

These levels are a starting point for voicing by ear. They say that no effect is far louder or quieter than another
on this signal. They do not say that an effect sounds right.

## 3. Processor cost

Both estimates follow the feasibility report's method, on the finished engine.

**Estimate A, a cycle count.** `tools/cyclecount.py` reads the pedal compiler's disassembly of `engine.o`, finds each
loop, and adds up one turn with the costs measured on a pedal (`docs/HARDWARE-HANDBOOK.md`, section 1). A loop that
runs once a channel counts twice. A Diffusion loop runs once for two samples, and 6.4 cycles are added for its closing
branch, which the script does not count. Space is its tick loop plus eight turns of its line loop. One per cent of a
block is 81.6 cycles a sample at 360 MHz. The script leaves out three costs the handbook documents, so it may read
low. The feasibility report checked the method against a loop whose load on a pedal is known, and it agreed within a
factor of two.

**Estimate B, host time converted.** `tests/palette_render.c` times each effect on the signal of section 2 on an
Apple M3 Pro, with `-O2 -fno-vectorize -fno-slp-vectorize`. The time is the process's CPU time, the least of 15 runs,
less the same engine with every module off. In the same run it times three blocks of OpenPocket's engine whose load
on a pedal an earlier task read with `node tools/op.mjs dsp` on v40:

| OpenPocket's block | Host, ns a sample | On a pedal, % of a block | % a ns |
| --- | --- | --- | --- |
| Reverb, room | 21.39 | 9 | 0.42 |
| Delay, tape | 9.90 | 3 | 0.30 |
| Modulation, chorus | 9.39 | 4 | 0.43 |

Column B uses the mean, 0.38 % a ns. The range is 0.30 to 0.43. The computer was busy during the run (a load average
of 33 on 12 cores), which is why the least of the runs is used and why the reference blocks are timed in the same
run. The feasibility report measured 0.45 on a quieter machine. The pedal's loads are whole per cents, so the factor
of a 3 % block carries a rounding error of up to a sixth.

**The two estimates** agree within a factor of 1.5 for 17 of the 20 effects. They disagree on Phaser, Space, and
Filter, as they did in the feasibility report, and the cause is still not established. The planning figure for an
effect is the larger of the two.

**Whole chains.** The time includes the chain's own code.

| Chain | A: % | B: % (range) |
| --- | --- | --- |
| Howl, Phaser, Space, Cassette: the heaviest effect of each module, timed as a chain | 25.1 | 36.4 (28.7 to 41.2) |
| The same, in the order Space, Howl, Cassette, Phaser | 25.1 | 32.5 (25.7 to 36.8) |
| Howl, Pitch, Space, Cassette | 23.9 | 23.5 (18.5 to 26.5) |
| Drive, Doubler, Collage, Broken | 11.8 | 12.3 (9.7 to 13.9) |
| The heaviest chain, taking the larger estimate for each effect | 38.2 | |

**Reading.** The heaviest chain costs 25 % to 38 % of one core's block at 360 MHz. Under m-bootloader 1.2 the clock is
320 MHz, and the same chain costs 28 % to 43 %. The base bypasses a sound that stays above 85 % for eight blocks, so
the estimate leaves a factor of two before that limit. The feasibility report estimated 25 % to 37 % for its sketch.

Three things are not in these figures:

- An effect change clears the module's memory in the audio interrupt, once. Diffusion's is 90 KB. The estimate is
  0.3 ms, a tenth of a block, in the one block where that module does not run.
- Coming back from bypass clears the memory of every module that is on, in one block.
- The engine always runs two channels. The input is mono, so a module that comes before the first stereo effect could
  run one channel and cost half. That saving is not taken.

A pedal must confirm all of this (section 5).

## 4. Memory against the declared regions

From the build's own report (`node tools/op.mjs app palette`):

```
ramcode: build/apps/palette/obj/engine.o: 22 section(s), 17504 bytes of code to RAM
pool tail : 0 bytes, 185056 left before the RAM code
hi RAM    : RAM code 21860, data 0, free 43664 of 65536 bytes (the RAM code window)
RAM       : 22912 of 65536 bytes (42624 free)
OK -> palette.bin: 74476 bytes
```

| Region (`firmware/apps/palette/memory.ld`) | Size | Used | Free | Content |
| --- | --- | --- | --- | --- |
| `POOL` | 311,296 | 126,240 | 185,056 | The engine: Diffusion's line 90,112, Movement's and Texture's 16,384 each, the rest state |
| `RAMTEXT` | 65,536 | 21,860 | 43,664 | The engine's per-sample code 17,504, the base's audio path |
| `RAM` | 65,536 | 22,912 | 42,624 | `.data` and `.bss`: the store's copy in RAM, the base |
| Flash image | 581,564 | 74,476 | | OpenPocket's image is 363,852 |

The 185 KB left in `POOL` is kept for the looper of a later version: about 4 s at 16 bits and 22.05 kHz.

A sound is 28 bytes and a preset 40. The store holds 80 presets in 3,212 bytes of the 19,184 the base allows.

No function of the engine's per-sample code refers to data in the flash. `pl_run` reads the table of effect functions
there four times a block, and the unit lengths of Space when that effect starts.

## 5. Checks on a pedal

Flashing a pedal is the owner's call. Run this list on one pedal, in order. Stop at the first step that fails and
note the screen. OpenPocket itself needs none of this: its image is the release's bytes.

Before you start:

1. Have the pedal's official package and its SD-card backup at hand ([ALT-FIRMWARE.md](ALT-FIRMWARE.md), section 6).
2. Run `node tools/op.mjs all`. It must exit with 0, and its output must hold the release pin's line and "all palette
   simulator tests passed".
3. With the pedal on OpenPocket, save one preset under a name you will recognise, and note it.

Install:

4. Run `node tools/op.mjs flash --dry-run --app palette`. It names the pedal's firmware and PALETTE v01 as what would
   replace it. It writes nothing.
5. Run `node tools/op.mjs flash --confirm-flash --app palette`, or `node tools/op.mjs serve app palette` and press
   "Install PALETTE v01". The pedal shows UPDATE, restarts, and shows PALETTE v01 on its boot screen.

Sound, load, and late blocks:

6. Play. Preset 1, CLEAN ROOM, is heard. MASTER changes the volume. The last line of the screen shows DSP, PEAK, and
   LATE. LATE must stay at 0 through this whole list.
7. Listen with headphones, then at the output jack. Note whether the jack carries two channels: preset 3, WIDE
   DOUBLE, is wide in headphones.
8. Step through the 20 factory presets with the right footswitch. For each, note DSP and PEAK, and note a click or a
   gap at the change. No preset may show OVERLOAD.
9. Press AMP, then press it again four times, playing all the while. Each press is the next Character effect, without
   a click. Note DSP for each. Do the same with MOD, DLY, and CAB. Section 2 has the estimate for each effect: A and B
   are per cent of a block, as DSP is.
10. Build the heaviest chain: on any preset, set Howl, Phaser, Space, and Cassette, all on. Note DSP and PEAK. The
    estimate is 25 % to 38 %, or 28 % to 43 % under m-bootloader 1.2. Send Control Change 78 at 127 and play hard: the
    output must distort softly, not crackle.
11. Hold a footswitch: BYPASS shows and the guitar is as it is. Hold it again: the effects are back without a click.
    Note PEAK after this step: it includes the block that clears every module.
12. Press DLY, then FX, turn the knob: Diffusion moves along the chain, and the sound changes without a click. Press FX.
13. Tap REV twice: the screen's TEMPO line on the HOME page shows the tempo, and a Cascade repeat follows it.

Controls, presets, and MIDI:

14. Hold AMP: Character goes off and its tile dims. Hold it again.
15. Change a sound, hold PUSH for a second: SAVED shows. Switch the pedal off and on: it starts on that preset as saved.
16. From a computer, send Program Change 5, Control Change 69 at 127, and Control Change 16 at 50: the preset, Diffusion's
    AMOUNT, and Character's effect (Fuzz) follow. Send a MIDI clock: the TEMPO line shows CLOCK and its tempo.
17. Open `editor.html` and press Connect. It says that the pedal runs PALETTE v01 and does not open.

Safe mode, the way back, and the data:

18. Switch the pedal off, on, and off again within 3 seconds, then on. If the record of an unfinished start survives
    this pedal's power switch, the screen shows LAST START FAILED, then PALETTE with SAFE MODE, the guitar passes, and
    the installer still finds the pedal. If the pedal starts as usual, note that and go on.
19. Run `node tools/op.mjs flash --confirm-flash`. It ends with "the pedal reports PocketX_942 (OpenPocket v42)" or the
    BlackBox's line. The preset saved in step 3 is there, and so are the NAM models and the cabinet IRs.
20. Install PALETTE once more. It starts on the preset saved in step 15: its own data stayed through the stay on
    OpenPocket. STARTER keeps its data in the same flash range under another magic number, so a save on one of the two
    replaces the other's.
21. Go back to OpenPocket with `node tools/op.mjs flash --confirm-flash`.

Then listen. The twenty effects behave as the reference's manual describes. Whether each one sounds right is the work
of the next version, with the owner listening.

## 6. Reproduce

```
node tools/op.mjs all                                  # every product, the release pin, every test
node tools/op.mjs sim --app palette                    # the app on this computer; the page plays a guitar loop
cc -O2 -fno-vectorize -fno-slp-vectorize -DPL_REFERENCE -Ifirmware/apps/palette -Iengine -o build/palette_render \
   tests/palette_render.c firmware/apps/palette/engine.c engine/px_dsp.c firmware/px_math.c -lm
build/palette_render --runs 15 --wav build/palette-wav # section 2's levels and times; each effect as a WAV file
```

For the cycle count, disassemble the object in the build container and count:

```
node tools/op.mjs shell
/opt/jieli/toolchain/common/bin/objdump -d --mcpu=r3 build/apps/palette/obj/engine.o > build/engine.disasm.txt
python3 tools/cyclecount.py build/engine.disasm.txt
```
