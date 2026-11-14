/* SPDX-License-Identifier: GPL-3.0-only
 * PALETTE (firmware/apps/palette, docs/PALETTE.md): each of its twenty effects on a standard signal, through the
 * engine's own code and the firmware's math library, as a table for docs/PALETTE-REPORT.md: the loudness against the
 * guitar's with AMOUNT at 0, 50 and 100, the highest peak of the three, and the time a sample takes on this computer
 * with AMOUNT at 50. With a folder it also writes each effect, AMOUNT at 50, as a WAV.
 *   cc -O2 -fno-vectorize -fno-slp-vectorize -Ifirmware/apps/palette -o build/palette_render tests/palette_render.c \
 *      firmware/apps/palette/engine.c firmware/px_math.c -lm
 *   build/palette_render [--wav DIR] [--runs N]
 * The signal is the base firmware's test guitar (firmware/base/audio.h, OP_TESTSIG): a note and its fifth, sawtooth,
 * plucked every 0.4 s at about -12 dBFS and decaying, the notes walking two octaves. Twelve seconds of it; the first
 * two are left out of the levels (the effect fades in, its tail builds up).
 * The time is this process's CPU time, the least of N runs (default 9) of the whole signal, less the same engine with
 * every module off: the least, because another program on the same computer can only make a run slower.
 * Built with -DPL_REFERENCE -Iengine and engine/px_dsp.c it also times three blocks of OpenPocket's engine in the same
 * run, whose loads on a pedal are known (an earlier task read them with `node tools/op.mjs dsp` on v40): what turns
 * this computer's time into a share of the pedal's block.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "engine.h"
#ifdef PL_REFERENCE
#include "px_dsp.h"
#endif

#define SECONDS 12u
#define SKIP 2u
#define NB (SECONDS * 44100u / PL_FR)
static float sig[NB * PL_FR], outl[NB * PL_FR], outr[NB * PL_FR];
static pl_engine_t eng;

static void guitar(void)
{
    static const float HZ[8] = {82.4f, 110.0f, 146.8f, 196.0f, 246.9f, 329.6f, 196.0f, 110.0f};
    float ph[2] = {0, 0}, env = 0.25f;
    uint32_t n = 0, k = 0;
    for (uint32_t i = 0; i < NB * PL_FR; i++, n++) {
        if (n >= 17640u) { n = 0; k = (k + 1u) & 7u; env = 0.25f; }
        const float f = HZ[k] / 44100.0f;
        ph[0] += f; if (ph[0] >= 1.0f) ph[0] -= 1.0f;
        ph[1] += f * 1.4983f; if (ph[1] >= 1.0f) ph[1] -= 1.0f;
        sig[i] = 0.5f * env * ((2.0f * ph[0] - 1.0f) + 0.6f * (2.0f * ph[1] - 1.0f));   /* (x 0.5: as a_input halves the converter's) */
        env *= 0.99995f;
    }
}
static double now_ns(void) { struct timespec t; clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t); return (double)t.tv_sec * 1e9 + (double)t.tv_nsec; }

