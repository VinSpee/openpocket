/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — host test of PALETTE's engine (firmware/apps/palette/engine.c, docs/PALETTE.md): the engine's own code
 * with the firmware's math library. Every effect on loud, quiet and silent input at each knob's ends stays finite and
 * under full scale and is not silent; a change of effect, of order, of a module's switch, of bypass and of the whole
 * sound makes no step in the output, where a cleared engine does; bypass is the guitar bit for bit; the order is heard;
 * a tempo sets the delay's time; the left output alone and the sum of both are each a whole sound; a sound that is out
 * of range is refused.
 *   cc -O1 -Wall -Ifirmware/apps/palette tests/test_palette.c firmware/apps/palette/engine.c firmware/px_math.c -lm -o build/test_palette
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "engine.h"

static int fails;
#define CHECK(c, what) do { const int ok_ = (c); printf("%-110s %s\n", what, ok_ ? "ok" : "FAIL"); if (!ok_) fails++; } while (0)

static pl_engine_t eng;
static float out[2 * PL_FR];
static uint32_t at;                             /* the input's sample counter */
enum { IN_SINE, IN_LOUD, IN_QUIET, IN_SILENT, IN_CLICK };
static void input(int kind, float *x)
{
    for (uint32_t i = 0; i < PL_FR; i++, at++) {
        const float ph = (float)(at % 44100u) / 44100.0f;
        if (kind == IN_SINE) x[i] = 0.2f * sinf(6.2831853f * 110.0f * ph);
        else if (kind == IN_LOUD) x[i] = (sinf(6.2831853f * 196.0f * ph) > 0.0f ? 0.5f : -0.5f) * ((at / 11025u) & 1u ? 1.0f : 0.1f);   /* full input, in bursts */
        else if (kind == IN_QUIET) x[i] = 0.002f * sinf(6.2831853f * 330.0f * ph);
        else if (kind == IN_CLICK) x[i] = at == 0u ? 0.4f : 0.0f;
        else x[i] = 0.0f;
    }
}
static void set(const pl_sound_t *s, uint32_t q, int bypass) { pl_coef_t c; pl_coefs(s, q, bypass, &c); pl_commit(&eng, &c); }
static void start(const pl_sound_t *s, uint32_t q) { set(s, q, 0); pl_reset(&eng); at = 0; }
typedef struct { double rms[2], mid, in, peak, step; int finite; } run_t;
/* n blocks of an input through the engine: the two outputs' RMS, their sum's, the input's, the peak, the largest
 * difference between two samples in a row (either side, from the last sample of the run before on), whether every
 * sample was a number */
static run_t run(int kind, uint32_t n)
{
    static float prev[2];
    run_t v = {{0, 0}, 0, 0, 0, 0, 1};
    float x[PL_FR];
    double e[2] = {0, 0}, em = 0, ei = 0;
    for (uint32_t b = 0; b < n; b++) {
        input(kind, x);
        pl_run(&eng, x, out);
        for (uint32_t i = 0; i < PL_FR; i++) {
            const float l = out[i], r = out[PL_FR + i];
            if (!isfinite(l) || !isfinite(r)) v.finite = 0;
            e[0] += (double)l * l; e[1] += (double)r * r; em += 0.25 * ((double)l + r) * ((double)l + r); ei += (double)x[i] * x[i];
            if (fabsf(l) > v.peak) v.peak = fabsf(l);
            if (fabsf(r) > v.peak) v.peak = fabsf(r);
            v.step = fmax(v.step, fmax(fabsf(l - prev[0]), fabsf(r - prev[1])));   /* (the run before's last sample counts) */
            prev[0] = l; prev[1] = r;
        }
    }
    const double k = 1.0 / ((double)n * PL_FR);
    v.rms[0] = sqrt(e[0] * k); v.rms[1] = sqrt(e[1] * k); v.mid = sqrt(em * k); v.in = sqrt(ei * k);
    return v;
}
static const char *const FX[PL_NMOD][PL_NFX] = {
    {"Drive", "Sweeten", "Fuzz", "Howl", "Swell"}, {"Doubler", "Vibrato", "Phaser", "Tremolo", "Pitch"},
    {"Cascade", "Reels", "Space", "Collage", "Reverse"}, {"Filter", "Squash", "Cassette", "Broken", "Interference"},
};
static void one(pl_sound_t *s, uint32_t m, uint32_t fx) { pl_default(s); s->fx[m] = (uint8_t)fx; s->on = (uint8_t)(1u << m); }

