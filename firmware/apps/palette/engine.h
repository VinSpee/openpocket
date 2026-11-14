/* SPDX-License-Identifier: GPL-3.0-only
 * PALETTE's engine (firmware/apps/palette, docs/PALETTE.md): four modules in series, each running one of its five
 * effects, in any order, two channels throughout. Clean room: every algorithm is a textbook structure written from the
 * public description of how the reference instrument behaves; no code, table or constant comes from it.
 * Single-precision float, 128 frames a block, no C library but the firmware's math (firmware/px_math.c) and memset.
 *
 *   pl_coefs   a sound -> the numbers a block runs on. Slow (powf, sinf) and pure: the main loop
 *   pl_commit  those numbers handed to the engine: with the audio interrupt held, so a block sees a whole change
 *   pl_run     one block, in the audio interrupt: the mono guitar in, a left block then a right block out
 * A change is never heard as a step: pl_run blends a module to its input, clears it, sets the other effect, lets it
 * run unheard until its memory holds sound, and blends it back; it fades the chain around a new order, and crossfades
 * to the guitar for bypass. A knob that moves a little is taken at the next block; one that jumps under a running
 * effect (pl_coef_t.hard: another preset) is taken with the module blended to its input, its memory kept.
 */
#pragma once
#include <stdint.h>

#define PL_FR 128u                              /* frames a block: the base firmware's A_FR */
#define PL_SR 44100.0f
enum { PL_CHAR, PL_MOVE, PL_DIFF, PL_TEXT, PL_NMOD };
#define PL_NFX 5u                               /* effects a module */
#define PL_OFF 5u                               /* in place of an effect: the module passes its input */
enum { PL_DRIVE, PL_SWEETEN, PL_FUZZ, PL_HOWL, PL_SWELL };
enum { PL_DOUBLER, PL_VIBRATO, PL_PHASER, PL_TREMOLO, PL_PITCH };
enum { PL_CASCADE, PL_REELS, PL_SPACE, PL_COLLAGE, PL_REVERSE };
enum { PL_FILTER, PL_SQUASH, PL_CASSETTE, PL_BROKEN, PL_INTERF };
enum { PL_TILT_STYLE, PL_LP_STYLE, PL_HP_STYLE, PL_NSTYLE };

/* the sixteen knob functions, each 0..100, in the order of the reference's Control Changes 64..79 */
enum { PL_K_TILT, PL_K_AMT_CHAR, PL_K_RATE, PL_K_AMT_MOVE, PL_K_TIME, PL_K_AMT_DIFF, PL_K_MIX, PL_K_AMT_TEXT,
       PL_K_SENS, PL_K_VOL_CHAR, PL_K_DRIFT_MOVE, PL_K_VOL_MOVE, PL_K_DRIFT_DIFF, PL_K_VOL_DIFF, PL_K_LEVEL, PL_K_VOL_TEXT,
       PL_NKNOB };
#define PL_K_AMT(m) (1u + 2u * (m))             /* module m's AMOUNT */
#define PL_K_VOL(m) (9u + 2u * (m))             /* module m's EFFECT VOL */
#define PL_TEMPO_MIN 30u
#define PL_TEMPO_MAX 300u

typedef struct {                                /* a sound: what a preset keeps, 28 bytes */
    uint8_t knob[PL_NKNOB];
    uint8_t fx[PL_NMOD];                        /* each module's effect, 0..4 */
    uint8_t order[PL_NMOD];                     /* the serial order: a permutation of the modules */
    uint8_t on;                                 /* bit m: module m is on (off, it keeps its effect) */
    uint8_t style;                              /* the Filter effect's: PL_TILT_STYLE .. */
    uint16_t tempo;                             /* 0: free time; else beats a minute, PL_TEMPO_MIN..PL_TEMPO_MAX */
} pl_sound_t;

#define PL_MV_N 8192                            /* Movement: 16-bit, full rate; one line of 186 ms or two of 93 ms */
#define PL_DF_N 45056                           /* Diffusion: 16-bit, half rate (22.05 kHz), 2.04 s; the reverb is laid out in it */
#define PL_TX_N 8192                            /* Texture: 16-bit, full rate, two lines of 93 ms */