/* the whole signal through a sound; the time a sample took, in ns */
static double render(const pl_sound_t *s)
{
    pl_coef_t c;
    float out[2 * PL_FR];
    pl_coefs(s, 0, 0, &c);
    pl_commit(&eng, &c);
    pl_reset(&eng);
    const double t0 = now_ns();
    for (uint32_t b = 0; b < NB; b++) {
        pl_run(&eng, sig + b * PL_FR, out);
        memcpy(outl + b * PL_FR, out, sizeof(float) * PL_FR);
        memcpy(outr + b * PL_FR, out + PL_FR, sizeof(float) * PL_FR);
    }
    return (now_ns() - t0) / (double)(NB * PL_FR);
}
static double timed(const pl_sound_t *s, int runs)
{
    double best = 1e30;
    for (int i = 0; i < runs; i++) { const double t = render(s); if (t < best) best = t; }
    return best;
}
typedef struct { double rms_db, peak, diff_db; int finite; } level_t;
static double db(double ms) { return ms > 1e-20 ? 10.0 * log10(ms) : -200.0; }
static level_t level(void)
{
    level_t v = {0, 0, 0, 1};
    double se = 0, sd = 0, sg = 0;
    for (uint32_t i = SKIP * 44100u; i < NB * PL_FR; i++) {
        const double l = outl[i], r = outr[i], g = sig[i];
        if (!isfinite(l) || !isfinite(r)) v.finite = 0;
        se += 0.5 * (l * l + r * r); sd += 0.5 * ((l - g) * (l - g) + (r - g) * (r - g)); sg += g * g;
        if (fabs(l) > v.peak) v.peak = fabs(l);
        if (fabs(r) > v.peak) v.peak = fabs(r);
    }
    v.rms_db = db(se) - db(sg); v.diff_db = db(sd) - db(sg);
    return v;
}
static void wav(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s.wav", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "%s: cannot write\n", path); exit(2); }
    const uint32_t n = NB * PL_FR, bytes = n * 4u, rate = 44100u, brate = rate * 4u, riff = 36u + bytes, fmt = 16u;
    const uint16_t pcm = 1, ch = 2, align = 4, bits = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmt, 4, 1, f);
    fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&brate, 4, 1, f); fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    for (uint32_t i = 0; i < n; i++) {
        const int16_t s[2] = {(int16_t)lrintf(fmaxf(-1.0f, fminf(1.0f, outl[i])) * 32767.0f), (int16_t)lrintf(fmaxf(-1.0f, fminf(1.0f, outr[i])) * 32767.0f)};
        fwrite(s, 2, 2, f);
    }
    fclose(f);
}

#ifdef PL_REFERENCE
/* one block type of OpenPocket's engine alone, every knob at 50, on the same signal: ns a sample, less the engine with
 * every block off */
static px_engine ref_eng;
static double ref_run(const px_preset_t *p, int runs)
{
    static float o[PX_MAX_BLOCK];
    double best = 1e30;
    for (int k = 0; k < runs; k++) {
        px_engine_init(&ref_eng, 44100.0f);
        px_engine_set(&ref_eng, p);
        const double t0 = now_ns();
        for (uint32_t b = 0; b < 2u * NB; b++) px_engine_process(&ref_eng, sig + b * PX_MAX_BLOCK, o, PX_MAX_BLOCK);   /* (a block as two halves, as the firmware) */
        const double t = (now_ns() - t0) / (double)(NB * PL_FR);
        if (t < best) best = t;
    }
    return best;
}
static void reference(int runs)
{
    static const struct { int blk; const char *type; int pedal; } REF[3] = {
        {PX_B_REVERB, "ROOM", 9}, {PX_B_DELAY, "TAPE", 3}, {PX_B_MOD, "CHORUS", 4} };
    px_preset_t p;
    memset(&p, 0, sizeof p);
    px_order_default(p.order);
    for (int b = 0; b < PX_NBLK; b++) for (int k = 0; k < PX_NPAR; k++) p.p[b][k] = 50;
    const double off = ref_run(&p, runs);
    printf("\n| OpenPocket's block | Host, ns a sample | On a pedal, %% of a block | %% a ns |\n| --- | --- | --- | --- |\n");
    for (int r = 0; r < 3; r++) {
        px_preset_t q = p;
        int type = -1;
        for (int t = 0; t < px_ntypes(REF[r].blk) && type < 0; t++) if (strstr(px_type_name(REF[r].blk, t), REF[r].type)) type = t;
        if (type < 0) { printf("| %s | not found | | |\n", REF[r].type); continue; }
        q.on[REF[r].blk] = 1; q.type[REF[r].blk] = (uint8_t)type;
        const double t = ref_run(&q, runs) - off;
        printf("| %s %s | %.2f | %d | %.2f |\n", px_block_name(REF[r].blk), px_type_name(REF[r].blk, type), t, REF[r].pedal, (double)REF[r].pedal / t);
    }
}
#endif

static const char *const MOD[PL_NMOD] = {"Character", "Movement", "Diffusion", "Texture"};
static const char *const FX[PL_NMOD][PL_NFX] = {
    {"Drive", "Sweeten", "Fuzz", "Howl", "Swell"}, {"Doubler", "Vibrato", "Phaser", "Tremolo", "Pitch"},
    {"Cascade", "Reels", "Space", "Collage", "Reverse"}, {"Filter", "Squash", "Cassette", "Broken", "Interference"},
};
/* one effect alone, as a factory sound would set it: AMOUNT 50 and the shared knobs at their centres, but where the
 * centre is "nothing" (PITCH's RATE, FILTER's tilt): there a setting that is heard */
