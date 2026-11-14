/* SPDX-License-Identifier: GPL-3.0-only
 * PALETTE, the app itself (docs/PALETTE.md): its presets, its screen, and what the controls, MIDI and its own editor
 * requests do. The pedal's file (palette.c) and the simulator (`node tools/op.mjs sim --app palette`) both include it,
 * after the base firmware's parts it uses. Its sound is engine.c.
 *
 * What a control does (app_turn, app_button, app_master: base/input.h):
 *   AMP MOD DLY CAB     Character, Movement, Diffusion, Texture: the module's page; pressed again: its next effect;
 *                       held 0.6 s: the module off and on
 *   HOME                the sound's page: PRESET, MIX, LEVEL, TEMPO
 *   the knob            the selected line's value; turned quickly, in steps of 5
 *   PUSH                the next line; held 0.8 s: SAVE the sound to its slot
 *   FX                  MOVE: the page's module is picked up, the knob moves it along the chain, FX or PUSH drops it
 *   REV                 TAP: the time between two taps is a quarter note; held 0.8 s: free time
 *   the footswitches    the preset before, the preset after (when they come up); held 0.7 s: bypass, off and on
 *   MASTER              the volume
 * MIDI (app_midi: base/midi.h), the reference instrument's map:
 *   Program Change 0..79 a preset; Control Change 64..79 the sixteen knob functions (engine.h's order);
 *   16..19 a module's effect (0..109 in five steps, from 110: off); 103..106 a module off (below 64) and on;
 *   91 bypass (below 64) and engage; 84 the filter style (low-pass, tilt, high-pass); 93 a tap; the clock's tempo
 *   while a clock runs.
 * Its own editor requests (app_editor: base/product.h), protocol 1, beside INFO. A value is 0..127:
 *   5 STATE      -> slot, flags (bit 0 the sound differs from its slot's, 1 the slot was saved, 2 bypass, 3 the MIDI
 *                   clock sets the tempo), page, line, then the sound: its name (12), knob (16), fx (4), order (4), on,
 *                   style, tempo (low 7 bits, the bits above)
 *   6 SET f v..  -> f, 0 taken or 1 refused. f 0..15 a knob = v (0..100); 16..19 a module's effect = v; 20..23 a
 *                   module off / on; 24 the filter style; 25 the tempo = v + 128 * v2 (0: free); 26 bypass;
 *                   27 the order = v v2 v3 v4
 *   7 PRESET k   -> k: slot k is loaded
 *   8 SAVE       -> 0 saved, 1 not
 *   9 NAME k     -> k, flags (bit 0 the slot holds a sound, bit 1 it was saved), its name
 *   10 RENAME c.. -> the sound's name is those characters (11 at most); answered as STATE is
 *   11 LOAD      -> the load in % (127 at most), its peak since the sound last changed, the blocks that came late
 *                   (low 7 bits, the bits above), the blocks that reached the output's knee (the same), the NaNs,
 *                   1 when the base bypassed an overload
 * The screen's last line shows the first three: DSP, PEAK, LATE.
 */
#pragma once
#include "engine.h"

#define AP_NSLOT 80u
#define AP_NFACT 20u                            /* the slots with a factory sound */
#define AP_MAGIC 0x544C4150u                    /* "PALT": this app's data in flash (base/store.h) */
#define AP_VER   1u                             /* its format: a change of ap_store_t gets the next number, and app_init reads the old */
typedef struct { char name[12]; pl_sound_t s; } ap_preset_t;                                  /* 40 bytes */
typedef struct { uint8_t last, pad, saved[AP_NSLOT / 8u]; ap_preset_t slot[AP_NSLOT]; } ap_store_t;   /* what the flash keeps */
typedef char ap_size_check[sizeof(ap_preset_t) == 40 && sizeof(ap_store_t) <= BS_MAX ? 1 : -1];

