/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — USB audio (v40.6, docs/USB-AUDIO.md): the pedal as a class-compliant USB sound card.
 *
 * The USB side is felucca-bsp's (bsp/chip/usb.c, FELUCCA_UAC and FELUCCA_UAC_OUT: USB Audio Class 1, both streams on
 * endpoint 4, isochronous, 16-bit stereo at 44.1 kHz; its descriptors, its rings and their pacing). This file is the
 * pedal's side:
 *   px_ua_block     each output block: the pedal's sound into the record ring, the computer's playback into the output
 *   px_ua_service   the USB AUDIO setting (px_midi.h px_ua_on) to the USB device, which re-enumerates on a change
 *   ed_ua_stat      the streams' state and counts, for the editor's USB_AUDIO (docs/EDITOR-PROTOCOL.md)
 * The clocks: the pedal's I2S clock is 44,117.6 Hz and not locked to USB's. To the computer the endpoint is
 * asynchronous: each 1 ms packet carries the frames the I2S clock made (44 or 45) and the ring's fill steers the rest by
 * one frame a block at most, so the computer follows the pedal's clock. From the computer the endpoint is adaptive:
 * its packets are resampled to the pedal's clock, the ring's fill steering the ratio.
 * The including file provides usb.c (with HALF_FRAMES = the frames of a block), a_out, a_mute, a_gv, a_ua and a_uat
 * (px_audio.h) and px_ua_on (px_midi.h); tests/test_usb_audio.c runs this file and usb.c on a PC.
 */
#pragma once

/* One output block (a_dec: the I2S interrupt on CPU0, where the block CPU1 made is whole; TIMER5's usb_poll, which
 * sends and takes the packets, nests in it as it does in felucca-bsp's render).
 * To the computer: a_out, the chain's output after the mutes and the change fade and before MASTER, through the
 * output's soft knee: what the output plays with MASTER at 100, so the recording's level does not follow the knob.
 * Mono, on both channels, 16 bits. The ring and its pacing are usb.c's (uac_render_start, uac_packet).
 * From the computer: usb.c's uac_play adds its stereo playback to the block, resampled to the pedal's clock, at
 * MASTER's gain, and clips the sum to the 24 bits the I2S takes. It is not in the recording. */
static void px_ua_block(int32_t *d, uint32_t n)
{
    uint32_t i;
    if (uac.go && ua_w == ua_r) uac.go = 0;     /* the ring ran dry (a flash write held the audio): primed again */
    uac_render_start();
    if (uac.feed) {
        const uint32_t w = ua_w;
        if (UA_N - (w - ua_r) < n) uac.overruns++;   /* (nobody reads: the block is dropped) */
        else {
            for (i = 0; i < n; i++) {
                float v = a_out[i], m = v < 0.0f ? -v : v;
                if (m > 0.8f) {                 /* (dsp_out's knee) */
                    const float t = (m - 0.8f) * 5.0f;
                    m = 0.8f + 0.2f * t / (1.0f + t);
                    v = v < 0.0f ? -m : m;
                }
                ua_ring[(w + i) & (UA_N - 1u)] = (uint32_t)(uint16_t)(int16_t)(v * 32767.0f) * 0x10001u;
            }
            RING_PUBLISH();
            ua_w = w + n;
        }
    }
    if (a_mute) uo.go = 0;                      /* (a flash write is coming: the output is zeros; primed again after it) */
    else if (uo.alt) uac_play(d, n, (uint32_t)(a_gv * 4096.0f));
}
/* the setting to the USB device (main loop): a change drops the pedal off the bus for a second, and it comes back with
 * or without the audio streams; the computer sees a new device. 150 ms first, so the editor's reply leaves */
static void px_ua_service(uint32_t now)
{
    static uint32_t t;
    static uint8_t seen;
    if (px_ua_on == usb_uac_on) { seen = 0; return; }
    if (!seen) { seen = 1; t = now; return; }
    if (now - t < 150u) return;
    seen = 0;
    a_ua = 0;                                   /* (the interrupt leaves the rings alone before the streams go) */
    usb_uac_switch(px_ua_on);
    a_uat = 0;
    a_ua = usb_uac_on;
    usb_retry_ms = now;                         /* off the bus for a whole second: the computer sees it leave */
}
/* USB_AUDIO's figures for the editor (px_editor.h, ED_UA_STATS of them after the state): 0 the state (bit 0: the
 * computer has the record stream open, bit 1: it takes our packets, bit 2: it has the playback stream open, bit 3: its
 * playback sounds); then, counted since the pedal started: the record stream's starts, packets, frames, the ring empty
 * (a frame repeated), the ring full (a block dropped), no packet ready, the computer stopped reading, packets a frame
 * longer, a frame shorter, the ring's lowest and highest fill at a block's start (since the stream started); the
 * playback stream's starts, packets, frames, the ring empty, the ring full; the last block's time in a_dec (ticks) */
static uint32_t ed_ua_stat(uint32_t k)
{
    switch (k) {
    case 0: return (uac.alt ? 1u : 0u) | (uac.flowing ? 2u : 0u) | (uo.alt ? 4u : 0u) | (uo.go ? 8u : 0u);
    case 1: return uac.starts;
    case 2: return uac.pkts;
    case 3: return uac.frames;
    case 4: return uac.underruns;
    case 5: return uac.overruns;
    case 6: return uac.missed;
    case 7: return uac.stalls;
    case 8: return uac.adj_up;
    case 9: return uac.adj_down;
    case 10: return uac.fill_lo;
    case 11: return uac.fill_hi;
    case 12: return uo.starts;
    case 13: return uo.pkts;
    case 14: return uo.frames;
    case 15: return uo.underruns;
    case 16: return uo.overruns;
    default: return a_uat;
    }
}
