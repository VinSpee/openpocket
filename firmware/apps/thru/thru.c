/* SPDX-License-Identifier: GPL-3.0-only
 * THRU — the smallest app on the base firmware (docs/BASE-FIRMWARE.md): the guitar straight to the output, the MASTER
 * knob, the load and the input level on the screen. No effects, no presets, no editor, one core.
 * It shows that an app other than OpenPocket builds on the base, and it is the file to copy to start one:
 *   node tools/op.mjs app thru   (OP_APP=thru sh firmware/build.sh)   ->   build/apps/thru/
 * It installs and is replaced as OpenPocket is: the same update entry over USB (px_usb.h), the same boot guard
 * (base/start.h), the same packages (tools/pxpkg.py).
 */
#include <stdint.h>
#define AUDIO_PROBE 0
#define DSP_CPU1 0                      /* one core: the sound is made in the I2S interrupt (base/audio_plain.h) */
#define OP_NAME "THRU"                  /* the name on the boot screen (base/boot_screens.h) */
#define OP_VERSION_NUM "01"             /* the package's number: PocketX_901, BlackBox_901 (firmware/build.sh, px_usb.h) */
#define OP_VERSION "v" OP_VERSION_NUM
#include "base/board.h"                 /* the chip, the pins, the screen and its text */
#include "base/controls.h"              /* the controls' pins, the ADC, the MASTER knob */
#include "base/diag.h"                  /* the crash screen, the boot stages kept across a reset */
#include "base/audio.h"                 /* the I2S, a_input, dsp_out, dsp_load, a_output */

static void app_sound(const float *in, float *out)     /* the app's sound: the guitar as it is */
{
    for (uint32_t i = 0; i < A_FR; i++) out[i] = in[i];
}
#include "base/audio_plain.h"           /* a_block, a_send, a_collect: app_sound in the interrupt */

static void ed_service(uint32_t now) { (void)now; }    /* no editor: every SysEx frame is the installer's */
#include "px_usb.h"                     /* USB-MIDI and the update entry (the web installer) */
#include "base/start.h"                 /* fm1_cstart: it calls app_main, below */

static void app_main(void)
{
    uint32_t boots_ok = 0, adc_tick = 0, t_show = 0, vol = 80, vol_set = ~0u;
    int32_t master_raw = -100;
    lcd_init();
#include "base/boot_screens.h"          /* (statements) the boot screen, the last start's report; declares safe */
    op_stage(ST_INPUTS, "INPUTS...");
    input_init();
    adc_pins_analog();
    input_baseline();
    a_hold = 1;                          /* app_sound held off until the start is through (in safe mode: for good) */
    px_flash_boot();                     /* the flash's size; a staging area an update left */
    px_usb_start();                      /* USB-MIDI and the update entry: the installer's way back to OpenPocket */
    audio_io_start();                    /* guitar -> app_sound -> output, the amp on */
    op_stage(ST_UI, "SCREEN...");
    lcd_rect(0, 0, 240, 240, UI_BG);
    ft_box(0, 60, 240, OP_NAME, &FT_MD, UI_ACC, UI_BG, 1);
    ft_box(0, 92, 240, "GUITAR TO OUTPUT, NO EFFECTS", &FT_SM, UI_TXT, UI_BG, 1);
    ft_box(0, 214, 240, safe ? OP_VERSION " SAFE MODE" : OP_VERSION, &FT_SM, UI_DIM, UI_BG, 1);
    op_stage(ST_RUN, 0);
    for (;;) {
        uint32_t now = ms_now();
        fm1_wdt_feed();
        px_usb_service(now);             /* the installer: handshake, update, UBOOT soft key */
        if (!boots_ok && now > 6000u) { boots_ok = 1; guard.pending = 0; guard.failed = 0; op_diag.stage = ST_OK; }
        if (a_hold && !safe && now > 1500u) a_hold = 0;
        op_diag.loops++;
        if (now - adc_tick >= 10u) {     /* the MASTER knob, every 10 ms */
            adc_tick = now;
            int32_t mv = adc_read2(ADC_CH_MASTER);
            if (mv >= 0) master_follow(&master_raw, mv, &vol);
        }
        if (vol != vol_set) { vol_set = vol; audio_volume(vol); }
        if (now - t_show >= 500u) {      /* the figures, twice a second; the same ones for the next start's report */
            char l[32];
            t_show = now;
            op_diag.blocks = a_blocks; op_diag.load = a_load; op_diag.peak = a_peak; op_diag.nans = a_nan_count;
            l[0] = 0; b_str(l, "VOL "); b_num(l, vol); b_str(l, "   DSP "); b_num(l, a_load); b_str(l, "%");
            ft_box(0, 140, 240, l, &FT_SM, UI_TXT, UI_BG, 1);
            const int db = a_level_db(0);
            l[0] = 0; b_str(l, "INPUT ");
            if (db <= -199) b_str(l, "SILENT"); else { b_str(l, "-"); b_num(l, (uint32_t)(db < 0 ? -db : 0)); b_str(l, " DB"); }
            ft_box(0, 164, 240, l, &FT_SM, UI_DIM, UI_BG, 1);
        }
    }
}