enum { AP_C = 1, AP_M = 2, AP_D = 4, AP_T = 8 };   /* a sound's `on` bits */
/* the knobs in engine.h's order; the four EFFECT VOLs and OUTPUT LEVEL neutral */
#define AP_K(tilt, ac, rate, am, time, ad, mix, at, sens, dm, dd) {tilt, ac, rate, am, time, ad, mix, at, sens, 50, dm, 50, dd, 50, 50, 50}
#define AP_ORD {0, 1, 2, 3}
static const ap_preset_t AP_FACTORY[AP_NFACT] = {
    {"CLEAN ROOM",  {AP_K(55, 40, 50, 50, 25, 30, 100, 50, 50, 0, 0),   {PL_SWEETEN, PL_DOUBLER, PL_SPACE, PL_FILTER},      AP_ORD, AP_C | AP_D, 0, 0}},
    {"WARM DRIVE",  {AP_K(50, 45, 50, 50, 20, 25, 100, 50, 50, 0, 10),  {PL_DRIVE, PL_DOUBLER, PL_CASCADE, PL_FILTER},      AP_ORD, AP_C | AP_D, 0, 0}},
    {"WIDE DOUBLE", {AP_K(50, 50, 30, 60, 50, 50, 100, 35, 50, 20, 0),  {PL_SWEETEN, PL_DOUBLER, PL_SPACE, PL_SQUASH},      AP_ORD, AP_M | AP_T, 0, 0}},
    {"TAPE ECHO",   {AP_K(50, 50, 50, 50, 55, 45, 100, 25, 50, 0, 30),  {PL_SWEETEN, PL_VIBRATO, PL_REELS, PL_CASSETTE},    AP_ORD, AP_D | AP_T, 0, 0}},
    {"BIG SPACE",   {AP_K(55, 35, 35, 20, 85, 55, 100, 50, 50, 30, 30), {PL_SWEETEN, PL_VIBRATO, PL_SPACE, PL_FILTER},      AP_ORD, AP_C | AP_M | AP_D, 0, 0}},
    {"FUZZ WALL",   {AP_K(55, 60, 50, 50, 60, 35, 100, 50, 50, 0, 20),  {PL_FUZZ, PL_PHASER, PL_SPACE, PL_FILTER},          AP_ORD, AP_C | AP_D, 0, 0}},
    {"HOWLER",      {AP_K(60, 55, 30, 40, 45, 35, 100, 50, 50, 20, 20), {PL_HOWL, PL_PHASER, PL_CASCADE, PL_FILTER},        AP_ORD, AP_C | AP_M | AP_D, 0, 0}},
    {"SLOW SWELL",  {AP_K(50, 60, 50, 50, 75, 50, 100, 50, 50, 0, 40),  {PL_SWELL, PL_VIBRATO, PL_SPACE, PL_FILTER},        AP_ORD, AP_C | AP_D, 0, 0}},
    {"PHASE 12",    {AP_K(50, 50, 35, 100, 50, 50, 100, 30, 50, 15, 0), {PL_DRIVE, PL_PHASER, PL_SPACE, PL_SQUASH},         AP_ORD, AP_M | AP_T, 0, 0}},
    {"TREM CHOP",   {AP_K(50, 25, 65, 90, 50, 50, 100, 50, 50, 0, 0),   {PL_DRIVE, PL_TREMOLO, PL_SPACE, PL_FILTER},        AP_ORD, AP_C | AP_M, 0, 0}},
    {"OCTAVE UP",   {AP_K(50, 50, 100, 45, 40, 30, 100, 50, 50, 0, 0),  {PL_DRIVE, PL_PITCH, PL_CASCADE, PL_FILTER},        AP_ORD, AP_M | AP_D, 0, 0}},
    {"SUB DRIFT",   {AP_K(50, 50, 0, 50, 50, 50, 100, 60, 50, 60, 0),   {PL_DRIVE, PL_PITCH, PL_SPACE, PL_FILTER},          AP_ORD, AP_M | AP_T, PL_LP_STYLE, 0}},
    {"COLLAGE",     {AP_K(50, 50, 40, 40, 60, 70, 100, 50, 50, 20, 40), {PL_DRIVE, PL_DOUBLER, PL_COLLAGE, PL_FILTER},      AP_ORD, AP_M | AP_D, 0, 0}},
    {"BACKWARDS",   {AP_K(50, 50, 40, 25, 50, 70, 100, 50, 50, 30, 30), {PL_DRIVE, PL_VIBRATO, PL_REVERSE, PL_FILTER},      AP_ORD, AP_M | AP_D, 0, 0}},
    {"WORN TAPE",   {AP_K(45, 35, 50, 50, 50, 50, 100, 70, 50, 0, 0),   {PL_SWEETEN, PL_VIBRATO, PL_REELS, PL_CASSETTE},    AP_ORD, AP_C | AP_T, 0, 0}},
    {"BAD MOTOR",   {AP_K(50, 50, 50, 50, 45, 40, 100, 55, 50, 0, 50),  {PL_DRIVE, PL_VIBRATO, PL_REELS, PL_BROKEN},        AP_ORD, AP_D | AP_T, 0, 0}},
    {"BAD SIGNAL",  {AP_K(50, 50, 50, 50, 40, 35, 100, 60, 50, 0, 20),  {PL_DRIVE, PL_VIBRATO, PL_CASCADE, PL_INTERF},      AP_ORD, AP_D | AP_T, 0, 0}},
    {"ECHO>FUZZ",   {AP_K(50, 45, 50, 50, 50, 40, 100, 50, 50, 0, 20),  {PL_FUZZ, PL_PHASER, PL_CASCADE, PL_FILTER},        {2, 0, 1, 3}, AP_C | AP_D, 0, 0}},
    {"SPACE>TREM",  {AP_K(50, 50, 60, 75, 70, 60, 100, 50, 50, 20, 20), {PL_DRIVE, PL_TREMOLO, PL_SPACE, PL_FILTER},        {2, 1, 0, 3}, AP_M | AP_D, 0, 0}},
    {"FULL CHAIN",  {AP_K(50, 40, 50, 45, 60, 40, 100, 40, 50, 20, 30), {PL_DRIVE, PL_PHASER, PL_REELS, PL_CASSETTE},       AP_ORD, AP_C | AP_M | AP_D | AP_T, 0, 120}},
};
static const char *const AP_MOD[PL_NMOD] = {"CHARACTER", "MOVEMENT", "DIFFUSION", "TEXTURE"};
static const char *const AP_FX[PL_NMOD][PL_NFX] = {
    {"DRIVE", "SWEETEN", "FUZZ", "HOWL", "SWELL"}, {"DOUBLER", "VIBRATO", "PHASER", "TREMOLO", "PITCH"},
    {"CASCADE", "REELS", "SPACE", "COLLAGE", "REVERSE"}, {"FILTER", "SQUASH", "CASSETTE", "BROKEN", "INTERFERENCE"},
};
static const char *const AP_STYLE[PL_NSTYLE] = {"TILT", "LOW-PASS", "HIGH-PASS"};
static const uint16_t AP_COL[PL_NMOD] = {0xF986, 0xFE60, 0x4F0B, 0x3D5F};   /* red, yellow, green, blue */