static void one(pl_sound_t *s, uint32_t m, uint32_t fx, uint8_t amount)
{
    pl_default(s);
    s->fx[m] = (uint8_t)fx; s->on = (uint8_t)(1u << m); s->knob[PL_K_AMT(m)] = amount;
    if (m == PL_MOVE && fx == PL_PITCH) s->knob[PL_K_RATE] = 85;       /* a fifth up */
    if (m == PL_TEXT && fx == PL_FILTER && amount == 50) s->knob[PL_K_AMT_TEXT] = 30;   /* highs cut */
}

int main(int argc, char **argv)
{
    const char *dir = 0;
    int runs = 9, bad = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--wav") && i + 1 < argc) dir = argv[++i];
        else if (!strcmp(argv[i], "--runs") && i + 1 < argc) runs = atoi(argv[++i]);
        else { fprintf(stderr, "usage: palette_render [--wav DIR] [--runs N]\n"); return 2; }
    }
    if (runs < 1 || runs > 64) runs = 9;
    pl_sound_t s;
    guitar();
    pl_default(&s);
    const double base = timed(&s, runs);
    printf("sizeof(pl_engine_t) = %zu bytes, sizeof(pl_sound_t) = %zu bytes\n", sizeof(pl_engine_t), sizeof(pl_sound_t));
    printf("every module off: %.2f ns a sample\n\n", base);
    printf("| Module | Effect | Loudness at AMOUNT 0, dB | at 50 | at 100 | Peak, of full scale | Changed at 50, dB | Host, ns a sample |\n| --- | --- | --- | --- | --- | --- | --- | --- |\n");
    for (uint32_t m = 0; m < PL_NMOD; m++) for (uint32_t fx = 0; fx < PL_NFX; fx++) {
        level_t v[3];
        double peak = 0;
        int finite = 1;
        one(&s, m, fx, 0); render(&s); v[0] = level();
        one(&s, m, fx, 100); render(&s); v[2] = level();
        one(&s, m, fx, 50);
        const double t = timed(&s, runs) - base;
        v[1] = level();
        if (dir) { char name[64]; snprintf(name, sizeof name, "%u%u-%s", m + 1u, fx + 1u, FX[m][fx]); wav(dir, name); }
        for (int k = 0; k < 3; k++) { if (v[k].peak > peak) peak = v[k].peak; finite &= v[k].finite; }
        if (!finite || peak > 1.0 || v[1].rms_db < -60.0) bad++;
        printf("| %s | %s | %+.1f | %+.1f | %+.1f | %.3f | %+.1f | %.2f |%s\n", fx ? "" : MOD[m], FX[m][fx], v[0].rms_db, v[1].rms_db,
               v[2].rms_db, peak, v[1].diff_db, t, finite ? "" : " NOT FINITE");
    }
    printf("\n| Chain | Host, ns a sample |\n| --- | --- |\n");
    {
        static const uint8_t CH[4][8] = {   /* the effects of the four modules, then the order */
            {PL_HOWL, PL_PHASER, PL_SPACE, PL_CASSETTE, 0, 1, 2, 3}, {PL_HOWL, PL_PITCH, PL_SPACE, PL_CASSETTE, 0, 1, 2, 3},
            {PL_HOWL, PL_PHASER, PL_SPACE, PL_CASSETTE, 2, 0, 3, 1}, {PL_DRIVE, PL_DOUBLER, PL_COLLAGE, PL_BROKEN, 0, 1, 2, 3},
        };
        for (int k = 0; k < 4; k++) {
            pl_default(&s); s.on = 15;
            for (uint32_t m = 0; m < PL_NMOD; m++) { s.fx[m] = CH[k][m]; s.order[m] = CH[k][4 + m]; }
            const double t = timed(&s, runs) - base;
            const level_t v = level();
            if (!v.finite || v.peak > 1.0) bad++;
            printf("| %s > %s > %s > %s | %.2f |\n", FX[s.order[0]][s.fx[s.order[0]]], FX[s.order[1]][s.fx[s.order[1]]],
                   FX[s.order[2]][s.fx[s.order[2]]], FX[s.order[3]][s.fx[s.order[3]]], t);
        }
    }
#ifdef PL_REFERENCE
    reference(runs);
#endif
    if (bad) { printf("\n%d row(s) not finite, silent or above full scale\n", bad); return 1; }
    return 0;
}
