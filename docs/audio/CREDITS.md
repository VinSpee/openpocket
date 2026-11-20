# Credits for the audio samples

The guitar in every file of this folder is a recording from the Guitar-TECHS dataset. The recordings are not under this
repository's GPL-3.0. They are licensed under the
[Creative Commons Attribution 4.0 International licence](https://creativecommons.org/licenses/by/4.0/) (CC BY 4.0), as
the dataset's Zenodo record states: <https://zenodo.org/records/14963133>. The samples are adaptations of those
recordings, and this file is the attribution that the licence asks for.

The dataset's authors ask users to cite their paper: Guitar-TECHS, ICASSP 2025, doi:10.1109/ICASSP49660.2025.10887996.

| Title | Authors | Source | Licence | Used in |
| --- | --- | --- | --- | --- |
| "Low riff" (chugging rhythm) | Hegel Emmanuel Pedroza Villalobos, Termeh Taheri, Wallace Abreu, Ryan Corey and Iran R. Roman | https://zenodo.org/records/14963133, P3_music.zip: audio/directinput/directinput_02.wav, 14.66 s to 20.054 s | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) | `amp-only-chug.m4a`, `palette-02-fuzz.m4a`, `palette-10-echo-into-fuzz.m4a` |
| "Clean riff" (clean riff) | Hegel Emmanuel Pedroza Villalobos, Termeh Taheri, Wallace Abreu, Ryan Corey and Iran R. Roman | https://zenodo.org/records/14963133, P3_music.zip: audio/directinput/directinput_08.wav, 15.57 s to 19.987 s | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) | `amp-only-riff.m4a`, `palette-01-drive.m4a`, `palette-08-full-chain.m4a` |
| "Arpeggios" (arpeggios) | Hegel Emmanuel Pedroza Villalobos, Termeh Taheri, Wallace Abreu, Ryan Corey and Iran R. Roman | https://zenodo.org/records/14963133, P3_music.zip: audio/directinput/directinput_09.wav, 18.078 s to 23.546 s | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) | `amp-only-arpeggio.m4a`, `palette-04-tremolo.m4a`, `palette-06-reverse.m4a` |
| "Chords" (chords) | Hegel Emmanuel Pedroza Villalobos, Termeh Taheri, Wallace Abreu, Ryan Corey and Iran R. Roman | https://zenodo.org/records/14963133, P3_music.zip: audio/directinput/directinput_11.wav, 2.377 s to 7.487 s | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) | `amp-only-chords.m4a`, `palette-03-phaser.m4a`, `palette-07-cassette.m4a` |
| "Lead phrase" (lead phrase) | Hegel Emmanuel Pedroza Villalobos, Termeh Taheri, Wallace Abreu, Ryan Corey and Iran R. Roman | https://zenodo.org/records/14963133, P3_music.zip: audio/directinput/directinput_12.wav, 3.275 s to 13.566 s | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) | `amp-only-lead.m4a`, `palette-05-space.m4a`, `palette-09-big-space.m4a` |

Changes made to each recording: cut to a loop, crossfaded, resampled to 44.1 kHz, converted to mono 16-bit, and
normalised (`assets/loops/prepare.py`, `assets/loops/ATTRIBUTION.md`); then played through PALETTE's engine and an
amplifier simulation, matched in loudness, faded, and encoded as AAC ([README.md](README.md)).

The amplifier simulation is OpenPocket's own engine (`engine/px_dsp.c`), GPL-3.0-only, part of this repository.
