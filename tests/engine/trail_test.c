/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket: trails (px_engine_switch). The outgoing preset's delay and reverb ring out after a change:
 *  1. a delay's trail is that delay's own tail: against an engine that stays on the outgoing preset, sample for sample
 *     (within a step of the 16-bit delay memory), every delay type, wherever the write index is in the delay memory when
 *     the change comes;
 *  2. the same for every reverb type (SHIMMER apart: its trail makes no new octaves, so it is only heard and ends);
 *  3. with the new preset's own delay and reverb playing beside them, the trails are the same;
 *  4. with the wet lanes, run the firmware's way: the same tail, 128 frames later; through the two-core pipeline of a
 *     NAM preset: as loud;
 *  5. they come in without a step, and they end: once quiet, by px_trails_stop (a fade, no step), a delay that feeds
 *     itself for ever after 20 s and 2 s of fade; afterwards the output is the engine's without trails, bit for bit, and
 *     the delay has its memory again;
 *  6. no room in the delay memory: that block changes as px_engine_set changes it, bit for bit;
 *  7. a change that leaves the delay and the reverb as they are is px_engine_set's, bit for bit; a delay switched off
 *     with echoes in its memory does not play them again after a change;
 *  8. px_engine_change and px_trail_due: a delay time the line cannot hold while trails ring is a full change;
 *  9. many changes at random (presets, knobs, lanes on and off): no NaN, the stretches of the delay memory never
 *     overlap, free ticks are zero, a delay's taps stay in its line; in the end silence and the whole memory back. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../../engine/px_dsp.c"
#define SR 44100
#define NB 64
#define MAXS (44 * SR)
#define FADE 256                                       /* frames a trail takes to come in (TR_FADE_FR) */
#define GAP 8192                                       /* silence before a change: the chain itself is quiet by then */
static px_engine E;
static float in[MAXS], ya[MAXS], yb[MAXS], yc[MAXS];
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; if (fails < 40) { printf("FAIL: " __VA_ARGS__); printf("\n"); } } } while (0)

/* plucked notes for `notes` samples (the last 4096 fading out, so that nothing is left in the chain's own filters
 * afterwards), then silence */
static void make_input(int notes, int n)
{
    unsigned r = 7;
    for (int i = 0; i < n; i++) {
        r = r * 1664525u + 1013904223u;
        const float t = (float)(i % 9000) / (float)SR, left = (float)(notes - i) / 4096.0f;
        const float g = left > 1.0f ? 1.0f : 0.5f - 0.5f * cosf(3.14159265f * left);
        in[i] = i < notes ? g * (0.25f * sinf((float)i * (0.031f + 0.004f * (float)(i / 9000 % 5))) * expf(-t * 7.0f)
                                     + 0.004f * ((float)(r >> 8) / 16777216.0f - 0.5f)) : 0.0f;
    }
}
/* a preset with nothing on, then the blocks asked for */
static px_preset_t bare(void)
{
    px_preset_t p;
    px_preset_factory(&p, 0);
    for (int b = 0; b < PX_NBLK; b++) p.on[b] = 0;
    px_order_default(p.order);
    return p;
}
static px_preset_t with_delay(px_preset_t p, int type, int time, int fb, int mix)
{
    p.on[PX_B_DELAY] = 1; p.type[PX_B_DELAY] = (uint8_t)type;
    p.p[PX_B_DELAY][0] = (uint8_t)time; p.p[PX_B_DELAY][1] = (uint8_t)fb; p.p[PX_B_DELAY][2] = (uint8_t)mix;
    p.p[PX_B_DELAY][3] = 50; p.p[PX_B_DELAY][4] = 50;
    return p;
}
static px_preset_t with_reverb(px_preset_t p, int type, int decay, int mix)
{
    p.on[PX_B_REVERB] = 1; p.type[PX_B_REVERB] = (uint8_t)type;
    p.p[PX_B_REVERB][0] = (uint8_t)decay; p.p[PX_B_REVERB][1] = 50; p.p[PX_B_REVERB][2] = (uint8_t)mix; p.p[PX_B_REVERB][3] = 50;
    return p;
}

/* the stretches of the delay memory: none overlaps another, free ticks are zero, the delay that plays reaches no
 * further than its line */
