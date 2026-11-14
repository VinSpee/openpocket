# THRU (thru.c): what firmware/build.sh needs to build it on the base firmware (docs/BASE-FIRMWARE.md).
APP_MAIN=thru.c            # the app's C file
APP_ENGINE=                # the engine's C file, when the app has one of its own (compiled -O2, as engine/px_dsp.c is)
APP_DEFS=                  # compiler options for both
APP_RAM_DSP=               # the engine file's functions that run from RAM
APP_RAM_FW="app_sound"     # thru.c's functions that run from RAM, beside the base's audio path
