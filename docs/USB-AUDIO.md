# USB audio: the pedal as a sound card (v40.6)

With the **USB AUDIO** setting on, the pedal is a USB sound card as well as a USB-MIDI device. A computer records the
pedal's sound over the USB-C cable, and what the computer plays comes out of the pedal's output and headphones. The
pedal is class compliant (USB Audio Class 1): macOS, Windows, iOS and iPadOS use it without a driver.

The setting is **off by default**. With it off the pedal is on USB exactly what it was before: the same USB device,
byte for byte, with MIDI alone.

## Using it

**Switch it on**, either way:

- On the pedal: hold **FX** for 0.8 s to open the MIDI page, press PUSH three times to reach **USB AUDIO**, turn the
  knob to **ON**, press HOME.
- In the web editor: **USB audio**, **Turn on**.

The pedal saves the setting (the sound stops for about 0.3 s, as for SAVE), leaves USB for one second and comes back
as a device named **OpenPocket** with an audio input, an audio output and the MIDI port. The setting is kept through
power cycles and firmware updates. The web editor connects again by itself; the installer and firmware updates work the
same with the setting on or off.

**Record the pedal.** Choose the input **OpenPocket** in the recording program, at 44.1 kHz. Both channels carry the
same signal: the pedal's sound after the whole effect chain. The level is the one the output has with MASTER at 100, so
the MASTER knob does not change the recording, and the tuner's mute is in it.

**Play through the pedal.** Choose the output **OpenPocket**. The computer's sound, in stereo, is added to the pedal's
own at the output, so you hear a backing track and your guitar together in the headphones. MASTER sets the level of
both. The balance between them is set with the volume of the playing program, because the pedal offers the computer
no volume control of its own (some systems then grey out their volume slider for the device). What the computer plays
is not in the recording.

| | |
| --- | --- |
| Format | 2 channels, 16 bit, 44.1 kHz, both ways. Only this rate: set the project or the device to 44.1 kHz |
| Record | the pedal's sound on both channels; delay from the output to the cable about 2 to 6 ms |
| Playback | stereo; delay from the cable to the output about 6 ms |
| USB | full speed (USB 1.1 rates), one isochronous endpoint each way, beside the MIDI endpoints |

**What to expect.**

- A change of an effect's type, of the block order or of the NAM model, a preset change and a save silence the pedal
  for a few milliseconds, as before. The recording has the same silence.
- The pedal's clock is its own (44,117.6 Hz, as the stock firmware's). The computer follows it when it records, and the
  pedal follows the computer when it plays. Nothing drifts apart over time; a recording made by another sound card at
  the same time is not sample-locked to it, as with any two sound cards.
- USB audio adds up to 1 point of DSP load while the computer records and up to 2 while it also plays (see "Cost"
  below). Nothing is added while no program uses the device, or with the setting off.
- The Bluetooth test image has no USB audio (its memory is full).

**If the computer does not see the device**, switch USB AUDIO off on the pedal's MIDI page: the pedal is then the plain
MIDI device again and the editor and installer reach it as before. Tested so far: macOS 26 on Apple silicon. Windows,
iOS, iPadOS, Linux and macOS 13 to 15 are not tested yet; the descriptors follow what felucca-bsp found those systems
to need (see below).

## How it works

### What was there

- **The chip.** The pedal's JieLi AC791N has one full-speed USB device controller with endpoint 0, three general
  endpoints and a fourth with its own DMA registers that can run isochronous transfers
  (`firmware/bsp/chip/fm1_usb.h`). The stock firmware uses it as a sound card: USB Audio Class 1, playback adaptive and
  record asynchronous, 2 channels, 24 bit, 44.1 kHz only, beside a MIDI interface ([STOCK-FIRMWARE.md](STOCK-FIRMWARE.md)
  13.2 and 15.2).
- **The USB code.** OpenPocket's USB device is felucca-bsp's `chip/usb.c` (MPL-2.0), written for the FM-1, which has the
  same chip. It already held both audio streams, switched off at build time: a record stream (`FELUCCA_UAC`) and a
  playback stream (`FELUCCA_UAC_OUT`), both on endpoint 4, 16 bit stereo, with rings, pacing and the descriptors, and
  the notes of what macOS needs of them. OpenPocket had built it with MIDI alone.

So the feature is felucca-bsp's streams switched on, a setting, and the pedal's side of the audio.

### The smallest design

- **The streams** are felucca-bsp's as they are: endpoint 4 IN, isochronous asynchronous, for the record stream;
  endpoint 4 OUT, isochronous adaptive, for playback; 16 bit, 2 channels, 44.1 kHz, one packet a millisecond. No 48 kHz:
  it needs a resampler in the audio interrupt, and the stock firmware offers 44.1 kHz only too.
- **The interfaces** are in the order audio control, record stream, playback stream, MIDI
  (`FELUCCA_UAC_MIDI_LAST 1`). felucca-bsp found that macOS 13 to 15 show no audio device when the MIDI interface comes
  before the streams.