static int16_t owner[PX_DLY_N];
static int mark(int lo, int hi, int who)
{
    if (hi <= lo) return 1;
    if (lo < 0 || hi > PX_DLY_N) return 0;
    for (int i = lo; i < hi; i++) { if (owner[i]) return 0; owner[i] = (int16_t)who; }
    return 1;
}
static void invariants(const char *what)
{
    memset(owner, 0, sizeof owner);
    int ok = mark(E.dly.dly_lo, E.dly.dly_hi, 1);
    if (px_tr.own == &E) {
        if (px_tr.st[TR_DLY]) ok &= mark(px_tr.d.dly_lo, px_tr.d.dly_hi, 2);
        if (px_tr.st[TR_REV]) ok &= mark(px_tr.r_at, px_tr.r_at + PX_RV_M, 3);
        for (int i = 0; i < 2; i++) ok &= mark(px_tr.clr[i][0], px_tr.clr[i][1], 4);
    }
    CHECK(ok, "%s: stretches overlap or leave the memory", what);
    int dirty = 0;
    for (int i = 0; i < PX_DLY_N; i++) if (!owner[i] && E.dly_buf[i]) dirty++;
    CHECK(!dirty, "%s: %d free ticks are not zero", what, dirty);
    CHECK(E.dly.dly_hi - E.dly.dly_lo >= TR_MIN, "%s: the delay's line is %d ticks", what, E.dly.dly_hi - E.dly.dly_lo);
    CHECK(E.dly.dly_w >= E.dly.dly_lo && E.dly.dly_w <= E.dly.dly_hi, "%s: write index out of its line", what);
    if (E.run.on[PX_B_DELAY])
        CHECK(dly_reach(&E.dly) <= E.dly.dly_hi - E.dly.dly_lo, "%s: taps reach %d of %d ticks", what, dly_reach(&E.dly),
              E.dly.dly_hi - E.dly.dly_lo);
}

/* one run: preset a, at sample at a change to b made how (0 none, 1 px_engine_switch, 2 that and the trails stopped at
 * once, 3 px_engine_set), n samples; lanes: the wet lanes on, run the firmware's way */
enum { STAY, SWITCH, SWITCH_BARE, SET };
static void run(const px_preset_t *a, const px_preset_t *b, int at, int how, int lanes, float *out, int n)
{
    px_engine_init(&E, (float)SR);
    px_engine_set(&E, a);
    if (lanes) px_wet_mode(&E, 1, 256);
    for (int i = 0; i + 2 * NB <= n; i += 2 * NB) {
        if (i == at && how != STAY) {
            if (how == SET) px_engine_set(&E, b); else px_engine_switch(&E, b);
            if (how == SWITCH_BARE) px_trails_stop(&E, 1);
        }
        if (lanes) px_wet_swap(&E);
        px_engine_process(&E, in + i, out + i, NB);
        px_engine_process(&E, in + i + NB, out + i + NB, NB);
        if (lanes) px_wet_run(&E, 0, 0);
    }
}
static float peak(const float *y, int from, int to) { float m = 0.0f; for (int i = from; i < to; i++) if (fabsf(y[i]) > m) m = fabsf(y[i]); return m; }
static int same(const float *a, const float *b, int from, int to) { return !memcmp(a + from, b + from, (size_t)(to - from) * sizeof(float)); }
/* the trail in ya (changed) less yb (changed, no trails), its coming in over FADE frames undone, through the output's
 * DC filter from rest, against the tail in yc (not changed) less yb, `shift` samples earlier: the largest difference
 * over [from, to), and the tail's peak there */
static float tail_diff(int at, int from, int to, int shift, float *pk)
{
    float x1 = 0.0f, f1 = 0.0f, m = 0.0f;
    *pk = 0.0f;
    for (int i = at; i < to; i++) {
        const float g = i - at < FADE ? (float)(i - at + 1) / (float)FADE : 1.0f;
        const float x = (ya[i] - yb[i]) / (shift ? 1.0f : g), f = x - x1 + 0.9990f * f1;
        x1 = x; f1 = f;
        if (i < from) continue;
        const float t = yc[i - shift] - yb[i - shift], d = fabsf(f - t);
        if (d > m) m = d;
        if (fabsf(t) > *pk) *pk = fabsf(t);
    }
    return m;
}

/* the largest step from one sample to the next in a less b over [from, to) */
static float step(const float *a, const float *b, int from, int to)
{
    float m = 0.0f;
    for (int i = from; i < to; i++) { const float d = fabsf((a[i] - b[i]) - (a[i - 1] - b[i - 1])); if (d > m) m = d; }
    return m;
}

