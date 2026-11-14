# PALETTE: measured levels, costs, and the checks left for a pedal

This report belongs to [PALETTE.md](PALETTE.md). Nothing in it was measured on a pedal. Every figure comes from the
engine's own code on a computer or from the pedal compiler's output. Section 5 lists what a pedal must still show.

## 1. Result

- All twenty effects stay within 1.5 dB of the guitar's loudness at AMOUNT 0, 50, and 100, except where the effect is a
  change of level by its nature (section 2).
- The heaviest chain of four effects is estimated at 27 % to 44 % of one core's block at 360 MHz, and at 30 % to 49 %
  at 320 MHz. The pedal has two cores, and PALETTE uses one (section 3).
- The engine uses 126,288 bytes of the 311,296 bytes the product declares for it. Its per-sample code is 18,360 bytes
  in a RAM code window of 65,536 bytes (section 4).
- OpenPocket's image and packages are still the release's bytes. `node tools/op.mjs all` ends with the release pin.

## 2. Each effect on the standard signal

The signal is the base firmware's test guitar: a note and its fifth, sawtooth, plucked every 0.4 s at about -12 dBFS
and decaying, the notes walking two octaves. It lasts twelve seconds, and the first two are left out of the levels.
One effect runs alone, with the shared knobs at their centres. PITCH is set a fifth up, and FILTER at AMOUNT 50 is
measured at 30, because their centres do nothing.

Loudness is the RMS of both outputs against the guitar's, in dB. Peak is the highest sample of the three runs, as a
part of full scale. The guitar's own peak is 0.197. Section 3 explains the four columns of cost.

| Module | Effect | Loudness at AMOUNT 0 | at 50 | at 100 | Peak | Cycles a sample | % of a block (A) | Host, ns a sample | % of a block (B) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Character | Drive | 0.0 | 0.0 | +0.1 | 0.395 | 455 | 5.6 | 17.02 | 6.7 |
|  | Sweeten | 0.0 | -0.2 | -0.3 | 0.214 | 126 | 1.5 | 3.89 | 1.5 |
|  | Fuzz | -1.5 | +0.1 | +0.8 | 0.196 | 509 | 6.2 | 16.44 | 6.5 |
|  | Howl | -1.3 | +0.1 | +0.5 | 0.324 | 579 | 7.1 | 20.92 | 8.2 |
|  | Swell | -0.3 | -3.8 | -6.4 | 0.197 | 84 | 1.0 | 3.17 | 1.2 |
| Movement | Doubler | 0.0 | -0.6 | 0.0 | 0.224 | 111 | 1.4 | 3.01 | 1.2 |
|  | Vibrato | 0.0 | 0.0 | 0.0 | 0.197 | 180 | 2.2 | 5.07 | 2.0 |
|  | Phaser | +0.1 | -0.7 | -0.1 | 0.351 | 355 | 4.3 | 32.54 | 12.8 |
|  | Tremolo | 0.0 | -0.3 | +0.3 | 0.290 | 104 | 1.3 | 5.06 | 2.0 |
|  | Pitch | 0.0 | +0.5 | 0.0 | 0.295 | 259 | 3.2 | 6.91 | 2.7 |
| Diffusion | Cascade | +0.9 | +1.2 | +3.0 | 0.320 | 120 | 1.5 | 4.57 | 1.8 |
|  | Reels | +0.9 | +1.2 | +3.1 | 0.321 | 124 | 1.5 | 4.16 | 1.6 |
|  | Space | 0.0 | +0.1 | -0.1 | 0.255 | 715 | 8.8 | 11.02 | 4.3 |
|  | Collage | +0.9 | +1.2 | +3.2 | 0.332 | 90 | 1.1 | 2.94 | 1.2 |
|  | Reverse | 0.0 | +0.5 | -0.1 | 0.300 | 113 | 1.4 | 3.09 | 1.2 |
| Texture | Filter | -2.5 | -0.2 | -12.7 | 0.307 | 90 | 1.1 | 7.57 | 3.0 |
|  | Squash | -0.7 | -0.5 | +0.8 | 0.203 | 66 | 0.8 | 3.93 | 1.5 |
|  | Cassette | -0.1 | 0.0 | 0.0 | 0.303 | 329 | 4.0 | 15.49 | 6.1 |
|  | Broken | 0.0 | 0.0 | 0.0 | 0.245 | 169 | 2.1 | 5.03 | 2.0 |
|  | Interference | 0.0 | -0.7 | -0.1 | 0.480 | 161 | 2.0 | 4.14 | 1.6 |
| The chain | Copy, MIX, level, fades, ceiling, with four modules on | | | | | 202 | 2.5 | 3.41 | 1.3 |

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
branch, which the script does not count. Space is its tick loop plus eight turns of its line loop. The chain's row is
the input's copy, the offset each running module's input gets, and the last loop. One per cent of a block is 81.6
cycles a sample at 360 MHz. The script leaves out three costs the handbook documents, so it may read low. The
feasibility report checked the method against a loop whose load on a pedal is known, and it agreed within a factor of
two.