int main(void)
{
    pl_sound_t s, t;
    char what[200];
    run_t v;

    /* ---- a sound from outside ---- */
    pl_default(&s);
    CHECK(pl_sound_ok(&s) && sizeof s == 28, "the default sound is in range; a sound is 28 bytes");
    {
        int refused = 0;
        t = s; t.knob[PL_K_MIX] = 101; refused += !pl_sound_ok(&t);
        t = s; t.fx[PL_DIFF] = PL_NFX; refused += !pl_sound_ok(&t);
        t = s; t.order[1] = 0; refused += !pl_sound_ok(&t);
        t = s; t.order[3] = 4; refused += !pl_sound_ok(&t);
        t = s; t.on = 16; refused += !pl_sound_ok(&t);
        t = s; t.style = PL_NSTYLE; refused += !pl_sound_ok(&t);
        t = s; t.tempo = PL_TEMPO_MIN - 1; refused += !pl_sound_ok(&t);
        t = s; t.tempo = PL_TEMPO_MAX + 1; refused += !pl_sound_ok(&t);
        t = s; t.tempo = 120; t.order[0] = 3; t.order[3] = 0; refused += pl_sound_ok(&t) ? 0 : 100;
        CHECK(refused == 8, "a knob above 100, an effect, an order, a switch, a style or a tempo out of range: refused; a tempo and another order: taken");
    }
    CHECK(sizeof(pl_engine_t) <= 128u * 1024u, "the engine's memory is under 128 KiB of the app's 304 KiB POOL");

    /* ---- every module off, MIX at 0, bypass: the guitar ---- */
    start(&s, 0);
    v = run(IN_SINE, 40);
    CHECK(v.finite && fabs(v.rms[0] - v.in) < 1e-6 && fabs(v.rms[1] - v.in) < 1e-6, "every module off: the guitar on both outputs");
    one(&t, PL_CHAR, PL_FUZZ); t.knob[PL_K_MIX] = 0;
    start(&t, 0);
    v = run(IN_SINE, 40);
    CHECK(fabs(v.rms[0] - v.in) < 1e-5 && fabs(v.rms[1] - v.in) < 1e-5, "MIX at 0: the guitar, whatever the modules do");
    {
        float x[PL_FR]; int same = 1;
        one(&t, PL_DIFF, PL_SPACE); start(&t, 0); run(IN_SINE, 40);
        set(&t, 0, 1); run(IN_SINE, 6);
        for (int b = 0; b < 20; b++) { input(IN_SINE, x); pl_run(&eng, x, out); if (memcmp(out, x, sizeof x) || memcmp(out + PL_FR, x, sizeof x)) same = 0; }
        CHECK(same, "bypass, once its crossfade is over: both outputs are the guitar bit for bit");
        set(&t, 0, 0); v = run(IN_SINE, 100);
        CHECK(v.finite && fabs(v.rms[0] - v.in) > 1e-4, "engaged again: the effect is back");
    }

    /* ---- every effect at its knobs' ends, on loud, quiet and silent input ---- */
    {
        static const uint8_t ENDS[3] = {0, 50, 100};
        int bad = 0, silent = 0, runs = 0;
        for (uint32_t m = 0; m < PL_NMOD; m++) for (uint32_t fx = 0; fx < PL_NFX; fx++) {
            int fxbad = 0;
            for (int a = 0; a < 3; a++) for (int o = 0; o < 3; o++) for (int d = 0; d < 2; d++) for (int st = 0; st < (m == PL_TEXT && fx == PL_FILTER ? 3 : 1); st++) {
                one(&t, m, fx);
                t.knob[PL_K_AMT(m)] = ENDS[a]; t.style = (uint8_t)st;
                t.knob[PL_K_TILT] = t.knob[PL_K_RATE] = t.knob[PL_K_TIME] = t.knob[PL_K_SENS] = ENDS[o];
                t.knob[PL_K_DRIFT_MOVE] = t.knob[PL_K_DRIFT_DIFF] = d ? 100 : 0;
                t.knob[PL_K_VOL(m)] = t.knob[PL_K_LEVEL] = ENDS[o];
                start(&t, 0);
                v = run(IN_LOUD, 345); runs++;
                if (!v.finite || v.peak > 1.0) fxbad++;
                if (v.rms[0] < 1e-4 || v.rms[1] < 1e-4) { if (!(m == PL_TEXT && fx == PL_FILTER)) silent++; }   /* (a filter at its end may cut everything) */
                v = run(IN_QUIET, 60); if (!v.finite || v.peak > 1.0) fxbad++;
                v = run(IN_SILENT, 345); if (!v.finite || v.peak > 1.0) fxbad++;
            }
            if (fxbad) { snprintf(what, sizeof what, "%s: %d run(s) not finite or above full scale", FX[m][fx], fxbad); CHECK(0, what); }
            bad += fxbad;
        }
        snprintf(what, sizeof what, "the 20 effects at their knobs' ends, %d settings, loud, quiet and silent input: finite, under full scale", runs);
        CHECK(!bad, what);
        CHECK(!silent, "and none is silent on a loud input, on either output");
    }
    {   /* the four modules together, every knob up, the loudest input: the heaviest sums */
        int bad = 0;
        for (uint32_t k = 0; k < 20u; k++) {
            pl_default(&t); t.on = 15;
            for (uint32_t m = 0; m < PL_NMOD; m++) { t.fx[m] = (uint8_t)((k + m * (k / 5u + 1u)) % PL_NFX); t.order[m] = (uint8_t)((m + k) % PL_NMOD); }
            for (uint32_t i = 0; i < PL_NKNOB; i++) t.knob[i] = 100;
            start(&t, k & 1u ? 500u : 0u);
            v = run(IN_LOUD, 690);
            if (!v.finite || v.peak > 1.0 || v.rms[0] < 1e-4) bad++;
        }
        CHECK(!bad, "20 chains of four effects in four orders, every knob at 100, with and without a tempo: finite, under full scale, not silent");
    }

    /* ---- changes make no step ---- */
    {
        /* what a step is: the same sine through an effect, then the output level changed with the engine cleared under
         * it, which takes the new level at once (five times, so that one lands off the sine's zero). The changes below
         * must stay near the steady sine's own movement, far under that */
        double hard = 0, soft;
        one(&s, PL_TEXT, PL_SQUASH); s.knob[PL_K_AMT_TEXT] = 80;
        start(&s, 0); run(IN_SINE, 100);
        soft = run(IN_SINE, 100).step;
        for (int k = 0; k < 5; k++) {
            t = s; t.knob[PL_K_LEVEL] = k & 1 ? 50 : 0;
            set(&t, 0, 0); pl_reset(&eng);
            hard = fmax(hard, run(IN_SINE, 7).step);
        }
        set(&s, 0, 0); run(IN_SINE, 100);
        snprintf(what, sizeof what, "a steady sine through an effect moves %.4f a sample at most; a change made at once jumps %.4f", soft, hard);
        CHECK(hard > 10.0 * soft, what);
#define NO_STEP(text) do { v = run(IN_SINE, 40); snprintf(what, sizeof what, "%s: no step (%.4f a sample at most)", text, v.step); \
                           CHECK(v.finite && v.step < 3.0 * soft, what); } while (0)
        t = s; t.fx[PL_TEXT] = PL_FILTER; t.knob[PL_K_AMT_TEXT] = 30; set(&t, 0, 0); NO_STEP("another effect in a module (Squash to Filter)");
        t.on = 0; set(&t, 0, 0); NO_STEP("the module off");
        t.on = 8; set(&t, 0, 0); NO_STEP("and on");
        t.on = 8 | 4; t.fx[PL_DIFF] = PL_SPACE; set(&t, 0, 0); NO_STEP("a second module on (Space)");
        t.order[0] = 3; t.order[1] = 2; t.order[2] = 1; t.order[3] = 0; set(&t, 0, 0); NO_STEP("another order");
        t.fx[PL_DIFF] = PL_CASCADE; t.fx[PL_TEXT] = PL_SQUASH; set(&t, 0, 0); NO_STEP("two effects at once");
        set(&t, 0, 1); NO_STEP("bypass");
        set(&t, 0, 0); NO_STEP("engage");
        pl_default(&t); t.on = 2; t.fx[PL_MOVE] = PL_TREMOLO; t.knob[PL_K_MIX] = 60; t.knob[PL_K_LEVEL] = 70; set(&t, 0, 0); NO_STEP("another sound altogether");
        t.knob[PL_K_VOL_MOVE] = 0; t.knob[PL_K_LEVEL] = 20; set(&t, 0, 0); NO_STEP("a volume and the output level from one end to the other");
    }
    {   /* a tail is kept by a module whose effect stays: the reverb rings on while another module's effect changes */
        pl_default(&s); s.on = 4 | 8; s.fx[PL_DIFF] = PL_SPACE; s.knob[PL_K_TIME] = 90; s.knob[PL_K_AMT_DIFF] = 60;
        start(&s, 0); run(IN_LOUD, 200);
        t = s; t.fx[PL_TEXT] = PL_SQUASH; set(&t, 0, 0);
        v = run(IN_SILENT, 60);
        CHECK(v.rms[0] > 1e-3, "Texture's effect changes while Space rings: Space's tail is still there");
        t.fx[PL_DIFF] = PL_CASCADE; set(&t, 0, 0); run(IN_SILENT, 8);
        v = run(IN_SILENT, 60);
        CHECK(v.rms[0] < 1e-5 && v.rms[1] < 1e-5, "Diffusion's own effect changes: its memory is cleared, nothing of the reverb is left in the delay");
    }

    /* ---- the order is heard; a tempo sets the time ---- */
    {
        double a, b;
        pl_default(&s); s.on = 1 | 4; s.fx[PL_CHAR] = PL_FUZZ; s.fx[PL_DIFF] = PL_SPACE; s.knob[PL_K_AMT_DIFF] = 60;
        start(&s, 0); run(IN_LOUD, 100); a = run(IN_SILENT, 30).rms[0];
        t = s; t.order[0] = PL_DIFF; t.order[2] = PL_CHAR;
        start(&t, 0); run(IN_LOUD, 100); b = run(IN_SILENT, 30).rms[0];
        snprintf(what, sizeof what, "Fuzz then Space leaves a tail of %.4f; Space then Fuzz, the fuzz sustaining it, a louder one: %.4f", a, b);
        CHECK(a > 1e-4 && b > 1.2 * a, what);
    }
    {
        static const struct { uint16_t q; uint8_t time; double ms; } T[4] = {{0, 0, 40.0}, {500, 100, 500.0}, {500, 50, 250.0}, {800, 0, 200.0}};
        int good = 0; double got[4];
        for (int k = 0; k < 4; k++) {
            float x[PL_FR]; uint32_t first = 0;
            one(&s, PL_DIFF, PL_CASCADE); s.knob[PL_K_TIME] = T[k].time; s.knob[PL_K_AMT_DIFF] = 0;
            start(&s, T[k].q); run(IN_SILENT, 4); at = 0;
            for (uint32_t b = 0; b < 400u && !first; b++) {
                input(IN_CLICK, x); pl_run(&eng, x, out);
                for (uint32_t i = 0; i < PL_FR && !first; i++) if (b * PL_FR + i > 200u && fabsf(out[i]) > 0.02f) first = b * PL_FR + i;
            }
            got[k] = (double)first / 44.1;
            if (fabs(got[k] - T[k].ms) < 2.0) good++;
        }
        snprintf(what, sizeof what, "Cascade's repeat: free time 40 ms (%.1f); at 120 a quarter note (%.1f) and an eighth (%.1f); at 75 a sixteenth (%.1f)", got[0], got[1], got[2], got[3]);
        CHECK(good == 4, what);
    }

    /* ---- one output alone, and both summed, are each a whole sound ---- */
    {
        int thin = 0;
        for (uint32_t m = 0; m < PL_NMOD; m++) for (uint32_t fx = 0; fx < PL_NFX; fx++) for (int d = 0; d < 2; d++) {
            one(&t, m, fx); t.knob[PL_K_DRIFT_MOVE] = t.knob[PL_K_DRIFT_DIFF] = d ? 100 : 0;
            if (m == PL_TEXT && fx == PL_FILTER) t.knob[PL_K_AMT_TEXT] = 35;
            start(&t, 0); run(IN_SINE, 100);
            v = run(IN_SINE, 345);
            if (v.rms[0] < 0.1 * v.in || v.rms[1] < 0.1 * v.in || v.mid < 0.1 * v.in) { thin++; printf("   %s: left %.4f right %.4f sum %.4f of %.4f\n", FX[m][fx], v.rms[0], v.rms[1], v.mid, v.in); }
        }
        CHECK(!thin, "each effect, DRIFT at 0 and at 100: the left output, the right one and their sum each carry the sound (a mono jack loses nothing)");
    }

    printf(fails ? "palette engine: %d FAILURES\n" : "palette engine: ok (0 failures)\n", fails);
    return fails ? 1 : 0;
}