int main(void)
{
    const px_preset_t off = bare();
    const int N = 5 * SR / (2 * NB) * (2 * NB);
    int ntail = 0;

    /* 1. every delay type, the change at three places of the write index (well into the memory; just past its end, so
     * that the ticks kept lie across it; early, before the memory was ever full) */
    static const int AT[3] = {26496, 92672, 14080};
    for (int t = 0; t < PX_DLY_COUNT; t++) for (int k = 0; k < 3; k++) {
        const px_preset_t a = with_delay(off, t, 30, 45, 50);
        make_input(AT[k] - GAP, N);
        run(&a, &off, AT[k], SWITCH, 0, ya, N);
        run(&a, &off, AT[k], SWITCH_BARE, 0, yb, N);
        run(&a, &off, AT[k], STAY, 0, yc, N);
        float pk, d = tail_diff(AT[k], AT[k], AT[k] + 2 * SR, 0, &pk);
        CHECK(pk > 0.005f && pk < 0.7f, "delay %s at %d: tail peak %g", px_type_name(PX_B_DELAY, t), AT[k], pk);
        /* (the engine that stays goes on sending its chain's last 1e-7 into the delay, which now and then rounds a
         * tick of the 16-bit memory the other way: one step of it, 6e-5, at this MIX; LO-FI's own steps are 1 / 256) */
        CHECK(d < (t == PX_DLY_LOFI ? 5e-3f : 1e-4f), "delay %s at %d: trail differs from the tail by %g (peak %g)", px_type_name(PX_B_DELAY, t), AT[k], d, pk);
        CHECK(peak(yb, AT[k] + 8192, N) < 1e-4f, "delay %s: without its trail the tail should be gone (%g)", px_type_name(PX_B_DELAY, t), peak(yb, AT[k] + 8192, N));
        CHECK(step(ya, yb, AT[k], AT[k] + 1024) <= step(yc, yb, AT[k], AT[k] + 1024) * 1.2f + 1e-5f, "delay %s at %d: the trail comes in with a step of %g (the tail's own: %g)",
              px_type_name(PX_B_DELAY, t), AT[k], step(ya, yb, AT[k], AT[k] + 1024), step(yc, yb, AT[k], AT[k] + 1024));
        ntail++;
    }
    /* 2. every reverb type */
    for (int t = 0; t < PX_REV_COUNT; t++) {
        const px_preset_t a = with_reverb(off, t, 60, 40);
        const int at = 26496;
        make_input(at - GAP, N);
        run(&a, &off, at, SWITCH, 0, ya, N);
        run(&a, &off, at, SWITCH_BARE, 0, yb, N);
        run(&a, &off, at, STAY, 0, yc, N);
        float pk, d = tail_diff(at, at, at + 2 * SR, 0, &pk);
        if (t == PX_REV_SHIMMER) {
            CHECK(peak(ya, at, at + SR / 2) > 0.002f, "shimmer: no trail (%g)", peak(ya, at, at + SR / 2));
        } else if (t == PX_REV_GATED) {                /* (its gate has closed by now: what is left of it is the same) */
            CHECK(d < 1e-5f, "reverb GATED: trail differs from the tail by %g", d);
        } else {
            CHECK(pk > 0.002f && pk < 0.7f, "reverb %s: tail peak %g", px_type_name(PX_B_REVERB, t), pk);
            CHECK(d < pk * 1e-4f + 2e-6f, "reverb %s: trail differs from the tail by %g (peak %g)", px_type_name(PX_B_REVERB, t), d, pk);
        }
        CHECK(peak(yb, at + 8192, N) < 1e-4f, "reverb %s: without its trail the tail should be gone", px_type_name(PX_B_REVERB, t));
        ntail++;
    }

    /* 3. the new preset plays its own delay and reverb beside the trails (notes go on): the trails are what they are
     * when the new preset has neither */
    {
        const px_preset_t a = with_reverb(with_delay(off, PX_DLY_DIGITAL, 30, 45, 50), PX_REV_HALL, 60, 40);
        const px_preset_t b = with_reverb(with_delay(off, PX_DLY_ANALOG, 12, 30, 40), PX_REV_ROOM, 30, 30);
        const int at = 92672;
        make_input(N, N);
        run(&a, &b, at, SWITCH, 0, ya, N);
        run(&a, &b, at, SWITCH_BARE, 0, yb, N);
        for (int i = 0; i < N; i++) yc[i] = ya[i] - yb[i];              /* the trails, beside b's delay and reverb */
        run(&a, &off, at, SWITCH, 0, ya, N);
        run(&a, &off, at, SWITCH_BARE, 0, yb, N);
        float m = 0.0f, pk = 0.0f;
        for (int i = at; i < N; i++) {
            const float t = ya[i] - yb[i], d = fabsf(t - yc[i]);
            if (d > m) m = d;
            if (fabsf(t) > pk) pk = fabsf(t);
        }
        CHECK(pk > 0.01f, "delay and reverb trails: peak %g", pk);
        CHECK(m < 2e-6f, "the trails change by %g (peak %g) when the new preset has a delay and a reverb", m, pk);
    }

    /* 4. the wet lanes, the firmware's way: the same tail, one block (128 frames) later */
    for (int k = 0; k < 2; k++) {
        const px_preset_t a = k ? with_reverb(off, PX_REV_HALL, 60, 40) : with_delay(off, PX_DLY_DIGITAL, 30, 45, 50);
        const int at = 92672;
        make_input(at - GAP, N);
        run(&a, &off, at, SWITCH, 1, ya, N);
        run(&a, &off, at, SWITCH_BARE, 1, yb, N);
        run(&a, &off, at, STAY, 1, yc, N);
        /* (from 0.4 s on: the change empties the lanes, two blocks of the tail that the output's DC filter then lacks) */
        float pk, d = tail_diff(at, at + 16384, at + 2 * SR, 128, &pk);
        CHECK(pk > 0.005f, "lanes, %s: tail peak %g", k ? "reverb" : "delay", pk);
        CHECK(d < (k ? pk * 1e-4f : 1e-4f), "lanes, %s: trail differs from the tail 128 frames before by %g (peak %g)", k ? "reverb" : "delay", d, pk);
        CHECK(peak(ya, at, at + 256) == peak(yb, at, at + 256), "lanes, %s: a trail in the two blocks after the change", k ? "reverb" : "delay");
        CHECK(step(ya, yb, at, at + 1024) <= step(yc, yb, at, at + 1024) * 1.2f + 1e-5f, "lanes, %s: the trail comes in with a step of %g (the tail's own: %g)",
              k ? "reverb" : "delay", step(ya, yb, at, at + 1024), step(yc, yb, at, at + 1024));
    }

    /* 4b. a NAM preset the firmware's way on two cores (the lanes first, the blocks after the amp for the block before,
     * then the blocks before the amp and the network; the pipeline starts again from silence at a change): the trails
     * ring as loud as on one core with the lanes, and end */
    {
        px_preset_t a, b;
        px_preset_factory(&a, 13);                     /* SMOOTH LEAD: a tape echo and a hall after a NAM amp */
        px_preset_factory(&b, 18);                     /* TIGHT METAL: neither */
        const int at = 92672, n = 3 * SR / (2 * NB) * (2 * NB);
        double pw[2] = {0.0, 0.0};
        make_input(at - GAP, n);
        for (int pipe = 0; pipe < 2; pipe++) {
            for (int bare_ = 0; bare_ < 2; bare_++) {
                float *out = bare_ ? yb : ya;
                static float nb[2][2 * NB];
                int cur = 0, have = 0;
                memset(out, 0, (size_t)n * sizeof(float));
                px_engine_init(&E, (float)SR); px_engine_set(&E, &a);
                px_wet_mode(&E, 1, 256);
                for (int i = 0; i + 2 * NB <= n; i += 2 * NB) {
                    if (i == at) { px_engine_switch(&E, &b); if (bare_) px_trails_stop(&E, 1); have = 0; }
                    px_wet_swap(&E);
                    if (!pipe) {
                        for (int o = 0; o < 2 * NB; o += NB) px_engine_process(&E, in + i + o, out + i + o, NB);
                        px_wet_run(&E, 0, 0);
                        continue;
                    }
                    px_wet_run(&E, 0, 0);
                    if (have) for (int o = 0; o < 2 * NB; o += NB) px_engine_part(&E, nb[cur ^ 1] + o, out + i - 2 * NB + o, NB, PX_PART_POST);
                    for (int o = 0; o < 2 * NB; o += NB) px_engine_part(&E, in + i + o, nb[cur] + o, NB, PX_PART_PRE);
                    for (int o = 0; o < 2 * NB; o += NB) px_engine_part(&E, nb[cur] + o, nb[cur] + o, NB, PX_PART_NAM);
                    cur ^= 1; have = 1;
                }
                if (!bare_) { CHECK(px_trails(&E) == 3, "NAM preset, %s: trails %d", pipe ? "two cores" : "one core", px_trails(&E)); invariants("NAM preset"); }
            }
            for (int i = at + 1024; i < at + SR; i++) pw[pipe] += (double)(ya[i] - yb[i]) * (double)(ya[i] - yb[i]);
            CHECK(peak(ya, 0, n) < 1.5f && peak(ya, 0, n) == peak(ya, 0, n), "NAM preset, %s: output", pipe ? "two cores" : "one core");
        }
        CHECK(pw[0] > 1e-3 && pw[1] > pw[0] * 0.8 && pw[1] < pw[0] * 1.25, "NAM preset: the trails' power on two cores %g, on one %g", pw[1], pw[0]);
    }

    /* 5. they end */
    {
        const px_preset_t a = with_reverb(with_delay(off, PX_DLY_TAPE, 30, 45, 50), PX_REV_HALL, 60, 40);
        const int at = 26496, n = 12 * SR / (2 * NB) * (2 * NB);
        make_input(at - GAP, n);
        run(&a, &off, at, SWITCH, 0, ya, n);
        CHECK(px_trails(&E) == 0 && !tr_busy(), "the trails have not ended after 11 s of silence");
        CHECK(E.dly.dly_lo == 0 && E.dly.dly_hi == PX_DLY_N, "the delay has not its whole memory again: %d..%d", E.dly.dly_lo, E.dly.dly_hi);
        invariants("after the trails");
        run(&a, &off, at, SWITCH_BARE, 0, yb, n);
        CHECK(peak(ya, at, at + SR) > 0.01f, "no trail heard");
        int last = n;
        while (last > at && ya[last - 1] == yb[last - 1]) last--;
        CHECK(last < n - SR, "the output never becomes the engine's without trails");
        float step = 0.0f;                             /* the end itself: no step */
        for (int i = last - 2048; i < last + 64 && i < n; i++) { const float d = fabsf((ya[i] - yb[i]) - (ya[i - 1] - yb[i - 1])); if (d > step) step = d; }
        CHECK(step < 2e-4f, "a step of %g where the trails end", step);
        printf("trails of a tape echo and a hall end %.1f s after the change\n", (double)(last - at) / SR);

        /* a new preset whose delay plays: its line is a stretch beside the trails, then the whole memory again */
        {
            const px_preset_t b = with_delay(off, PX_DLY_DIGITAL, 10, 30, 40);
            make_input(n, n);
            px_engine_init(&E, (float)SR); px_engine_set(&E, &a);
            for (int i = 0; i < at; i += NB) px_engine_process(&E, in + i, yc + i, NB);
            px_engine_switch(&E, &b);
            CHECK(px_trails(&E) == 3 && E.dly.dly_hi - E.dly.dly_lo < PX_DLY_N - PX_RV_M, "a stretch beside two trails: %d ticks", E.dly.dly_hi - E.dly.dly_lo);
            for (int i = at; i < n; i += NB) px_engine_process(&E, in + i, yc + i, NB);
            CHECK(px_trails(&E) == 0 && E.dly.dly_lo == 0 && E.dly.dly_hi == PX_DLY_N, "the delay that plays has not grown back: %d..%d", E.dly.dly_lo, E.dly.dly_hi);
            invariants("grown back");
            make_input(at - GAP, n);
        }

        /* px_trails_stop: gone within 256 frames, through a fade */
        px_engine_init(&E, (float)SR); px_engine_set(&E, &a);
        for (int i = 0; i < at; i += NB) px_engine_process(&E, in + i, yc + i, NB);
        px_engine_switch(&E, &off);
        for (int i = at; i < at + 8 * NB; i += NB) px_engine_process(&E, in + i, yc + i, NB);
        CHECK(px_trails(&E) == 3, "trails: %d", px_trails(&E));
        px_trails_stop(&E, 0);
        for (int i = at + 8 * NB; i < at + 40 * NB; i += NB) px_engine_process(&E, in + i, yc + i, NB);
        CHECK(px_trails(&E) == 0, "px_trails_stop: still ringing");
        CHECK(same(yc, yb, at + 8 * NB + 256, at + 40 * NB), "px_trails_stop: sound after 256 frames");
        step = 0.0f;
        float before = 0.0f;
        for (int i = at + 4 * NB; i < at + 8 * NB; i++) { const float d = fabsf((yc[i] - yb[i]) - (yc[i - 1] - yb[i - 1])); if (d > before) before = d; }
        for (int i = at + 8 * NB; i < at + 14 * NB; i++) { const float d = fabsf((yc[i] - yb[i]) - (yc[i - 1] - yb[i - 1])); if (d > step) step = d; }
        CHECK(step <= before * 1.5f + 1e-5f, "px_trails_stop: a step of %g (the trail's own: %g)", step, before);

        /* a delay that feeds itself for ever: 20 s, then 2 s of fade */
        const px_preset_t osc = with_delay(off, PX_DLY_ANALOG, 20, 100, 60);
        const int n2 = 25 * SR / (2 * NB) * (2 * NB);
        make_input(at - GAP, n2);
        run(&osc, &off, at, SWITCH, 0, ya, n2);
        CHECK(px_trails(&E) == 0 && !tr_busy(), "an endless delay's trail has not ended");
        CHECK(peak(ya, at + 19 * SR, at + 20 * SR) > 0.02f, "an endless delay's trail: gone before 20 s (%g)", peak(ya, at + 19 * SR, at + 20 * SR));
        CHECK(peak(ya, at + 22 * SR + 4096, n2) < 1e-6f, "an endless delay's trail: still there after 22 s (%g)", peak(ya, at + 22 * SR + 4096, n2));
        CHECK(peak(ya, at, n2) < 1.5f, "an endless delay's trail: peak %g", peak(ya, at, n2));
    }

    /* 6. no room: two long delays; the delay keeps its memory under the new settings, as px_engine_set does it */
    {
        const px_preset_t a = with_delay(off, PX_DLY_DIGITAL, 90, 50, 50), b = with_delay(off, PX_DLY_DIGITAL, 88, 30, 40);
        const int at = 92672;
        make_input(at - GAP, N);
        run(&a, &b, at, SWITCH, 0, ya, N);
        run(&a, &b, at, SET, 0, yb, N);
        CHECK(same(ya, yb, 0, N), "two long delays: not px_engine_set's output");
        /* a long outgoing delay and a short new one: room for the reverb's trail, the delay keeps its memory */
        const px_preset_t c = with_reverb(with_delay(off, PX_DLY_DIGITAL, 84, 50, 50), PX_REV_HALL, 60, 40);
        const px_preset_t d = with_reverb(with_delay(off, PX_DLY_DIGITAL, 20, 30, 40), PX_REV_PLATE, 40, 30);
        px_engine_init(&E, (float)SR); px_engine_set(&E, &c);
        for (int i = 0; i < at; i += NB) px_engine_process(&E, in + i, yc + i, NB);
        px_engine_switch(&E, &d);
        CHECK(px_trails(&E) == 2, "long delay, short delay: trails %d (the reverb's only)", px_trails(&E));
        invariants("long delay, short delay");
        for (int i = at; i < N; i += NB) px_engine_process(&E, in + i, yc + i, NB);
        CHECK(peak(yc, at, N) < 1.5f && peak(yc, at, N) == peak(yc, at, N), "long delay, short delay: output");
        /* both fit when the new preset has no delay */
        const px_preset_t f = with_reverb(off, PX_REV_PLATE, 40, 30);
        px_engine_init(&E, (float)SR); px_engine_set(&E, &c);
        for (int i = 0; i < at; i += NB) px_engine_process(&E, in + i, yc + i, NB);
        px_engine_switch(&E, &f);
        CHECK(px_trails(&E) == 3, "long delay, no delay: trails %d (both)", px_trails(&E));
        invariants("long delay, no delay");
    }

    /* 7. the delay and the reverb as they are: px_engine_set's output; no echoes from a delay switched off earlier */
    {
        px_preset_t a, b;
        px_preset_factory(&a, 12); px_preset_factory(&b, 12);          /* BRIT LEAD: an analog delay and a hall */
        b.type[PX_B_MOD] = PX_MOD_PHASE90; b.on[PX_B_MOD] = 1; b.p[PX_B_EQ][2] = 70;
        const int at = 92672;
        make_input(N, N);
        run(&a, &b, at, SWITCH, 0, ya, N);
        CHECK(px_trails(&E) == 0, "same delay and reverb: trails %d", px_trails(&E));
        run(&a, &b, at, SET, 0, yb, N);
        CHECK(same(ya, yb, 0, N), "same delay and reverb: not px_engine_set's output");

        /* the delay stays (the same, or longer with no room for its trail) beside the reverb's trail: its line is cut
         * down to what its taps reach, wherever the write index is, and it sounds as px_engine_set leaves it */
        for (int k = 0; k < 4; k++) {
            const int time = k < 2 ? 30 : 60, at2 = k & 1 ? 92672 : 26496;
            const px_preset_t c = with_reverb(with_delay(off, PX_DLY_TAPE, time, 45, 50), PX_REV_HALL, 60, 40);
            const px_preset_t f = with_delay(off, PX_DLY_TAPE, k < 2 ? time : 62, 45, 50);   /* (no reverb: the delay alone is heard) */
            px_engine_init(&E, (float)SR); px_engine_set(&E, &c);
            for (int i = 0; i < at2; i += NB) px_engine_process(&E, in + i, yc + i, NB);
            px_engine_switch(&E, &f);
            CHECK(px_trails(&E) == 2 && E.dly.dly_hi - E.dly.dly_lo < PX_DLY_N - PX_RV_M, "delay kept (%d): trails %d, its line %d ticks", k, px_trails(&E),
                  E.dly.dly_hi - E.dly.dly_lo);
            invariants("delay kept");
            run(&c, &f, at2, SWITCH_BARE, 0, ya, N);
            run(&c, &f, at2, SET, 0, yb, N);
            CHECK(same(ya, yb, 0, N), "delay kept (%d): not px_engine_set's output", k);
        }

        const px_preset_t d = with_delay(off, PX_DLY_DIGITAL, 30, 60, 60);
        px_preset_t d0 = d;
        d0.on[PX_B_DELAY] = 0;
        make_input(SR / 2, N);
        px_engine_init(&E, (float)SR); px_engine_set(&E, &d);
        for (int i = 0; i < SR / 2 / NB * NB; i += NB) px_engine_process(&E, in + i, yc + i, NB);
        px_engine_knobs(&E, &d0);                      /* the delay off, its echoes left in its memory */
        for (int i = 0; i < 64; i++) px_engine_process(&E, in + N - NB, yc, NB);
        px_engine_switch(&E, &d);
        float m = 0.0f;
        for (int i = 0; i < 2 * SR / NB; i++) { px_engine_process(&E, in + N - NB, yc, NB); const float q = peak(yc, 0, NB); if (q > m) m = q; }
        CHECK(m < 1e-4f, "a delay switched off earlier plays its old echoes after a change (%g)", m);
    }

    /* 8. px_trail_due, and a delay time the line cannot hold */
    {
        const px_preset_t a = with_reverb(with_delay(off, PX_DLY_DIGITAL, 30, 45, 50), PX_REV_HALL, 60, 40);
        px_preset_t b = with_reverb(with_delay(off, PX_DLY_DIGITAL, 20, 45, 50), PX_REV_HALL, 60, 40);
        const int at = 26496;
        make_input(at - GAP, N);
        px_engine_init(&E, (float)SR); px_engine_set(&E, &a);
        CHECK(!px_trail_due(&E, &b), "a trail due with nothing sounding");
        for (int i = 0; i < at; i += NB) px_engine_process(&E, in + i, yc + i, NB);
        CHECK(px_engine_change(&E, &b) == PX_CH_KNOBS && px_trail_due(&E, &b), "another delay time: knobs, and a trail due");
        CHECK(!px_trail_due(&E, &a), "the same preset: no trail due");
        px_preset_t q = a; q.p[PX_B_EQ][1] ^= 3;
        CHECK(!px_trail_due(&E, &q), "another EQ setting: no trail due");
        q = a; q.on[PX_B_REVERB] = 0;
        CHECK(px_trail_due(&E, &q), "the reverb off: a trail due");
        px_engine_switch(&E, &b);
        CHECK(px_trails(&E) == 1, "another delay time: trails %d (the delay's)", px_trails(&E));
        CHECK(E.dly.dly_hi - E.dly.dly_lo < PX_DLY_N, "the delay's line is the whole memory beside a trail");
        q = b; q.p[PX_B_DELAY][0] = 24;
        CHECK(px_engine_change(&E, &q) == PX_CH_KNOBS, "a little more delay time: knobs");
        q = b; q.p[PX_B_DELAY][0] = 100;
        CHECK(px_engine_change(&E, &q) == PX_CH_FULL, "2 s of delay beside a trail: a full change");
        px_engine_switch(&E, &q);
        CHECK(dly_reach(&E.dly) <= E.dly.dly_hi - E.dly.dly_lo, "2 s of delay: its line holds %d ticks", E.dly.dly_hi - E.dly.dly_lo);
        invariants("2 s of delay");
        q.p[PX_B_DELAY][0] = 30; px_engine_switch(&E, &q);
        b = q; b.p[PX_B_DELAY][0] = 100;
        px_engine_knobs(&E, &b);                       /* (not asked of px_engine_change: the trails give way) */
        CHECK(dly_reach(&E.dly) <= E.dly.dly_hi - E.dly.dly_lo && px_trails(&E) == 0, "px_engine_knobs past the line");
        invariants("px_engine_knobs past the line");
    }

    /* 9. changes at random (the amp off: it has no part in this, and its network takes most of the time) */
    {
        unsigned r = 2024;
#define RND(n) ((int)((r = r * 1664525u + 1013904223u) >> 10) % (n))
        const int play = 18 * SR, n = (play + 24 * SR) / (2 * NB) * (2 * NB);
        long made = 0, rang = 0, checked = 0;
        for (int trial = 0; trial < 6; trial++) {
            const int lanes = trial & 1;
            px_preset_t p;
            px_preset_factory(&p, RND(24));
            p.on[PX_B_AMP] = 0;
            px_engine_init(&E, (float)SR); px_engine_set(&E, &p);
            if (lanes) px_wet_mode(&E, 1, 256);
            make_input(play, n);
            int next = 2 * NB * (4 + RND(60));
            for (int i = 0; i + 2 * NB <= n; i += 2 * NB) {
                if (i >= next && i < play) {
                    const int far = RND(4) == 0;
                    next = i + 2 * NB * (1 + RND(far ? 700 : 90));
                    if (RND(3)) {                       /* another preset, sometimes with its delay and reverb at random */
                        px_preset_factory(&p, RND(24));
                        p.on[PX_B_AMP] = 0;
                        if (RND(2)) {
                            p.on[PX_B_DELAY] = (uint8_t)(RND(4) != 0); p.type[PX_B_DELAY] = (uint8_t)RND(PX_DLY_COUNT);
                            p.on[PX_B_REVERB] = (uint8_t)(RND(4) != 0); p.type[PX_B_REVERB] = (uint8_t)RND(PX_REV_COUNT);
                            for (int k = 0; k < PX_NPAR; k++) { p.p[PX_B_DELAY][k] = (uint8_t)RND(101); p.p[PX_B_REVERB][k] = (uint8_t)RND(101); }
                            if (RND(3)) p.p[PX_B_DELAY][0] = (uint8_t)RND(50);
                        }
                        px_engine_switch(&E, &p);
                        made++;
                        if (px_trails(&E)) rang++;
                    } else {                            /* a knob or a block on / off, the firmware's way */
                        const int b = RND(3) ? (RND(2) ? PX_B_DELAY : PX_B_REVERB) : RND(PX_NBLK), k = RND(PX_NPAR), v = RND(101);
                        if (b == PX_B_AMP) continue;
                        if (RND(4)) p.p[b][k] = (uint8_t)v; else p.on[b] ^= 1;
                        const int ch = px_engine_change(&E, &p);
                        if (ch == PX_CH_KNOBS) { px_engine_prepare(&E, &p); px_engine_knobs(&E, &p); }
                        else if (ch == PX_CH_FULL) px_engine_switch(&E, &p);
                    }
                    invariants("after a change");
                    checked++;
                }
                if (lanes) px_wet_swap(&E);
                px_engine_process(&E, in + i, ya + i, NB);
                px_engine_process(&E, in + i + NB, ya + i + NB, NB);
                if (lanes) px_wet_run(&E, 0, 0);
                if (i / (2 * NB) % 97 == 0) { invariants("while playing"); checked++; }
            }
            int bad = 0;
            for (int i = 0; i < n; i++) if (!(fabsf(ya[i]) < 4.0f)) bad++;
            CHECK(!bad, "random changes %d: %d samples NaN or runaway", trial, bad);
            CHECK(px_trails(&E) == 0 && !tr_busy(), "random changes %d: trails %d after 24 s", trial, px_trails(&E));
            invariants("at the end");
        }
        printf("random: %ld changes of preset (%ld left trails), the delay memory checked %ld times\n", made, rang, checked);
    }

    if (fails) { printf("trails: %d FAILED\n", fails); return 1; }
    printf("trails: OK (%d tails sample for sample)\n", ntail);
    return 0;
}