**Estimate B, host time converted.** `tests/palette_render.c` times each effect on the signal of section 2 on an
Apple M3 Pro, built with clang and `-O2 -fno-vectorize -fno-slp-vectorize`. The time is the process's CPU time, the
least of 15 runs, less the same engine with every module off (the chain's row). In the same run it times three blocks
of OpenPocket's engine whose load on a pedal an earlier task read with `node tools/op.mjs dsp` on v40:

| OpenPocket's block | Host, ns a sample | On a pedal, % of a block | % a ns |
| --- | --- | --- | --- |
| Reverb, room | 19.28 | 9 | 0.47 |
| Delay, tape | 9.45 | 3 | 0.32 |
| Modulation, chorus | 10.23 | 4 | 0.39 |

Column B uses the mean, 0.39 % a ns. The range is 0.32 to 0.47. This factor is the weakest part of the estimate:

- The computer was busy during the run (a load average of 50 on 12 cores, with the container build running). That is
  why the time is the least of the runs and why the reference blocks are timed in the same run. Two earlier runs of
  the same tool on earlier states of the engine gave 0.38 and 0.45, and the feasibility report measured 0.45. The run
  that gave 0.45 put the heaviest chain's larger estimate at 44 %, which is the upper figure of section 1.
- The pedal's loads are whole per cents, so the factor of a 3 % block carries a rounding error of up to a sixth.

**The two estimates** agree within a factor of 1.5 for 14 of the 20 effects. They disagree by a factor of 2 or more
on Phaser, Space, and Filter, as they did in the feasibility report, and the cause is still not established. Squash,
Tremolo, and Cassette differ by 1.5 to 1.9. The planning figure for an effect is the larger of the two.

**Whole chains.** The time includes the chain's own code.

| Chain | A: % | B: % (range) |
| --- | --- | --- |
| Howl, Phaser, Space, Cassette: the heaviest effect of each module, timed as a chain | 26.7 | 33.4 (27.2 to 39.9) |
| The same, in the order Space, Howl, Cassette, Phaser | 26.7 | 34.4 (28.0 to 41.1) |
| Howl, Pitch, Space, Cassette | 25.5 | 22.0 (17.9 to 26.3) |
| Drive, Doubler, Collage, Broken | 12.6 | 11.4 (9.3 to 13.6) |
| The heaviest chain, taking the larger estimate for each effect | 38.4 | |

**Reading.** The heaviest chain costs 27 % to 38 % of one core's block at 360 MHz in this run, and up to 44 % with the
largest factor of the three runs. Under m-bootloader 1.2 the clock is 320 MHz, and the same chain costs 30 % to 49 %.
The base bypasses a sound that stays above 85 % for eight blocks. The estimate leaves a factor of 1.7 before that
limit in the worst reading, and of 3 in the best. The feasibility report estimated 25 % to 37 % for its sketch. Of
the difference, 16 cycles a sample a module are the offset each module's input now gets against denormal numbers.
The rest is within the spread of the conversion.

Four things are not in these figures:

- An effect change clears the module's memory in the audio interrupt, once. Diffusion's is 90 KB. The estimate is
  0.3 ms, a tenth of a block, in the one block where that module does not run.
- Coming back from bypass clears the memory of every module that is on, in one block.
- An effect that starts runs unheard for two blocks first, three for Cassette and nine for Pitch, so a change of
  effect takes 17 ms from the press to the full sound, and 38 ms into Pitch.
- The engine always runs two channels. The input is mono, so a module that comes before the first stereo effect could
  run one channel and cost half. That saving is not taken.

A pedal must confirm all of this (section 5).

One limit is known and left for the voicing: a knob turned quickly, five at a time, can tick in an effect where it
sets a gain or a filter's frequency. Delay times, volumes, Vibrato's depth, and Phaser's stages glide. A jump of more
than five is blended.

## 4. Memory against the declared regions

From the build's own report (`node tools/op.mjs app palette`):

```
ramcode: build/apps/palette/obj/engine.o: 22 section(s), 18360 bytes of code to RAM
pool tail : 0 bytes, 185008 left before the RAM code
hi RAM    : RAM code 22716, data 0, free 42808 of 65536 bytes (the RAM code window)
RAM       : 22912 of 65536 bytes (42624 free)
OK -> palette.bin: 75584 bytes
```

| Region (`firmware/apps/palette/memory.ld`) | Size | Used | Free | Content |
| --- | --- | --- | --- | --- |
| `POOL` | 311,296 | 126,288 | 185,008 | The engine: Diffusion's line 90,112, Movement's and Texture's 16,384 each, the rest state |
| `RAMTEXT` | 65,536 | 22,716 | 42,808 | The engine's per-sample code 18,360, the base's audio path |
| `RAM` | 65,536 | 22,912 | 42,624 | `.data` and `.bss`: the store's copy in RAM, the base |
| Flash image | 581,564 | 75,584 | | OpenPocket's image is 363,852 |

The 185 KB left in `POOL` is kept for the looper of a later version: about 4 s at 16 bits and 22.05 kHz.

A sound is 28 bytes and a preset 40. The store holds 80 presets in 3,212 bytes of the 19,184 the base allows.

No effect's loop refers to data in the flash: the object has no relocation in any of the twenty effect functions.
`pl_run` refers to it in three places: the table of effect functions, read four times a block, and the unit lengths
of Space, read when that effect starts.

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
    estimate is 27 % to 44 %, or 30 % to 49 % under m-bootloader 1.2. Send Control Change 78 at 127 and play hard: the
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
mkdir -p build/palette-wav
cc -O2 -fno-vectorize -fno-slp-vectorize -DPL_REFERENCE -Ifirmware/apps/palette -Iengine -o build/palette_render \
   tests/palette_render.c firmware/apps/palette/engine.c engine/px_dsp.c firmware/px_math.c -lm    # (clang)
build/palette_render --runs 15 --wav build/palette-wav # section 2's levels and times; each effect as a WAV file
```

For the cycle count, disassemble the object in the build container and count:

```
node tools/op.mjs shell
/opt/jieli/toolchain/common/bin/objdump -d --mcpu=r3 build/apps/palette/obj/engine.o > build/engine.disasm.txt
python3 tools/cyclecount.py build/engine.disasm.txt
```