/* a page's lines: the sound's page, then a page a module. What the knob changes on each */
enum { AP_L_NONE, AP_L_PRESET, AP_L_TEMPO, AP_L_FX, AP_L_STYLE, AP_L_KNOB };
typedef struct { const char *name; uint8_t kind, knob; } ap_line_t;
#define AP_NLINE 5u
static const ap_line_t AP_LINES[1u + PL_NMOD][AP_NLINE] = {
    {{"PRESET", AP_L_PRESET, 0}, {"MIX", AP_L_KNOB, PL_K_MIX}, {"LEVEL", AP_L_KNOB, PL_K_LEVEL}, {"TEMPO", AP_L_TEMPO, 0}},
    {{"EFFECT", AP_L_FX, 0}, {"AMOUNT", AP_L_KNOB, PL_K_AMT_CHAR}, {"TILT", AP_L_KNOB, PL_K_TILT}, {"SENSITIVITY", AP_L_KNOB, PL_K_SENS}, {"VOLUME", AP_L_KNOB, PL_K_VOL_CHAR}},
    {{"EFFECT", AP_L_FX, 0}, {"AMOUNT", AP_L_KNOB, PL_K_AMT_MOVE}, {"RATE", AP_L_KNOB, PL_K_RATE}, {"DRIFT", AP_L_KNOB, PL_K_DRIFT_MOVE}, {"VOLUME", AP_L_KNOB, PL_K_VOL_MOVE}},
    {{"EFFECT", AP_L_FX, 0}, {"AMOUNT", AP_L_KNOB, PL_K_AMT_DIFF}, {"TIME", AP_L_KNOB, PL_K_TIME}, {"DRIFT", AP_L_KNOB, PL_K_DRIFT_DIFF}, {"VOLUME", AP_L_KNOB, PL_K_VOL_DIFF}},
    {{"EFFECT", AP_L_FX, 0}, {"AMOUNT", AP_L_KNOB, PL_K_AMT_TEXT}, {"STYLE", AP_L_STYLE, 0}, {"VOLUME", AP_L_KNOB, PL_K_VOL_TEXT}},
};

static pl_engine_t ap_eng OP_POOL;              /* the engine, in the POOL region (memory.ld) */
static ap_store_t ap_st;
static ap_preset_t ap_now;                      /* the sound that plays */
static uint32_t ap_slot, ap_page, ap_line, ap_move, ap_bypass;   /* ap_page: 0 the sound's, 1 + m module m's */
static uint32_t ap_clock_q, ap_clock_ms;        /* the MIDI clock's quarter note in ms (0: no clock runs), its last clock */
static uint32_t ap_tap_ms, ap_tap_ok;           /* the last tap */
static uint32_t ap_note_ms;                     /* a word in the status line's place until then */
static const char *ap_note_s;
static midi_tempo_t ap_clk;