/* ---- the numbers a block runs on, a module each (pl_coefs) ---- */
typedef struct {
    float g, g2, khp, kt, gl, gh, bias, bk, kb, lvl;        /* drive, fuzz, howl */
    float a1, a2, a3, sq;                                   /* howl's filter */
    float katt, krel, kc, make, sat;                        /* sweeten's compressor */
    float thr, ratio, kup, kdn;                             /* swell */
} pl_cc_t;
typedef struct {
    float inc, amt, dfk, off, lvl;
    float dlt, drt;                                         /* doubler: where the two taps go, in samples */
    float depth;                                            /* vibrato */
    float a0, a1, fb, w6[6];                                /* phaser: the sweep's ends, the taps' weights */
    float hard;                                             /* tremolo */
    float pinc, W, rest; int32_t hold;                      /* pitch */
} pl_mc_t;
typedef struct {
    float wet, dryg, dfk, khp, ks, inc1, inc2, off;
    float dt, fb, klp, wow, flut, nz, drop;                 /* cascade, reels */
    int32_t n[12]; float g, kd, dg, md, sinc;               /* space: each unit's length, in ticks */
    float len, spdt;                                        /* collage */
    float pinc, step;                                       /* reverse */
} pl_dc_t;
typedef struct {
    float katt, krel, amt;
    float a1, a2, a3, m0, m1, m2;                           /* filter */
    float kc, make, drv, lvl;                               /* squash, cassette */
    float base, wow, flut, inc1, inc2, klp, khp, nz, drop;  /* cassette */
    float rate, am, fm;                                     /* broken */
    float cinc, lev, ilev, rm, rinc;                        /* interference */
} pl_tc_t;
typedef struct {
    uint8_t fx[PL_NMOD];                        /* what each module is to run: its effect, or PL_OFF */
    uint8_t order[PL_NMOD];
    uint8_t bypass, hard, pad[2];               /* hard: the knobs jumped (pl_coefs leaves it 0; the caller sets it) */
    float vol[PL_NMOD], wet, dry, level;        /* each module's volume; MIX's two gains; OUTPUT LEVEL */
    pl_cc_t ch; pl_mc_t mv; pl_dc_t df; pl_tc_t tx;
} pl_coef_t;

/* ---- what the effects remember, a module each ---- */
typedef struct { float v, tgt; uint32_t rnd; int32_t cnt; } pl_drift_t;   /* a slow random value */
typedef struct { float s[6]; } pl_ap3_t;                    /* three first-order all-pass sections */
typedef struct { pl_ap3_t ua, ub, da, db; float podd; } pl_os2_t;        /* twice the rate and back, half-band */
typedef struct {
    pl_os2_t os[2];
    float hp[2], dc[2], lo[2], lo2[2], be[2], ic1[2], ic2[2];
    float env, slow, gate; int32_t arm;
} pl_cs_t;
typedef struct {
    int32_t w, hc;
    float ph, base, dl, dr, r0, r1, ap[2][12], last[2], w6[6], fb, pph, held[2], rest;
    pl_drift_t dft[2];
} pl_ms_t;
typedef struct {
    int32_t w, p, n[12], b[12], k, wp;                      /* space: each unit's length now, its place in the ring */
    float yl, yr, d, lp, hp, ph1, ph2, st[8], sph, rp, spd, rd[2], rph[2];
    pl_drift_t dft[2];
} pl_ds_t;
typedef struct {
    int32_t w, evt, sk, stut, sp, sl;
    float ic1[2], ic2[2], env, ph1, ph2, l1[2], l2[2], h[2], d, slope, cph, cheld[2], rph;
    pl_drift_t dft[2];
} pl_ts_t;

typedef struct {
    pl_coef_t next;                             /* pl_commit's: what the blocks from now on are to run */
    uint32_t fresh;                             /* next was written since the last block */
    /* the audio side's own, from here on */
    uint8_t run[PL_NMOD];                       /* the effect each module's memory belongs to, or PL_OFF */
    uint8_t order[PL_NMOD];
    uint8_t lead[PL_NMOD];                      /* blocks an effect that just started still runs unheard */
    uint8_t redo[PL_NMOD];                      /* the module is blending to its input to take knobs that jumped */
    float mg[PL_NMOD];                          /* each module's blend: 0 its input, 1 its effect */
    float fg[PL_NMOD];                          /* an effect that just started: its input, fading in */
    float vol[PL_NMOD], og, bg, wet, dry, level;   /* as they glide: the chain's fade (og), bypass's (bg) */
    uint32_t rnd;
    pl_cc_t ch; pl_mc_t mv; pl_dc_t df; pl_tc_t tx;         /* the numbers the running effects use */
    pl_cs_t cs; pl_ms_t ms; pl_ds_t ds; pl_ts_t ts;
    float dryb[PL_FR], tl[PL_FR], tr[PL_FR];    /* the guitar; a module's input while it blends */
    int16_t mvb[PL_MV_N], dfb[PL_DF_N], txb[PL_TX_N];
} pl_engine_t;

void pl_default(pl_sound_t *s);                 /* every module off, every knob neutral */
int  pl_sound_ok(const pl_sound_t *s);          /* 1 = every field is in its range (a sound from the flash or from outside) */
void pl_coefs(const pl_sound_t *s, uint32_t quarter_ms, int bypass, pl_coef_t *c);   /* quarter_ms: a quarter note, 0 = free time */
void pl_commit(pl_engine_t *e, const pl_coef_t *c);         /* with the audio interrupt held */
void pl_reset(pl_engine_t *e);                  /* everything cleared but pl_commit's numbers: before the first block, after a NaN */
void pl_run(pl_engine_t *e, const float *in, float *out);   /* PL_FR frames in; out: PL_FR left, then PL_FR right */
