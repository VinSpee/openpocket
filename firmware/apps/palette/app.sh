# PALETTE (palette.c): what firmware/build.sh needs to build it on the base firmware (docs/BASE-FIRMWARE.md).
APP_MAIN=palette.c         # the app's C file
APP_ENGINE=engine.c        # the engine's C file (compiled -O2, as engine/px_dsp.c is)
APP_DEFS=                  # compiler options for both
# the engine file's functions that run from RAM: everything a block calls (a name the compiler inlined is skipped)
APP_RAM_DSP="pl_run pl_swap pl_adopt fx_drive fx_sweeten fx_fuzz fx_howl fx_swell fx_doubler fx_vibrato fx_phaser fx_tremolo
  fx_pitch fx_cascade fx_reels fx_space fx_collage fx_reverse fx_filter fx_squash fx_cassette fx_broken fx_interf"
APP_RAM_FW="app_sound"     # palette.c's functions that run from RAM, beside the base's audio path