/* ---- the sound ---- */
static void app_sound(const float *in, float *out) { pl_run(&ap_eng, in, out); }   /* in the I2S interrupt (base/audio_plain.h) */
static void app_sound_reset(void) { pl_reset(&ap_eng); }
static uint32_t ap_quarter(void) { return ap_clock_q ? ap_clock_q : ap_now.s.tempo ? 60000u / ap_now.s.tempo : 0u; }
/* the sound, the tempo or bypass changed: the engine's numbers are made here, and handed over between two blocks. The
 * engine makes the change without a step (engine.h) */
static void ap_apply(void)
{
    pl_coef_t c;
    pl_coefs(&ap_now.s, ap_quarter(), (int)ap_bypass, &c);
    fm1_irq_mask(FM1_IRQ_ALNK0);
    pl_commit(&ap_eng, &c);
    fm1_irq_unmask(FM1_IRQ_ALNK0);
    a_peak = 0;                                 /* (the peak shown is this sound's, the change itself included) */
}
static void ap_say(const char *s) { ap_note_s = s; ap_note_ms = 1500u; }
static int ap_saved(uint32_t k) { return (ap_st.saved[k / 8u] >> (k % 8u)) & 1u; }
static int ap_used(uint32_t k) { return k < AP_NFACT || ap_saved(k); }
static void ap_slot_get(uint32_t k, ap_preset_t *p)     /* slot k's sound: the saved one, else its factory one, else an empty one */
{
    if (ap_saved(k)) { *p = ap_st.slot[k]; return; }
    if (k < AP_NFACT) { *p = AP_FACTORY[k]; return; }
    memset(p, 0, sizeof *p);
    memcpy(p->name, "EMPTY", 5);
    pl_default(&p->s);
}
static int ap_dirty(void) { ap_preset_t ref; ap_slot_get(ap_slot, &ref); return memcmp(&ref, &ap_now, sizeof ap_now) != 0; }
static void ap_load(uint32_t k)
{
    ap_slot = k; ap_move = 0;
    ap_slot_get(k, &ap_now);
    ap_apply();
}
static void ap_step_preset(int dd)              /* the footswitches: the next slot that holds a sound */
{
    uint32_t k = ap_slot;
    do k = (k + (dd > 0 ? 1u : AP_NSLOT - 1u)) % AP_NSLOT; while (!ap_used(k) && k != ap_slot);
    ap_load(k);
}
static int ap_save(void)
{
    if (!memcmp(ap_now.name, "EMPTY", 6)) {     /* an empty slot's first sound: named after its slot */
        memcpy(ap_now.name, "USER ", 5);
        ap_now.name[5] = (char)('0' + (ap_slot + 1u) / 10u); ap_now.name[6] = (char)('0' + (ap_slot + 1u) % 10u); ap_now.name[7] = 0;
    }
    ap_st.slot[ap_slot] = ap_now; ap_st.saved[ap_slot / 8u] |= (uint8_t)(1u << (ap_slot % 8u)); ap_st.last = (uint8_t)ap_slot;
    const int rc = bs_save(AP_MAGIC, AP_VER, &ap_st, sizeof ap_st);
    ap_say(rc ? "SAVE FAILED" : "SAVED");
    return rc;
}
/* one field of the sound, as MIDI, the editor and the controls set it (the editor's SET numbers); 0 = taken */
enum { AP_F_FX = 16, AP_F_ON = 20, AP_F_STYLE = 24, AP_F_TEMPO, AP_F_BYPASS, AP_F_ORDER };
static int ap_set(uint32_t f, uint32_t v)
{
    pl_sound_t *s = &ap_now.s;
    if (f < PL_NKNOB) s->knob[f] = (uint8_t)(v > 100u ? 100u : v);
    else if (f < AP_F_FX + PL_NMOD) { if (v >= PL_NFX) return 1; s->fx[f - AP_F_FX] = (uint8_t)v; s->on |= (uint8_t)(1u << (f - AP_F_FX)); }
    else if (f < AP_F_ON + PL_NMOD) s->on = (uint8_t)((s->on & ~(1u << (f - AP_F_ON))) | (v ? 1u << (f - AP_F_ON) : 0u));
    else if (f == AP_F_STYLE) { if (v >= PL_NSTYLE) return 1; s->style = (uint8_t)v; }
    else if (f == AP_F_TEMPO) s->tempo = (uint16_t)(v == 0u ? 0u : v < PL_TEMPO_MIN ? PL_TEMPO_MIN : v > PL_TEMPO_MAX ? PL_TEMPO_MAX : v);
    else if (f == AP_F_BYPASS) ap_bypass = v ? 1u : 0u;
    else return 1;
    ap_apply();
    return 0;
}
static void ap_tap(uint32_t ms)                 /* a tap at time ms: with the tap before it, a quarter note (200 ms .. 2 s) */
{
    const uint32_t dt = ms - ap_tap_ms;
    if (ap_tap_ok && dt >= 200u && dt <= 2000u) ap_set(AP_F_TEMPO, (60000u + dt / 2u) / dt);
    ap_tap_ms = ms; ap_tap_ok = 1;
}
static void ap_shift(uint32_t m, int dd)        /* module m one place along the chain */
{
    uint8_t *o = ap_now.s.order;
    uint32_t at = 0;
    while (at < PL_NMOD && o[at] != m) at++;
    const uint32_t to = at + (uint32_t)dd;      /* (dd -1 at place 0 wraps beyond PL_NMOD: refused) */
    if (at >= PL_NMOD || to >= PL_NMOD) return;
    o[at] = o[to]; o[to] = (uint8_t)m;
    ap_apply();
}

