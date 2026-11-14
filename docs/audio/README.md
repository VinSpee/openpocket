# PALETTE: audio samples

These files are not recordings of a pedal. Each one is a Creative Commons guitar recording played through PALETTE's
own code on a computer, and then through an amplifier simulation. The engine is the code the pedal runs
(`firmware/apps/palette/engine.c`). The pedal's converters and its analog stage are not modelled, and nobody has
compared these files with a pedal. [../PALETTE.md](../PALETTE.md) describes the instrument.

Every file is in stereo, AAC at 96 kbit/s in an `.m4a` file, and 11 to 14 seconds long. All fifteen files are set to
the same loudness (-27.5 dBFS, see below), so each sample and its reference are matched. The level is low because a
clean guitar has high peaks: turn the volume up.

## The samples

Each sample has a reference: the same recording through the same amplifier with every PALETTE module off.

| Sample | PALETTE's sound, in the chain's order | Recording | Reference | Length |
| --- | --- | --- | --- | --- |
| [`palette-01-drive.m4a`](palette-01-drive.m4a) | Drive (AMOUNT 60, TILT 50) | "Clean riff" | [`amp-only-riff.m4a`](amp-only-riff.m4a) | 11.4 s |
| [`palette-02-fuzz.m4a`](palette-02-fuzz.m4a) | Fuzz (AMOUNT 55, TILT 55) | "Low riff" | [`amp-only-chug.m4a`](amp-only-chug.m4a) | 13.3 s |
| [`palette-03-phaser.m4a`](palette-03-phaser.m4a) | Phaser (AMOUNT 70, RATE 35, DRIFT 15) | "Chords" | [`amp-only-chords.m4a`](amp-only-chords.m4a) | 12.8 s |
| [`palette-04-tremolo.m4a`](palette-04-tremolo.m4a) | Tremolo (AMOUNT 75, RATE 65) | "Arpeggios" | [`amp-only-arpeggio.m4a`](amp-only-arpeggio.m4a) | 13.5 s |
| [`palette-05-space.m4a`](palette-05-space.m4a) | Space (AMOUNT 50, TIME 80, DRIFT 30) | "Lead phrase" | [`amp-only-lead.m4a`](amp-only-lead.m4a) | 12.8 s |
| [`palette-06-reverse.m4a`](palette-06-reverse.m4a) | Reverse (AMOUNT 65, TIME 50) | "Arpeggios" | [`amp-only-arpeggio.m4a`](amp-only-arpeggio.m4a) | 13.5 s |
| [`palette-07-cassette.m4a`](palette-07-cassette.m4a) | Cassette (AMOUNT 65) | "Chords" | [`amp-only-chords.m4a`](amp-only-chords.m4a) | 12.8 s |
| [`palette-08-full-chain.m4a`](palette-08-full-chain.m4a) | factory preset 20, FULL CHAIN: Drive (AMOUNT 40, TILT 50), then Phaser (AMOUNT 45, RATE 50, DRIFT 20), then Reels (AMOUNT 40, TIME 60, DRIFT 30), then Cassette (AMOUNT 40), 120 beats a minute | "Clean riff" | [`amp-only-riff.m4a`](amp-only-riff.m4a) | 11.4 s |
| [`palette-09-big-space.m4a`](palette-09-big-space.m4a) | factory preset 5, BIG SPACE: Sweeten (AMOUNT 35, TILT 55), then Vibrato (AMOUNT 20, RATE 35, DRIFT 30), then Space (AMOUNT 55, TIME 85, DRIFT 30) | "Lead phrase" | [`amp-only-lead.m4a`](amp-only-lead.m4a) | 12.8 s |
| [`palette-10-echo-into-fuzz.m4a`](palette-10-echo-into-fuzz.m4a) | factory preset 18, ECHO>FUZZ: Cascade (AMOUNT 40, TIME 50, DRIFT 20), then Fuzz (AMOUNT 45, TILT 50) | "Low riff" | [`amp-only-chug.m4a`](amp-only-chug.m4a) | 13.3 s |

A knob that is not named is at its default: TILT, RATE, TIME, SENSITIVITY, MIX, OUTPUT LEVEL, and each EFFECT VOL at
50 (MIX at 100), DRIFT at 0. Samples 8 to 10 are factory presets as they are. Sample 10 has another order: Diffusion
comes before Character, so the echoes are fuzzed.

## How they were made

1. **The guitar.** Five dry direct-input recordings of an electric guitar from the Guitar-TECHS dataset, CC BY 4.0,
   which this repository already bundles in `assets/loops/`. [CREDITS.md](CREDITS.md) names each one. A recording is
   played twice in a row, or once for the longer lead phrase, and then 2.5 seconds of silence follow so that a tail
   can ring out.
2. **PALETTE.** The simulator (`node tools/op.mjs sim --app palette`) runs the app's own C code. The recording goes in
   with `POST /api/loop/NAME` at the level a guitar has at the pedal's converter (0.15 of full scale after the base's
   input stage), and the two outputs come back with `POST /api/audio/MS`. MASTER is at 100. The sound is set with the
   app's own editor requests: an empty slot first, then the effect and the knobs of the table, or a factory preset.
3. **The amplifier, after PALETTE,** as with a pedal in front of an amp. It is OpenPocket's own engine
   (`engine/px_dsp.c`, GPL-3.0-only, this repository) running on the computer with only two blocks on: the amp TWIN
   (GAIN 40, BASS 50, MID 50, TREBLE 55, PRESENCE 50) and the cabinet 1x12 US OPEN (BODY 50, BRIGHT 50, MIC 50).
   Each side of PALETTE's stereo output has an amplifier of its own, so the stereo effects stay stereo. The engine's
   preset level is at its lowest, -15 dB, so its output limiter (which starts at 0.8 of full scale) is never
   reached: the highest peak before the level is matched is 0.56. At these levels the amp is clean: half the
   input gives half the output.
4. **Loudness.** Each file's loudness is its RMS over blocks of 400 ms, without the blocks more than 10 dB under the
   mean. Every file gets the gain that puts it at the same loudness, and then one common trim keeps the highest
   peak of all files at -1 dBFS. The result is -27.5 dBFS for each file. Nothing else is done to the sound: no
   limiter, no equaliser. A fade of 5 ms opens each file and one of 300 ms closes it.
5. **Encoding.** `afconvert -f m4af -d aac -b 96000` on macOS.

The drive and the fuzz are louder than the guitar on these recordings before the loudness is matched: by 5 dB and
9 dB of that RMS, with peaks 0 dB and 3 dB higher, because they sustain the notes. The engine's level trims were set on another
signal ([../PALETTE-REPORT.md](../PALETTE-REPORT.md), section 2), and matching every effect by ear is the next step.