- **The setting** switches the descriptors at run time (`FELUCCA_UAC_SWITCH`, added to `usb.c` for OpenPocket, after
  its `usb_cdc_switch`): off, the pedal presents the descriptors of a build without audio, with `bcdDevice` 3.00; on,
  the audio device, with `bcdDevice` 3.D0, so a computer does not reuse what it learnt about the other one. A change
  takes the pedal off the bus for one second. The setting is one bit in the preset store's header
  (`firmware/px_store.h`, the header's second spare byte; every earlier firmware wrote 0 there, which is off). At boot
  the store is now read before the USB device starts, so the computer finds the right device from its first question.
- **The pedal's side** is `firmware/px_uaudio.h`. Once a block, in the I2S interrupt on CPU0, where the block the second
  core made is complete (`a_dec`), `px_ua_block` writes the block into the record ring and adds the playback to the
  output. USB's own work (taking and sending the packets) stays in the 2 kHz USB poll that MIDI already uses.
- **What is recorded** is the chain's output after the mutes and the change fade and before MASTER, through the
  output's soft knee, as 16 bits on both channels.
- **Playback** is added after MASTER has been applied to the pedal's sound, at MASTER's gain, and the sum is clipped
  to the output's 24 bits.

### The two clocks

The pedal's sample clock comes from its own crystal: 6 MHz / 136 = 44,117.647 Hz. USB's millisecond comes from the
computer. They are not locked, so neither side may assume 44.1 frames a millisecond.

- **Record (asynchronous endpoint):** the pedal is the clock. Each packet carries the frames the pedal's clock made in
  that millisecond: 44, and 45 every 8.5th packet. A ring of 512 frames lies between the audio interrupt, which writes
  128 frames every 2.9 ms, and the USB poll, which reads a packet a millisecond. What is left of the difference between
  the two crystals is steered by the ring's fill: when the fill at a block's start leaves the band of 80 to 128 frames,
  one packet is made a frame longer or shorter, at most once a block. The computer measures the rate it receives and
  follows it.
- **Playback (adaptive endpoint):** the computer is the clock. Its packets go into a ring of 1,024 frames, and the audio
  interrupt reads them resampled to the pedal's clock (linear interpolation). The ring's fill, aimed at 256 frames,
  steers the ratio by at most 0.6 %.

### With USB AUDIO off

- The USB descriptors are those of the firmware before, byte for byte (`tests/test_usb_audio.c` reads them through the
  real control-transfer code and compares them with a build without audio).
- Requests for the streams are refused as before, endpoint 4 is not set up, and nothing is ever queued on it.
- The audio interrupt tests one flag a block. The loads it measures are computed as before.
- The engine is not touched: `tests/equiv.sh` against the base gives identical output on its 852 runs.

### Cost

Measured on a BlackBox with the release image. The first figure is the time `px_ua_block` takes in the audio interrupt
on CPU0, as a share of an audio block (2.9 ms). The second is the change of the load the pedal reports (the busier
core's, in points) against the base firmware, over the 11 worst-case chains of the DSP work (a NAM amp, all 12 blocks
on, every knob at 100) and over the 23 factory presets with an amp. It also holds the USB poll's extra work.

| | In the audio interrupt | Load, 11 worst-case chains | Load, factory presets |
| --- | --- | --- | --- |
| Setting off | one flag tested | 0.0 to +0.6 (mean +0.2), the spread between two runs | -1 to +1 (mean 0.0) |
| On, no program using the device | 0.01 % | not measured apart | not measured apart |
| The computer records | 0.38 % | -0.5 to +1.0 (mean +0.6) | 0 to +2 (mean +0.4) |
| The computer records and plays | 1.13 % (playing alone: 0.75 %) | -0.4 to +2.0 (mean +1.3) | 0 to +2 (mean +1.2) |

No chain engaged the safety bypass (above 93 % for 8 blocks) and no block was late, also with a synthetic guitar at
the input, which costs more than a quiet one. The heaviest chain, W4, went from 82.4 % to 83.4 % with both streams
(83.8 % with the synthetic guitar, its highest single block 92 %).

With a NAM amp the two cores share the network so that both are equally busy; the time USB audio takes on CPU0 is
part of what the sharing balances.

Memory: 6.7 KB of RAM (the record ring 2 KB, the playback ring 4 KB, two packet buffers and the counters), 0.4 KB of RAM
code.

### Limits, and what is not done

- 16 bit, 44.1 kHz only. The stock firmware sends 24 bit; felucca-bsp's streams are 16 bit and were kept as they are.
- The recording is mono on both channels (the engine is mono). A dry guitar on the second channel for reamping in the
  computer, and playing the computer's sound into the effect chain, are not done.
- No volume or mute control is offered to the computer.
- A full change, a preset change and a save leave a short silence in the recording, and the record ring is primed
  again after it (about 2 ms of extra silence).
- A recording program that stops reading without closing the stream makes the pedal drop blocks for 20 ms before it
  stops feeding the ring. Nothing is heard: nobody is reading.

## Tests

- `tests/test_usb_audio.c` (in `tests/run_tests.sh`): `usb.c` and `px_uaudio.h` on a PC against a model of the USB
  controller and of a computer. It checks the descriptors and the requests with the setting off and on, the switch,
  the recording's level and soft knee, the playback's level, clipping and mute, and 10 minutes of each stream with the
  pedal's clock 300 ppm fast and slow against the computer's: every frame arrives once and in order.
- `tests/test_store.c`: the setting survives a power cycle and a preset save; an older store reads as off.
- `tests/test_editor.mjs`, `tests/test_editor_page.mjs`: `USB_AUDIO` on the simulated pedal, the editor's button.
- `tests/uisim.c`: the MIDI page's fourth row.
- On a pedal: the editor's `USB_AUDIO` command gives the streams' counts ([EDITOR-PROTOCOL.md](EDITOR-PROTOCOL.md));
  the record ring's "empty" and "no packet ready" counts must not rise while a preset simply plays.