/* ---- the controls (base/input.h) ---- */
static const uint16_t AP_HOLD[IN_N] = { [IN_PUSH] = 800, [IN_REV] = 800, [IN_FSUP] = 700, [IN_FSDN] = 700,
                                        [IN_AMP] = 600, [IN_MOD] = 600, [IN_DLY] = 600, [IN_CAB] = 600 };
static const uint8_t AP_BTN_MOD[IN_N] = { [IN_AMP] = 1u + PL_CHAR, [IN_MOD] = 1u + PL_MOVE, [IN_DLY] = 1u + PL_DIFF, [IN_CAB] = 1u + PL_TEXT };
static void app_turn(int dd, int fast)
{
    const ap_line_t *ln = &AP_LINES[ap_page][ap_line];
    const int step = dd * (fast ? 5 : 1);
    pl_sound_t *s = &ap_now.s;
    if (ap_move) { ap_shift(ap_page - 1u, dd); return; }
    switch (ln->kind) {
    case AP_L_KNOB: { const int v = (int)s->knob[ln->knob] + step; ap_set(ln->knob, (uint32_t)(v < 0 ? 0 : v)); break; }
    case AP_L_PRESET: ap_load((ap_slot + (dd > 0 ? 1u : AP_NSLOT - 1u)) % AP_NSLOT); break;
    case AP_L_TEMPO: { const int v = (int)(s->tempo ? s->tempo : PL_TEMPO_MIN - 1u) + step; ap_set(AP_F_TEMPO, v < (int)PL_TEMPO_MIN ? 0u : (uint32_t)v); break; }
    case AP_L_FX: ap_set(AP_F_FX + ap_page - 1u, (s->fx[ap_page - 1u] + (dd > 0 ? 1u : PL_NFX - 1u)) % PL_NFX); break;
    case AP_L_STYLE: { const int v = (int)s->style + dd; if (v >= 0) ap_set(AP_F_STYLE, (uint32_t)v); break; }
    default: break;
    }
}
static void app_button(uint32_t b, uint32_t ev, uint32_t held)
{
    const uint32_t pg = AP_BTN_MOD[b];
    (void)held;
    if (pg) {                                   /* a module's button */
        const uint32_t m = pg - 1u;
        if (ev == IN_HOLD) ap_set(AP_F_ON + m, !((ap_now.s.on >> m) & 1u));
        else if (ev == IN_CLICK && ap_page == pg && !ap_move) ap_set(AP_F_FX + m, (ap_now.s.fx[m] + 1u) % PL_NFX);
        else if (ev == IN_CLICK) { ap_page = pg; ap_line = 1; ap_move = 0; }      /* its page, on AMOUNT */
        return;
    }
    if (b == IN_FSUP || b == IN_FSDN) {
        if (ev == IN_CLICK) ap_step_preset(b == IN_FSUP ? 1 : -1);
        else if (ev == IN_HOLD) ap_set(AP_F_BYPASS, !ap_bypass);
        return;
    }
    if (b == IN_PUSH && ev == IN_HOLD) ap_save();
    if (b == IN_PUSH && ev == IN_CLICK) {
        if (ap_move) ap_move = 0;
        else { ap_line++; if (ap_line >= AP_NLINE || !AP_LINES[ap_page][ap_line].name) ap_line = 0; }
    }
    if (b == IN_REV && ev == IN_HOLD) { ap_tap_ok = 0; ap_set(AP_F_TEMPO, 0); }
    if (ev != IN_DOWN) return;
    if (b == IN_HOME) { ap_page = 0; ap_line = 0; ap_move = 0; }
    if (b == IN_FX) { if (ap_page) ap_move = !ap_move; else ap_say("MOVE: PICK A MODULE FIRST"); }
    if (b == IN_REV) ap_tap(ms_now());
}
static void app_master(uint32_t vol) { audio_volume(vol); }

