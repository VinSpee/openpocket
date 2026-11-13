/* SPDX-License-Identifier: GPL-3.0-only
 * The base firmware (docs/BASE-FIRMWARE.md): the plain way to make the sound, for an app without a schedule of its
 * own. Every block is made in the I2S interrupt, on one core. (OpenPocket has its own, px_audio.h: two cores.)
 *
 * The app's file provides, before this one:
 *   app_sound(in, out)   A_FR frames of the guitar (float, mono) -> A_FR frames of the app's sound; with A_CH 2
 *                        (base/audio.h) out takes the left block, then the right block
 * It is not called while a_hold is set (the start, safe mode) or after the load stayed above 85 % for 8 blocks
 * (a_bypass): the guitar then goes straight to the output, on both sides.
 */
#pragma once

static void a_block(const uint8_t *data, int len)      /* a guitar block: its sound made */
{
    a_input(data, len);
    const uint32_t t0 = fm1_ticks();
    if (!a_bypass && !a_hold) app_sound(a_in, a_out);
    else for (uint32_t i = 0; i < A_OUT_N; i++) a_out[i] = a_in[i % A_FR];
    dsp_out();
    dsp_load(t0, 85u);
}
static void a_send(int32_t *d, uint32_t n) { a_output(d, n); }   /* an output block filled with that sound */
static void a_collect(void) { }                        /* (nothing is in progress outside the interrupt) */