/* ---- the MIDI map (base/midi.h) ---- */
static void app_midi(const midi_msg_t *m, uint32_t ms)
{
    const uint32_t v = ((uint32_t)m->d2 * 100u + 63u) / 127u;      /* 0..127 -> 0..100 */
    switch (m->type) {
    case MIDI_PC: if (m->d1 < AP_NSLOT) ap_load(m->d1); break;
    case MIDI_CC:
        if (m->d1 >= 64u && m->d1 < 64u + PL_NKNOB) ap_set(m->d1 - 64u, v);
        else if (m->d1 >= 16u && m->d1 < 16u + PL_NMOD) { if (m->d2 >= 110u) ap_set(AP_F_ON + m->d1 - 16u, 0); else ap_set(AP_F_FX + m->d1 - 16u, m->d2 / 22u); }
        else if (m->d1 >= 103u && m->d1 < 103u + PL_NMOD) ap_set(AP_F_ON + m->d1 - 103u, m->d2 >= 64u);
        else if (m->d1 == 91u) ap_set(AP_F_BYPASS, m->d2 < 64u);
        else if (m->d1 == 84u) ap_set(AP_F_STYLE, m->d2 < 44u ? PL_LP_STYLE : m->d2 < 88u ? PL_TILT_STYLE : PL_HP_STYLE);
        else if (m->d1 == 93u) ap_tap(ms);
        break;
    case MIDI_CLOCK: {
        const uint32_t q = midi_tempo(&ap_clk, ms);
        ap_clock_ms = ms;
        if (q && q != ap_clock_q) { ap_clock_q = q; ap_apply(); }
        break;
    }
    case MIDI_START: midi_tempo_reset(&ap_clk); break;
    case MIDI_STOP: midi_tempo_reset(&ap_clk); if (ap_clock_q) { ap_clock_q = 0; ap_apply(); } break;
    }
}

/* ---- its own editor requests (base/product.h) ---- */
static void ap_ed_state(uint32_t cmd)
{
    const pl_sound_t *s = &ap_now.s;
    ed_begin(cmd);
    ed_b(ap_slot); ed_b((ap_dirty() ? 1u : 0u) | (ap_saved(ap_slot) ? 2u : 0u) | (ap_bypass ? 4u : 0u) | (ap_clock_q ? 8u : 0u));
    ed_b(ap_page); ed_b(ap_line);
    for (uint32_t i = 0; i < sizeof ap_now.name; i++) ed_b((uint8_t)ap_now.name[i]);
    for (uint32_t i = 0; i < PL_NKNOB; i++) ed_b(s->knob[i]);
    for (uint32_t i = 0; i < PL_NMOD; i++) ed_b(s->fx[i]);
    for (uint32_t i = 0; i < PL_NMOD; i++) ed_b(s->order[i]);
    ed_b(s->on); ed_b(s->style); ed_b(s->tempo & 127u); ed_b(s->tempo >> 7);
    ed_send();
}
static void app_editor(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    if (cmd == 5u) ap_ed_state(cmd);
    else if (cmd == 6u && n >= 2u) {
        int rc = 1;
        if (a[0] == AP_F_ORDER) {
            pl_sound_t t = ap_now.s;
            for (uint32_t i = 0; i < PL_NMOD && n >= 5u; i++) t.order[i] = a[1u + i];
            if (n >= 5u && pl_sound_ok(&t)) { ap_now.s = t; ap_apply(); rc = 0; }
        } else rc = ap_set(a[0], a[0] == AP_F_TEMPO && n >= 3u ? a[1] + 128u * a[2] : a[1]);
        ed_begin(cmd); ed_b(a[0]); ed_b((uint32_t)rc); ed_send();
    } else if (cmd == 7u && n >= 1u && a[0] < AP_NSLOT) {
        ap_load(a[0]);
        ed_begin(cmd); ed_b(a[0]); ed_send();
    } else if (cmd == 8u) {
        const int rc = ap_save();
        ed_begin(cmd); ed_b(rc ? 1u : 0u); ed_send();
    } else if (cmd == 9u && n >= 1u && a[0] < AP_NSLOT) {
        ap_preset_t p;
        ap_slot_get(a[0], &p);
        ed_begin(cmd); ed_b(a[0]); ed_b((ap_used(a[0]) ? 1u : 0u) | (ap_saved(a[0]) ? 2u : 0u)); ed_str(p.name, 11); ed_send();
    } else if (cmd == 10u) {
        uint32_t i = 0;
        for (; i < n && i < 11u && a[i] >= 32u && a[i] <= 126u; i++) ap_now.name[i] = (char)a[i];
        for (; i < sizeof ap_now.name; i++) ap_now.name[i] = 0;
        ap_ed_state(cmd);
    } else if (cmd == 11u) {
        const uint32_t late = a_xrun > 16383u ? 16383u : a_xrun, clip = a_clip > 16383u ? 16383u : a_clip;
        ed_begin(cmd);
        ed_b(a_load > 127u ? 127u : a_load); ed_b(a_peak > 127u ? 127u : a_peak);
        ed_b(late & 127u); ed_b(late >> 7); ed_b(clip & 127u); ed_b(clip >> 7);
        ed_b(a_nan_count > 127u ? 127u : a_nan_count); ed_b(a_bypass);
        ed_send();
    }
}

/* ---- the screen: the preset, four tiles in the chain's order, the page's lines, a status line. Only what changed is
 * drawn again ---- */
static uint32_t ap_hash(uint32_t h, const char *s) { while (*s) h = h * 31u + (uint8_t)*s++; return h * 31u + 7u; }
static void ap_value(char *l, const ap_line_t *ln)      /* a line's value, as text */
{
    const pl_sound_t *s = &ap_now.s;
    l[0] = 0;
    switch (ln->kind) {
    case AP_L_KNOB: b_num(l, s->knob[ln->knob]); break;
    case AP_L_PRESET: b_num(l, ap_slot + 1u); b_str(l, " / "); b_num(l, AP_NSLOT); break;
    case AP_L_TEMPO:
        if (ap_clock_q) { b_str(l, "CLOCK "); b_num(l, (60000u + ap_clock_q / 2u) / ap_clock_q); }
        else if (s->tempo) { b_num(l, s->tempo); b_str(l, " BPM"); }
        else b_str(l, "FREE");
        break;
    case AP_L_FX: b_str(l, AP_FX[ap_page - 1u][s->fx[ap_page - 1u]]); break;
    case AP_L_STYLE: b_str(l, AP_STYLE[s->style]); break;
    default: break;
    }
}
static void ap_draw(uint32_t now)
{
    static uint32_t seen[11], t_status, t_last, drawn;
    const pl_sound_t *s = &ap_now.s;
    char l[48];
    uint32_t h, k;
    if (!drawn) { drawn = 1; for (k = 0; k < 11u; k++) seen[k] = ~0u; }
    if (ap_note_ms) { const uint32_t dt = now - t_last; ap_note_ms = ap_note_ms > dt ? ap_note_ms - dt : 0u; }
    t_last = now;
    /* the preset: its number, its name, a mark when it differs from its slot's */
    l[0] = 0; b_num(l, ap_slot + 1u); b_str(l, "  "); b_str(l, ap_now.name); if (ap_dirty()) b_str(l, " *");
    h = ap_hash(ap_bypass, l);
    if (h != seen[0]) { seen[0] = h; ft_box(0, 2, 240, l, &FT_MD, ap_bypass ? UI_DIM : UI_TXT, UI_BG, 1); }
    for (k = 0; k < PL_NMOD; k++) {             /* a tile a module, in the chain's order */
        const uint32_t m = s->order[k], on = (s->on >> m) & 1u, sel = ap_page == 1u + m, y = 28u + 26u * k;
        const uint16_t bg = sel ? (ap_move ? UI_ACC : UI_SEL) : UI_PANEL, fg = sel ? UI_BG : on ? UI_TXT : UI_DIM;
        h = m | s->fx[m] << 2 | on << 5 | sel << 6 | (sel ? ap_move : 0u) << 7 | (uint32_t)s->knob[PL_K_AMT(m)] << 8;
        if (h == seen[1u + k]) continue;
        seen[1u + k] = h;
        lcd_rect(0, y, 6, 24, on ? AP_COL[m] : UI_OFF);
        lcd_rect(6, y, 234, 24, bg);
        ft_box(12, y + 4u, 88, AP_MOD[m], &FT_SM, sel ? UI_BG : on ? AP_COL[m] : UI_DIM, bg, 0);
        ft_box(100, y + 4u, 108, AP_FX[m][s->fx[m]], &FT_SM, fg, bg, 0);
        l[0] = 0; if (on) b_num(l, s->knob[PL_K_AMT(m)]); else b_str(l, "OFF");
        ft_box(208, y + 4u, 28, l, &FT_SM, fg, bg, 2);
    }
    for (k = 0; k < AP_NLINE; k++) {            /* the page's lines: a name, its value; the selected one lit */
        const ap_line_t *ln = &AP_LINES[ap_page][k];
        const uint32_t sel = k == ap_line && !ap_move, y = 136u + 17u * k;
        const uint16_t bg = sel ? UI_SEL : UI_BG, fg = sel ? UI_BG : UI_TXT;
        ap_value(l, ln);
        h = ap_hash(ap_hash(ap_page << 4 | k << 1 | sel, ln->name ? ln->name : ""), l);
        if (h == seen[5u + k]) continue;
        seen[5u + k] = h;
        lcd_rect(0, y, 6, 15, bg); lcd_rect(234, y, 6, 15, bg);
        ft_box(6, y, 124, ln->name ? ln->name : "", &FT_SM, sel ? UI_BG : UI_DIM, bg, 0);
        ft_box(130, y, 104, l, &FT_SM, fg, bg, 2);
    }
    if (now - t_status >= 250u || seen[10] == ~0u) {   /* four times a second: a note, or the load, its peak, the late blocks */
        t_status = now ? now : 1u;
        l[0] = 0;
        if (ap_note_ms) b_str(l, ap_note_s);
        else if (base_safe) b_str(l, "SAFE MODE: THE GUITAR AS IT IS");
        else if (a_bypass) b_str(l, "OVERLOAD: THE GUITAR AS IT IS");
        else if (ap_move) b_str(l, "MOVE: TURN, THEN FX OR PUSH");
        else if (ap_bypass) b_str(l, "BYPASS: HOLD A FOOTSWITCH");
        else { b_str(l, "DSP "); b_num(l, a_load); b_str(l, "%  PEAK "); b_num(l, a_peak); b_str(l, "%  LATE "); b_num(l, a_xrun); }
        h = ap_hash(ap_note_ms ? 1u : 0u, l);
        if (h != seen[10]) { seen[10] = h; ft_box(0, 224, 240, l, &FT_SM, ap_note_ms || ap_bypass || ap_move ? UI_ACC : UI_DIM, UI_BG, 1); }
    }
}

/* ---- the start and the main loop (palette.c's app_main, the simulator's) ---- */
static int ap_name_ok(const char *n)
{
    if (n[11]) return 0;
    for (uint32_t i = 0; i < 11u && n[i]; i++) if (n[i] < 32 || n[i] > 126) return 0;
    return 1;
}
static void app_init(void)                      /* after base_begin(): the saved sounds, the slot to start on. Interrupts off */
{
    static ap_store_t t;
    pl_coef_t c;
    uint32_t len = 0, ver = 0, k;
    int ok = bs_load(AP_MAGIC, &t, sizeof t, &len, &ver) && ver == AP_VER && len == sizeof t && t.last < AP_NSLOT;
    for (k = 0; ok && k < AP_NSLOT; k++)        /* what the flash held is checked like anything from outside */
        if (((t.saved[k / 8u] >> (k % 8u)) & 1u) && (!pl_sound_ok(&t.slot[k].s) || !ap_name_ok(t.slot[k].name))) ok = 0;
    if (ok) ap_st = t;
    ap_slot = ap_used(ap_st.last) ? ap_st.last : 0u;
    ap_slot_get(ap_slot, &ap_now);
    pl_coefs(&ap_now.s, ap_quarter(), 0, &c);   /* (the POOL region is not zeroed: the engine is set, then cleared) */
    pl_commit(&ap_eng, &c);
    pl_reset(&ap_eng);
    in_start(AP_HOLD);
    lcd_rect(0, 0, 240, 240, UI_BG);
}
static void app_step(uint32_t now)              /* every main-loop pass, after base_loop(now) */
{
    in_poll(now);
    /* the clock stopped (a clock can be newer than `now`, which was read before this pass took it: a signed difference) */
    if (ap_clock_q && (int32_t)(now - ap_clock_ms) > 500) { ap_clock_q = 0; midi_tempo_reset(&ap_clk); ap_apply(); }
    ap_draw(now);
}
