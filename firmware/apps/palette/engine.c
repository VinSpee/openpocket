/* SPDX-License-Identifier: GPL-3.0-only
 * PALETTE's engine: see engine.h and docs/PALETTE.md. The per-sample code has one loop an effect (no branch on the
 * effect inside a loop), reads its numbers into locals, and keeps the long lines in 16-bit memory. Every function a
 * block calls is in app.sh's list of RAM code. */
#include "engine.h"
#include <math.h>
#include <string.h>

#define I16 (1.0f / 32768.0f)
#define AINL static inline __attribute__((always_inline))
#define XF (1.0f / 256.0f)                      /* a blend's step a sample: 5.8 ms from end to end, as the base's change fade */
/* Denormals: the pedal's FPU slows on them (docs/HARDWARE-HANDBOOK.md), and digital silence through a filter that
 * feeds back decays into them. Three rules keep every number the engine keeps out of them (tests/test_palette.c):
 * a module's input gets +-1e-15 (pl_run); a filter fed from 16-bit memory, which holds exact zeros, gets DN at each
 * step; a glide that ends at zero ends there exactly. */
#define DN 1e-18f
AINL float f_abs(float x) { return x < 0.0f ? -x : x; }
AINL float f_sat(float x) { return x / (1.0f + f_abs(x)); }                  /* a soft clip: one division, never beyond 1 */
AINL int16_t f_q(float x) { x *= 32767.0f; if (x > 32767.0f) x = 32767.0f; if (x < -32767.0f) x = -32767.0f; return (int16_t)x; }
AINL float f_noise(uint32_t *r) { *r = *r * 1664525u + 1013904223u; return (float)(int32_t)*r * (1.0f / 2147483648.0f); }
AINL float f_psin(float ph) { const float x = 2.0f * ph - 1.0f; return 4.0f * x * (1.0f - f_abs(x)); }   /* ph 0..1: a sine's shape */
AINL float f_wrap(float ph) { return ph >= 1.0f ? ph - 1.0f : ph; }
AINL float f_step(float v, float tgt, float st) { const float d = tgt - v; return d > st ? v + st : d < -st ? v - st : tgt; }   /* v towards tgt, st at most */
/* the output's ceiling: as it is up to half of full scale (the loudest the guitar itself is), then bending towards 1 */
AINL float f_ceil(float v)
{
    float m = f_abs(v);
    if (m > 0.5f) { const float t = (m - 0.5f) * 2.0f; m = 0.5f + 0.5f * t / (1.0f + t); v = v < 0.0f ? -m : m; }
    return v;
}
/* half-band all-pass coefficients: the six of docs/HARDWARE-HANDBOOK.md section 8.4, split between the two branches */
static const float HB_A[3] = { 0.062889286f, 0.424695820f, 0.782113520f }, HB_B[3] = { 0.224015205f, 0.616864442f, 0.927573355f };
AINL float f_ap(pl_ap3_t *a, const float *c, float x)
{
    float y = c[0] * (x - a->s[1]) + a->s[0]; a->s[0] = x; a->s[1] = y; x = y;
    y = c[1] * (x - a->s[3]) + a->s[2]; a->s[2] = x; a->s[3] = y; x = y;
    y = c[2] * (x - a->s[5]) + a->s[4]; a->s[4] = x; a->s[5] = y; return y;
}
AINL float drift_step(pl_drift_t *d, float k)   /* once a block: a slow random value in -1..1 */
{
    if (--d->cnt <= 0) { d->tgt = f_noise(&d->rnd); d->cnt = 6 + (int32_t)(d->rnd >> 28); }
    d->v += k * (d->tgt - d->v); return d->v;
}
/* a 16-bit line read between two samples: d samples back from w, in a ring of mask + 1 */
AINL float f_rd(const int16_t *b, int32_t w, int32_t mask, float d)
{
    const int32_t i0 = (int32_t)d; const float f = d - (float)i0, a = (float)b[(w - i0) & mask], c = (float)b[(w - i0 - 1) & mask];
    return (a + f * (c - a)) * I16;
}
/* the same in a ring whose length is not a power of two (d < len) */
AINL float f_rdn(const int16_t *b, int32_t w, int32_t len, float d)
{
    const int32_t i0 = (int32_t)d; const float f = d - (float)i0; int32_t j = w - i0; if (j < 0) j += len;
    int32_t j1 = j - 1; if (j1 < 0) j1 += len;
    const float a = (float)b[j], c = (float)b[j1]; return (a + f * (c - a)) * I16;
}

/* ============================================================ Character === */
static void fx_drive(pl_engine_t *e, float *l, float *r)
{
    const pl_cc_t *k = &e->ch; pl_cs_t *s = &e->cs;
    const float g = k->g, khp = k->khp, kt = k->kt, gl = k->gl, gh = k->gh, bias = k->bias, lvl = k->lvl;
    for (int c = 0; c < 2; c++) {
        float *x = c ? r : l; pl_os2_t *o = &s->os[c]; float hp = s->hp[c], dc = s->dc[c], lo = s->lo[c];
        for (uint32_t i = 0; i < PL_FR; i++) {
            float v = x[i]; hp += khp * (v - hp); v = (v - hp) * g;
            const float u0 = f_sat(f_ap(&o->ua, HB_A, v) + bias), u1 = f_sat(f_ap(&o->ub, HB_B, v) + bias);
            float y = 0.5f * (f_ap(&o->da, HB_A, u0) + f_ap(&o->db, HB_B, o->podd)); o->podd = u1;
            dc += 0.0007f * (y - dc); y -= dc;
            lo += kt * (y - lo) + DN; x[i] = (lo * gl + (y - lo) * gh) * lvl;
        }
        s->hp[c] = hp; s->dc[c] = dc; s->lo[c] = lo;
    }
}
static void fx_sweeten(pl_engine_t *e, float *l, float *r)
{
    const pl_cc_t *k = &e->ch; pl_cs_t *s = &e->cs;
    const float katt = k->katt, krel = k->krel, kc = k->kc, make = k->make, sat = k->sat, kt = k->kt, khp = k->khp,
                gl = k->gl, gh = k->gh, lvl = k->lvl;
    float env = s->env, lo0 = s->lo[0], lo1 = s->lo[1], h0 = s->lo2[0], h1 = s->lo2[1];
    for (uint32_t i = 0; i < PL_FR; i++) {
        float a = f_abs(l[i]); const float b = f_abs(r[i]); a = a > b ? a : b;
        env += (a > env ? katt : krel) * (a - env);
        const float gn = make / (1.0f + kc * env);
        float x = l[i]; lo0 += kt * (x - lo0); h0 += khp * (x - h0); x = (x + gl * lo0 + gh * (x - h0)) * gn;
        l[i] = x / (1.0f + sat * f_abs(x)) * lvl;
        x = r[i]; lo1 += kt * (x - lo1); h1 += khp * (x - h1); x = (x + gl * lo1 + gh * (x - h1)) * gn;
        r[i] = x / (1.0f + sat * f_abs(x)) * lvl;
    }
    s->env = env; s->lo[0] = lo0; s->lo[1] = lo1; s->lo2[0] = h0; s->lo2[1] = h1;
}
AINL void fuzz_core(pl_engine_t *e, float *l, float *r, const int howl)
{
    const pl_cc_t *k = &e->ch; pl_cs_t *s = &e->cs;
    const float g = k->g, g2 = k->g2, khp = k->khp, kt = k->kt, bias = k->bias, bk = k->bk, kb = k->kb, lvl = k->lvl,
                a1 = k->a1, a2 = k->a2, a3 = k->a3, sq = k->sq;
    for (int c = 0; c < 2; c++) {
        float *x = c ? r : l; pl_os2_t *o = &s->os[c];
        float hp = s->hp[c], dc = s->dc[c], lo = s->lo[c], be = s->be[c], ic1 = s->ic1[c], ic2 = s->ic2[c], dn = 1e-15f;
        for (uint32_t i = 0; i < PL_FR; i++) {
            float v = x[i]; hp += khp * (v - hp); v = (v - hp) * g;
            be += kb * (f_abs(v) - be);
            const float b = bias - bk * be;                         /* the bias moves with the level: sputter, gating */
            const float u0 = f_sat(f_sat(f_ap(&o->ua, HB_A, v) + b) * g2), u1 = f_sat(f_sat(f_ap(&o->ub, HB_B, v) + b) * g2);
            float y = 0.5f * (f_ap(&o->da, HB_A, u0) + f_ap(&o->db, HB_B, o->podd)); o->podd = u1;
            dc += 0.0007f * (y - dc); y -= dc;
            lo += kt * (y - lo) + DN; y = lo;
            if (howl) {                                             /* a resonant state-variable filter after the fuzz */
                const float v3 = y + dn - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
                ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2; y = v2 + sq * v1; dn = -dn;
            }
            x[i] = y * lvl;
        }
        s->hp[c] = hp; s->dc[c] = dc; s->lo[c] = lo; s->be[c] = be; s->ic1[c] = ic1; s->ic2[c] = ic2;
    }
}
static void fx_fuzz(pl_engine_t *e, float *l, float *r) { fuzz_core(e, l, r, 0); }
static void fx_howl(pl_engine_t *e, float *l, float *r) { fuzz_core(e, l, r, 1); }
static void fx_swell(pl_engine_t *e, float *l, float *r)
{
    const pl_cc_t *k = &e->ch; pl_cs_t *s = &e->cs;
    const float katt = k->katt, krel = k->krel, thr = k->thr, ratio = k->ratio, kup = k->kup, kdn = k->kdn;
    float env = s->env, slow = s->slow, gate = s->gate; int32_t arm = s->arm;
    for (uint32_t i = 0; i < PL_FR; i++) {
        float a = f_abs(l[i]); const float b = f_abs(r[i]); a = a > b ? a : b;
        env += (a > env ? katt : krel) * (a - env); slow += 0.0004f * (env - slow);
        if (arm && env > thr && env > ratio * slow) { gate = 0.0f; arm = 0; }   /* a new note: start from silence */
        if (env < 1.25f * slow) arm = 1;                            /* its attack is over: the next one may trigger */
        const float tgt = env > thr ? 1.0f : 1e-9f;                 /* (closed: -180 dB, and never a denormal) */
        gate += (tgt > gate ? kup : kdn) * (tgt - gate);
        l[i] *= gate; r[i] *= gate;
    }
    s->env = env; s->slow = slow; s->gate = gate; s->arm = arm;
}

/* ============================================================= Movement === */
static void fx_doubler(pl_engine_t *e, float *l, float *r)
{
    const pl_mc_t *k = &e->mv; pl_ms_t *s = &e->ms;
    int16_t *b = e->mvb; int32_t w = s->w; const int32_t M = PL_MV_N - 1; const float amt = k->amt, lvl = k->lvl;
    const float d0 = drift_step(&s->dft[0], 0.08f), d1 = drift_step(&s->dft[1], 0.08f);
    const float tl = k->dlt * (1.0f + k->dfk * d0), tr = k->drt * (1.0f + k->dfk * d1);
    float dl = s->dl, dr = s->dr;                                   /* a tap goes a tenth of its way a block: a short pitch bend */
    const float il = 0.1f * (tl - dl) * (1.0f / (float)PL_FR), ir = 0.1f * (tr - dr) * (1.0f / (float)PL_FR);
    for (uint32_t i = 0; i < PL_FR; i++) {
        b[w] = f_q(0.5f * (l[i] + r[i]));
        l[i] = (l[i] + amt * f_rd(b, w, M, dl)) * lvl; r[i] = (r[i] + amt * f_rd(b, w, M, dr)) * lvl;
        dl += il; dr += ir; w = (w + 1) & M;
    }
    s->w = w; s->dl = dl; s->dr = dr;
}
static void fx_vibrato(pl_engine_t *e, float *l, float *r)
{
    const pl_mc_t *k = &e->mv; pl_ms_t *s = &e->ms;
    int16_t *bl = e->mvb, *br = e->mvb + PL_MV_N / 2; const int32_t M = PL_MV_N / 2 - 1; int32_t w = s->w & M;
    const float inc = k->inc, off = k->off;
    const float t0 = k->dfk * drift_step(&s->dft[0], 0.2f), t1 = k->dfk * drift_step(&s->dft[1], 0.2f);
    float r0 = s->r0, r1 = s->r1, ph = s->ph, depth = s->dl;        /* the depth glides: a jump of it is a jump in the line */
    const float i0 = (t0 - r0) * (1.0f / (float)PL_FR), i1 = (t1 - r1) * (1.0f / (float)PL_FR);
    const float dp1 = f_abs(k->depth - depth) < 0.01f ? k->depth : depth + 0.1f * (k->depth - depth), ddp = (dp1 - depth) * (1.0f / (float)PL_FR);
    for (uint32_t i = 0; i < PL_FR; i++) {
        bl[w] = f_q(l[i]); br[w] = f_q(r[i]);
        depth += ddp;
        const float base = 2.2f * depth + 4.0f;
        l[i] = f_rd(bl, w, M, base + depth * (f_psin(ph) + r0));
        r[i] = f_rd(br, w, M, base + depth * (f_psin(f_wrap(ph + off)) + r1));
        ph = f_wrap(ph + inc); r0 += i0; r1 += i1; w = (w + 1) & M;
    }
    s->w = w; s->ph = ph; s->r0 = r0; s->r1 = r1; s->dl = dp1;
}
static void fx_phaser(pl_engine_t *e, float *l, float *r)
{
    const pl_mc_t *k = &e->mv; pl_ms_t *s = &e->ms;
    for (int j = 0; j < 6; j++) s->w6[j] += 0.0625f * (k->w6[j] - s->w6[j]);    /* the stages cross over in a few blocks */
    s->fb += 0.0625f * (k->fb - s->fb);
    const float fb = s->fb, w0 = s->w6[0], w1 = s->w6[1], w2 = s->w6[2], w3 = s->w6[3], w4 = s->w6[4], w5 = s->w6[5], hv = 0.5f * k->lvl;
    const float ph = f_wrap(s->ph + k->inc * (float)PL_FR);
    float lf = 0.5f + 0.5f * f_psin(ph) + k->dfk * drift_step(&s->dft[0], 0.2f);
    lf = lf < 0.0f ? 0.0f : lf > 1.0f ? 1.0f : lf;
    const float a_end = k->a0 + (k->a1 - k->a0) * lf, a_beg = s->base, da = (a_end - a_beg) * (1.0f / (float)PL_FR);
    s->ph = ph; s->base = a_end;
    for (int c = 0; c < 2; c++) {
        float *x = c ? r : l, *z = s->ap[c], last = s->last[c], a = a_beg;
        float z0 = z[0], z1 = z[1], z2 = z[2], z3 = z[3], z4 = z[4], z5 = z[5], z6 = z[6], z7 = z[7], z8 = z[8], z9 = z[9], z10 = z[10], z11 = z[11];
        for (uint32_t i = 0; i < PL_FR; i++) {                      /* twelve stages; the taps after 2, 4 .. 12 crossfaded */
            float v = x[i] + fb * last, y, acc;
            y = z0 - a * v; z0 = v + a * y; v = y; y = z1 - a * v; z1 = v + a * y; v = y; acc = w0 * v;
            y = z2 - a * v; z2 = v + a * y; v = y; y = z3 - a * v; z3 = v + a * y; v = y; acc += w1 * v;
            y = z4 - a * v; z4 = v + a * y; v = y; y = z5 - a * v; z5 = v + a * y; v = y; acc += w2 * v;
            y = z6 - a * v; z6 = v + a * y; v = y; y = z7 - a * v; z7 = v + a * y; v = y; acc += w3 * v;
            y = z8 - a * v; z8 = v + a * y; v = y; y = z9 - a * v; z9 = v + a * y; v = y; acc += w4 * v;
            y = z10 - a * v; z10 = v + a * y; v = y; y = z11 - a * v; z11 = v + a * y; v = y; acc += w5 * v;
            last = acc; x[i] = hv * (x[i] + acc); a += da;
        }
        z[0] = z0; z[1] = z1; z[2] = z2; z[3] = z3; z[4] = z4; z[5] = z5; z[6] = z6; z[7] = z7; z[8] = z8; z[9] = z9; z[10] = z10; z[11] = z11;
        s->last[c] = last;
    }
}
static void fx_tremolo(pl_engine_t *e, float *l, float *r)
{
    const pl_mc_t *k = &e->mv; pl_ms_t *s = &e->ms;
    const float hard = k->hard, off = k->off, hn = 1.0f + hard, lvl = k->lvl;
    float depth = k->depth * (1.0f + k->dfk * drift_step(&s->dft[0], 0.2f));
    const float inc = k->inc * (1.0f + k->dfk * drift_step(&s->dft[1], 0.2f));
    float ph = s->ph;
    if (depth > 1.0f) depth = 1.0f;
    for (uint32_t i = 0; i < PL_FR; i++) {
        float s1 = f_psin(ph), s2 = f_psin(f_wrap(ph + off));
        s1 = s1 * hn / (1.0f + hard * f_abs(s1)); s2 = s2 * hn / (1.0f + hard * f_abs(s2));   /* a sine towards a square */
        l[i] *= (1.0f - depth * (0.5f - 0.5f * s1)) * lvl; r[i] *= (1.0f - depth * (0.5f - 0.5f * s2)) * lvl;
        ph = f_wrap(ph + inc);
    }
    s->ph = ph;
}
AINL void pitch_core(pl_engine_t *e, float *l, float *r, const int lofi)
{
    const pl_mc_t *k = &e->mv; pl_ms_t *s = &e->ms;
    int16_t *bl = e->mvb, *br = e->mvb + PL_MV_N / 2; const int32_t M = PL_MV_N / 2 - 1; int32_t w = s->w & M;
    const float W = k->W, a = k->amt;
    /* the ratio glides to where RATE put it; at RATE's centre the two heads give way to the signal as it is (rest) */
    const float ptgt = k->pinc * (1.0f + k->dfk * drift_step(&s->dft[0], 0.2f));
    float pinc = s->r0 + 0.08f * (ptgt - s->r0);
    if (f_abs(ptgt - pinc) < 1e-9f) pinc = ptgt;
    float u = s->rest; const float u1 = f_step(u, k->rest, 0.25f), du = (u1 - u) * (1.0f / (float)PL_FR);
    /* AMOUNT's two gains: the shifted sound does not add up with the guitar, so they keep the loudness (dp, wp); at rest
     * the two are the same signal, and they add up to 1 (1 - a, a) */
    const float dp = 1.0f - a * a, wp = a * (2.0f - a) * 1.3f;
    float dry = dp + u * (1.0f - a - dp), amt = wp + u * (a - wp);
    const float ddry = (dp + u1 * (1.0f - a - dp) - dry) * (1.0f / (float)PL_FR), damt = (wp + u1 * (a - wp) - amt) * (1.0f / (float)PL_FR);
    float ph = s->pph, hl = s->held[0], hr = s->held[1]; int32_t hc = s->hc; const int32_t hold = k->hold;
    for (uint32_t i = 0; i < PL_FR; i++) {
        bl[w] = f_q(l[i]); br[w] = f_q(r[i]);
        const float p1 = f_wrap(ph + 0.5f), g0 = 1.0f - f_abs(2.0f * ph - 1.0f), g1 = 1.0f - g0;   /* two heads, crossfaded */
        const float da = ph * W + 2.0f, db = p1 * W + 2.0f;
        float yl = g0 * f_rd(bl, w, M, da) + g1 * f_rd(bl, w, M, db), yr = g0 * f_rd(br, w, M, da) + g1 * f_rd(br, w, M, db);
        yl += u * (l[i] - yl); yr += u * (r[i] - yr); u += du;
        if (lofi) { if (++hc >= hold) { hc = 0; hl = yl; hr = yr; } yl = hl; yr = hr; }   /* the shifter at a lower rate */
        dry += ddry; amt += damt;
        l[i] = l[i] * dry + yl * amt; r[i] = r[i] * dry + yr * amt;
        ph += pinc; if (ph >= 1.0f) ph -= 1.0f; if (ph < 0.0f) ph += 1.0f;
        w = (w + 1) & M;
    }
    s->w = w; s->pph = ph; s->held[0] = hl; s->held[1] = hr; s->hc = hc; s->r0 = pinc; s->rest = u1;
}
static void fx_pitch(pl_engine_t *e, float *l, float *r) { if (e->mv.hold > 1) pitch_core(e, l, r, 1); else pitch_core(e, l, r, 0); }

/* ============================================================ Diffusion === */
/* All of these tick at half rate (22.05 kHz) in 16-bit memory, as OpenPocket's delays and reverbs do: the two inputs of
 * a pair are averaged in, and the output is brought back up by a straight line between two ticks. */
AINL void tape_core(pl_engine_t *e, float *l, float *r, const int reels)
{
    const pl_dc_t *k = &e->df; pl_ds_t *s = &e->ds;
    int16_t *b = e->dfb; int32_t w = s->w; const int32_t N = PL_DF_N;
    const float dt = k->dt, ks = k->ks, fb = k->fb, klp = k->klp, khp = k->khp, wet = k->wet, off = k->off, inc1 = k->inc1,
                inc2 = k->inc2, nz = k->nz;
    const float dv = drift_step(&s->dft[0], 0.1f), wow = k->wow * (1.0f + 0.5f * dv), flut = k->flut;
    const float drop = reels ? 1.0f - k->drop * f_abs(drift_step(&s->dft[1], 0.3f)) : 1.0f;
    float d = s->d, lp = s->lp, hp = s->hp, yl = s->yl, yr = s->yr, ph1 = s->ph1, ph2 = s->ph2; uint32_t rnd = e->rnd;
    for (uint32_t i = 0; i + 1u < PL_FR; i += 2u) {
        const float xin = 0.25f * (l[i] + r[i] + l[i + 1u] + r[i + 1u]);
        d += ks * (dt - d);                                         /* the time glides: turning TIME bends the pitch */
        const float dd = d + wow * f_psin(ph1) + flut * f_psin(ph2);
        ph1 = f_wrap(ph1 + inc1); ph2 = f_wrap(ph2 + inc2);
        const float t1 = f_rdn(b, w, N, dd), t2 = f_rdn(b, w, N, dd + off);
        lp += klp * (t1 - lp) + DN; hp += khp * (lp - hp);
        float f = (lp - hp) * fb;
        if (reels) f = (f + nz * f_noise(&rnd)) * drop;
        b[w] = f_q(xin + f_sat(f)); if (++w >= N) w = 0;
        l[i] += wet * 0.5f * (yl + t1); l[i + 1u] += wet * t1;
        r[i] += wet * 0.5f * (yr + t2); r[i + 1u] += wet * t2;
        yl = t1; yr = t2;
    }
    s->w = w; s->d = d; s->lp = lp; s->hp = hp; s->yl = yl; s->yr = yr; s->ph1 = ph1; s->ph2 = ph2; e->rnd = rnd;
}
static void fx_cascade(pl_engine_t *e, float *l, float *r) { tape_core(e, l, r, 0); }
static void fx_reels(pl_engine_t *e, float *l, float *r) { tape_core(e, l, r, 1); }
static void fx_space(pl_engine_t *e, float *l, float *r)
{
    const pl_dc_t *k = &e->df; pl_ds_t *s = &e->ds;
    int16_t *b = e->dfb; const int32_t M = 32767; int32_t p = s->p, sk = s->k; const int32_t *B = s->b; int32_t *L = s->n;
    const float kd = k->kd, g = k->g, dg = k->dg, wet = k->wet, dryg = k->dryg, md = k->md, inc = k->sinc;
    float st[8], yl = s->yl, yr = s->yr, ph = s->sph;
    for (int j = 0; j < 8; j++) st[j] = s->st[j];
    for (uint32_t i = 0; i + 1u < PL_FR; i += 2u) {
        float x = 0.0625f * (l[i] + r[i] + l[i + 1u] + r[i + 1u]), o[8], sum = 0.0f;    /* (a quarter: the lines ring up to 5 times their input) */
        {   /* TIME moved: one unit a tick goes one sample towards its new length (no step, a short bend) */
            const int32_t t = k->n[sk]; if (L[sk] < t) L[sk]++; else if (L[sk] > t) L[sk]--;
            if (++sk >= 12) sk = 0;
        }
        for (int j = 0; j < 4; j++) {                               /* four input diffusers */
            const float dl = (float)b[(p + B[j] + L[j]) & M] * I16, wv = x - dg * dl;
            b[(p + B[j]) & M] = f_q(wv); x = dl + dg * wv;
        }
        const float m0 = md * (1.0f + f_psin(ph)), m1 = md * (1.0f + f_psin(f_wrap(ph + 0.25f))); ph = f_wrap(ph + inc);
        for (int j = 0; j < 8; j++) {                               /* eight lines, each with its own damping */
            float v;
            if (j == 0 || j == 4) {                                 /* two of them are read at a moving point (DRIFT) */
                const float mo = j ? m1 : m0; const int32_t i0 = (int32_t)mo; const float f = mo - (float)i0;
                const int32_t q = p + B[4 + j] + L[4 + j] - 8 + i0;
                const float a = (float)b[q & M], c = (float)b[(q + 1) & M]; v = (a + f * (c - a)) * I16;
            } else v = (float)b[(p + B[4 + j] + L[4 + j]) & M] * I16;
            st[j] += kd * (v - st[j]) + DN; o[j] = st[j]; sum += o[j];
        }
        sum *= 0.25f;                                               /* Householder feedback: each line minus 2/8 of the sum */
        for (int j = 0; j < 8; j++) b[(p + B[4 + j]) & M] = f_q(x + (o[j] - sum) * g);
        const float ol = 0.5f * (o[0] - o[2] + o[4] - o[6]), orr = 0.5f * (o[1] - o[3] + o[5] - o[7]);
        p = (p - 1) & M;
        l[i] = l[i] * dryg + wet * 0.5f * (yl + ol); l[i + 1u] = l[i + 1u] * dryg + wet * ol;
        r[i] = r[i] * dryg + wet * 0.5f * (yr + orr); r[i + 1u] = r[i + 1u] * dryg + wet * orr;
        yl = ol; yr = orr;
    }
    for (int j = 0; j < 8; j++) s->st[j] = st[j];
    s->p = p; s->k = sk; s->yl = yl; s->yr = yr; s->sph = ph;
}
static void fx_collage(pl_engine_t *e, float *l, float *r)
{
    const pl_dc_t *k = &e->df; pl_ds_t *s = &e->ds;
    int16_t *b = e->dfb; const int32_t len = (int32_t)k->len; const float fl = (float)len, fb = k->fb, wet = k->wet;
    const float dv = drift_step(&s->dft[0], 1.0f);                  /* DRIFT: now and then a pass at double speed */
    const float spdt = k->dfk > 0.0f && dv > 1.0f - k->dfk ? 2.0f : k->spdt;
    float rp = s->rp, spd = s->spd, yl = s->yl, yr = s->yr; int32_t wp = s->wp;
    if (wp >= len) wp = 0;                                          /* TIME moved: the loop is cut where it stands */
    if (rp >= fl) rp = 0.0f;
    for (uint32_t i = 0; i + 1u < PL_FR; i += 2u) {
        const float xin = 0.25f * (l[i] + r[i] + l[i + 1u] + r[i + 1u]);
        spd += 0.002f * (spdt - spd);
        int32_t i0 = (int32_t)rp, i1 = i0 + 1; if (i1 >= len) i1 = 0;
        float f = rp - (float)i0; const float t1 = ((float)b[i0] + f * ((float)b[i1] - (float)b[i0])) * I16;
        float rq = rp + 0.5f * fl; if (rq >= fl) rq -= fl;
        i0 = (int32_t)rq; i1 = i0 + 1; if (i1 >= len) i1 = 0; f = rq - (float)i0;
        const float t2 = ((float)b[i0] + f * ((float)b[i1] - (float)b[i0])) * I16;
        b[wp] = f_q(xin + f_sat((float)b[wp] * I16 * fb)); if (++wp >= len) wp = 0;   /* overdubbed in place */
        rp += spd; if (rp >= fl) rp -= fl;
        l[i] += wet * 0.5f * (yl + t1); l[i + 1u] += wet * t1;
        r[i] += wet * 0.5f * (yr + t2); r[i + 1u] += wet * t2;
        yl = t1; yr = t2;
    }
    s->rp = rp; s->spd = spd; s->wp = wp; s->yl = yl; s->yr = yr;
}
static void fx_reverse(pl_engine_t *e, float *l, float *r)
{
    const pl_dc_t *k = &e->df; pl_ds_t *s = &e->ds;
    int16_t *b = e->dfb; int32_t w = s->w; const int32_t N = PL_DF_N;
    const float fb = k->fb, wet = k->wet, dryg = k->dryg, pinc = k->pinc;
    const float step = k->step * (1.0f + k->dfk * drift_step(&s->dft[0], 0.2f));
    float d0 = s->rd[0], d1 = s->rd[1], p0 = s->rph[0], p1 = s->rph[1], yl = s->yl, yr = s->yr;
    for (uint32_t i = 0; i + 1u < PL_FR; i += 2u) {
        const float xin = 0.25f * (l[i] + r[i] + l[i + 1u] + r[i + 1u]);
        const float a = f_rdn(b, w, N, d0), c = f_rdn(b, w, N, d1);    /* two heads running backwards */
        const float g0 = 1.0f - f_abs(2.0f * p0 - 1.0f), g1 = 1.0f - f_abs(2.0f * p1 - 1.0f);
        const float ol = g0 * a + 0.5f * g1 * c, orr = 0.5f * g0 * a + g1 * c;
        b[w] = f_q(xin + fb * 0.5f * (ol + orr)); if (++w >= N) w = 0;
        d0 += step; d1 += step; p0 += pinc; p1 += pinc;
        if (p0 >= 1.0f) { p0 -= 1.0f; d0 = 2.0f; }
        if (p1 >= 1.0f) { p1 -= 1.0f; d1 = 2.0f; }
        l[i] = l[i] * dryg + wet * 0.5f * (yl + ol); l[i + 1u] = l[i + 1u] * dryg + wet * ol;
        r[i] = r[i] * dryg + wet * 0.5f * (yr + orr); r[i + 1u] = r[i + 1u] * dryg + wet * orr;
        yl = ol; yr = orr;
    }
    s->w = w; s->rd[0] = d0; s->rd[1] = d1; s->rph[0] = p0; s->rph[1] = p1; s->yl = yl; s->yr = yr;
}

/* ============================================================== Texture === */
static void fx_filter(pl_engine_t *e, float *l, float *r)
{
    const pl_tc_t *k = &e->tx; pl_ts_t *s = &e->ts;
    const float a1 = k->a1, a2 = k->a2, a3 = k->a3, m0 = k->m0, m1 = k->m1, m2 = k->m2;
    for (int c = 0; c < 2; c++) {
        float *x = c ? r : l, ic1 = s->ic1[c], ic2 = s->ic2[c];
        for (uint32_t i = 0; i < PL_FR; i++) {
            const float v0 = x[i], v3 = v0 - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2; x[i] = m0 * v0 + m1 * v1 + m2 * v2;
        }
        s->ic1[c] = ic1; s->ic2[c] = ic2;
    }
}
static void fx_squash(pl_engine_t *e, float *l, float *r)
{
    const pl_tc_t *k = &e->tx; pl_ts_t *s = &e->ts;
    const float katt = k->katt, krel = k->krel, kc = k->kc, make = k->make, drv = k->drv, lvl = k->lvl;
    float env = s->env;
    for (uint32_t i = 0; i < PL_FR; i++) {
        float a = f_abs(l[i]); const float b = f_abs(r[i]); a = a > b ? a : b;
        env += (a > env ? katt : krel) * (a - env);
        const float gn = drv * make / (1.0f + kc * env);
        l[i] = f_sat(l[i] * gn) * lvl; r[i] = f_sat(r[i] * gn) * lvl;
    }
    s->env = env;
}
static void fx_cassette(pl_engine_t *e, float *l, float *r)
{
    const pl_tc_t *k = &e->tx; pl_ts_t *s = &e->ts;
    int16_t *bl = e->txb, *br = e->txb + PL_TX_N / 2; const int32_t M = PL_TX_N / 2 - 1; int32_t w = s->w & M;
    const float base = k->base, wow = k->wow * (1.0f + 0.5f * drift_step(&s->dft[0], 0.1f)), flut = k->flut, inc1 = k->inc1,
                inc2 = k->inc2, katt = k->katt, krel = k->krel, kc = k->kc, make = k->make, drv = k->drv, klp = k->klp,
                khp = k->khp, nz = k->nz, lvl = k->lvl;
    const float drop = (1.0f - k->drop * f_abs(drift_step(&s->dft[1], 0.3f))) * lvl;
    float ph1 = s->ph1, ph2 = s->ph2, env = s->env, a1 = s->l1[0], a2 = s->l2[0], ah = s->h[0], b1 = s->l1[1], b2 = s->l2[1],
          bh = s->h[1]; uint32_t rnd = e->rnd;
    for (uint32_t i = 0; i < PL_FR; i++) {
        bl[w] = f_q(l[i]); br[w] = f_q(r[i]);
        const float fl = flut * f_psin(ph2);
        float yl = f_rd(bl, w, M, base + wow * f_psin(ph1) + fl) + 1e-15f, yr = f_rd(br, w, M, base + wow * f_psin(f_wrap(ph1 + 0.07f)) + fl) + 1e-15f;
        ph1 = f_wrap(ph1 + inc1); ph2 = f_wrap(ph2 + inc2);
        float a = f_abs(yl); const float b = f_abs(yr); a = a > b ? a : b;
        env += (a > env ? katt : krel) * (a - env);
        const float gn = drv * make / (1.0f + kc * env), hiss = nz * f_noise(&rnd);
        yl = f_sat(yl * gn); a1 += klp * (yl - a1); a2 += klp * (a1 - a2); ah += khp * (a2 - ah); l[i] = (a2 - ah + hiss) * drop;
        yr = f_sat(yr * gn); b1 += klp * (yr - b1); b2 += klp * (b1 - b2); bh += khp * (b2 - bh); r[i] = (b2 - bh + hiss) * drop;
        w = (w + 1) & M;
    }
    s->w = w; s->ph1 = ph1; s->ph2 = ph2; s->env = env; e->rnd = rnd;
    s->l1[0] = a1; s->l2[0] = a2; s->h[0] = ah; s->l1[1] = b1; s->l2[1] = b2; s->h[1] = bh;
}
static void fx_broken(pl_engine_t *e, float *l, float *r)
{
    const pl_tc_t *k = &e->tx; pl_ts_t *s = &e->ts;
    int16_t *bl = e->txb, *br = e->txb + PL_TX_N / 2; const int32_t M = PL_TX_N / 2 - 1; int32_t w = s->w & M;
    const float am = k->am, fm = k->fm, inc1 = k->inc1, inc2 = k->inc2, lvl = k->lvl;
    /* the motor: now and then it slows (the lag grows: the pitch drops), then it catches up (the pitch rises back) */
    if (--s->evt <= 0) {
        if (s->slope > 0.0f) { s->slope = -0.5f * k->rate; s->evt = 1 << 20; }
        else { s->slope = k->rate; s->evt = 20 + (int32_t)(40.0f * f_abs(drift_step(&s->dft[0], 1.0f))); }
    }
    float d = s->d, slope = s->slope, ph1 = s->ph1, ph2 = s->ph2;
    if (slope > 0.0f && d + slope * (float)PL_FR > 3600.0f) { slope = -0.5f * k->rate; s->slope = slope; s->evt = 1 << 20; }
    if (slope < 0.0f && d + slope * (float)PL_FR < 0.0f) { slope = 0.0f; s->slope = 0.0f; s->evt = 30 + (int32_t)(s->dft[0].rnd >> 26); }
    for (uint32_t i = 0; i < PL_FR; i++) {
        bl[w] = f_q(l[i]); br[w] = f_q(r[i]);
        const float dd = d + fm * (1.0f + f_psin(ph2)) + 2.0f, g = (1.0f - am * (0.5f + 0.5f * f_psin(ph1))) * lvl;
        l[i] = f_rd(bl, w, M, dd) * g; r[i] = f_rd(br, w, M, dd) * g;
        d += slope; ph1 = f_wrap(ph1 + inc1); ph2 = f_wrap(ph2 + inc2); w = (w + 1) & M;
    }
    s->w = w; s->d = d; s->ph1 = ph1; s->ph2 = ph2;
}
AINL void interf_core(pl_engine_t *e, float *l, float *r, const int stut)
{
    const pl_tc_t *k = &e->tx; pl_ts_t *s = &e->ts;
    int16_t *bl = e->txb, *br = e->txb + PL_TX_N / 2; const int32_t M = PL_TX_N / 2 - 1; int32_t w = s->w & M, sk = s->sk;
    const int32_t sp = s->sp, sl = s->sl;
    const float cinc = k->cinc, lev = k->lev, ilev = k->ilev, rm = k->rm, rinc = k->rinc, nz = k->nz, dry = 1.0f - k->amt,
                amt = k->amt * (1.0f + 1.5f * k->amt * k->amt);     /* (the coarser it gets, the quieter: made up for) */
    float cph = s->cph, hl = s->cheld[0], hr = s->cheld[1], rph = s->rph; uint32_t rnd = e->rnd;
    for (uint32_t i = 0; i < PL_FR; i++) {
        float xl = l[i], xr = r[i];
        if (stut) { xl = (float)bl[(sp + sk) & M] * I16; xr = (float)br[(sp + sk) & M] * I16; if (++sk >= sl) sk = 0; }   /* a stuck packet */
        else { bl[w] = f_q(xl); br[w] = f_q(xr); w = (w + 1) & M; }
        cph += cinc; if (cph >= 1.0f) { cph -= 1.0f; hl = xl; hr = xr; }             /* a lower sample rate */
        float ql = (float)(int32_t)(hl * lev) * ilev, qr = (float)(int32_t)(hr * lev) * ilev;   /* fewer bits */
        const float car = (1.0f - rm) + rm * f_psin(rph), hiss = nz * f_noise(&rnd) * (f_abs(hl) + f_abs(hr));
        rph = f_wrap(rph + rinc);
        ql = ql * car + hiss; qr = qr * car + hiss;
        l[i] = l[i] * dry + ql * amt; r[i] = r[i] * dry + qr * amt;
    }
    s->w = w; s->sk = sk; s->cph = cph; s->cheld[0] = hl; s->cheld[1] = hr; s->rph = rph; e->rnd = rnd;
}
static void fx_interf(pl_engine_t *e, float *l, float *r)
{
    pl_ts_t *s = &e->ts;
    if (s->stut > 0) { s->stut--; interf_core(e, l, r, 1); return; }
    if (drift_step(&s->dft[0], 1.0f) > 1.0f - 0.2f * e->tx.amt) {   /* a stutter starts: the last few milliseconds repeat */
        s->sl = 400 + (int32_t)(s->dft[0].rnd >> 21); s->sp = (s->w - s->sl) & (PL_TX_N / 2 - 1); s->sk = 0;
        s->stut = 4 + (int32_t)(s->dft[0].rnd >> 28);
    }
    interf_core(e, l, r, 0);
}

/* ================================================================ chain === */
typedef void (*pl_fn)(pl_engine_t *, float *, float *);
static const pl_fn PL_FX[PL_NMOD][PL_NFX] = {
    { fx_drive, fx_sweeten, fx_fuzz, fx_howl, fx_swell },
    { fx_doubler, fx_vibrato, fx_phaser, fx_tremolo, fx_pitch },
    { fx_cascade, fx_reels, fx_space, fx_collage, fx_reverse },
    { fx_filter, fx_squash, fx_cassette, fx_broken, fx_interf },
};
static const int16_t SP_LEN[12] = { 142, 107, 379, 277, 1117, 1321, 1493, 1657, 1811, 1997, 2131, 2311 };   /* space: ticks */
#define SP_DIF_MAX 1.2f                         /* the diffusers' and the lines' longest, as a multiple of SP_LEN */
#define SP_LIN_MAX 1.6f
static const uint8_t PL_LEAD[PL_NMOD][PL_NFX] = { {2, 2, 2, 2, 2}, {2, 2, 2, 2, 9}, {2, 2, 2, 2, 2}, {2, 2, 3, 2, 2} };   /* blocks unheard at a start */

/* module m takes pl_commit's numbers, and starts effect fx from nothing: its memory cleared, its glides where they go */
static void pl_swap(pl_engine_t *e, uint32_t m, uint32_t fx)
{
    if (m == PL_CHAR) {
        e->ch = e->next.ch;
        memset(&e->cs, 0, sizeof e->cs); e->cs.arm = 1;
    } else if (m == PL_MOVE) {
        e->mv = e->next.mv;
        memset(&e->ms, 0, sizeof e->ms); memset(e->mvb, 0, sizeof e->mvb);
        e->ms.dft[0].rnd = 77u; e->ms.dft[1].rnd = 78u;
        e->ms.dl = fx == PL_VIBRATO ? e->mv.depth : e->mv.dlt; e->ms.dr = e->mv.drt; e->ms.base = e->mv.a0; e->ms.rest = e->mv.rest;
        e->ms.fb = e->mv.fb;
        for (int j = 0; j < 6; j++) e->ms.w6[j] = e->mv.w6[j];
        if (fx == PL_PITCH) e->ms.r0 = e->mv.pinc;
    } else if (m == PL_DIFF) {
        int32_t at = 0;
        e->df = e->next.df;
        memset(&e->ds, 0, sizeof e->ds); memset(e->dfb, 0, sizeof e->dfb);
        e->ds.dft[0].rnd = 991u; e->ds.dft[1].rnd = 992u;
        e->ds.d = e->df.dt; e->ds.spd = 1.0f; e->ds.rd[0] = e->ds.rd[1] = 2.0f; e->ds.rph[1] = 0.5f;
        for (int j = 0; j < 12; j++) {
            e->ds.n[j] = e->df.n[j]; e->ds.b[j] = at;
            at += (int32_t)((float)SP_LEN[j] * (j < 4 ? SP_DIF_MAX : SP_LIN_MAX)) + 26;
        }
    } else {
        e->tx = e->next.tx;
        memset(&e->ts, 0, sizeof e->ts); memset(e->txb, 0, sizeof e->txb);
        e->ts.dft[0].rnd = 4242u; e->ts.dft[1].rnd = 4243u;
    }
    e->run[m] = (uint8_t)fx;
    /* it runs unheard first: its input fades in (two blocks), and an effect that is only its line's output has no
     * sound before the line has filled (Cassette reads 364 samples back at most, Pitch's second head 1026) */
    e->lead[m] = fx < PL_NFX ? PL_LEAD[m][fx] : 0; e->fg[m] = 0.0f; e->redo[m] = 0;
}
static void pl_adopt(pl_engine_t *e, uint32_t m)    /* a knob moved: the running effect's numbers follow */
{
    if (m == PL_CHAR) e->ch = e->next.ch;
    else if (m == PL_MOVE) e->mv = e->next.mv;
    else if (m == PL_DIFF) e->df = e->next.df;
    else e->tx = e->next.tx;
}
static int pl_moved(const pl_engine_t *e, uint32_t m)   /* pl_commit's numbers for module m are not the ones it runs on */
{
    if (m == PL_CHAR) return memcmp(&e->ch, &e->next.ch, sizeof e->ch) != 0;
    if (m == PL_MOVE) return memcmp(&e->mv, &e->next.mv, sizeof e->mv) != 0;
    if (m == PL_DIFF) return memcmp(&e->df, &e->next.df, sizeof e->df) != 0;
    return memcmp(&e->tx, &e->next.tx, sizeof e->tx) != 0;
}
/* a gain that glides: a quarter of its way a block, as a straight line across the block */
AINL void pl_gain(float *l, float *r, float *now, float tgt)
{
    const float g0 = *now; float g1 = g0 + 0.25f * (tgt - g0);
    if (f_abs(tgt - g1) < 0.0005f) g1 = tgt;
    if (g0 == 1.0f && g1 == 1.0f) return;
    const float dg = (g1 - g0) * (1.0f / (float)PL_FR); float g = g0;
    for (uint32_t i = 0; i < PL_FR; i++) { g += dg; l[i] *= g; r[i] *= g; }
    *now = g1;
}

void pl_run(pl_engine_t *e, const float *in, float *out)
{
    float *l = out, *r = out + PL_FR;
    const pl_coef_t *nx = &e->next;
    uint32_t differs = 0;
    if (nx->bypass && e->bg == 0.0f) {                              /* bypassed, and the crossfade is over: nothing runs */
        for (uint32_t m = 0; m < PL_NMOD; m++) { e->run[m] = PL_OFF; e->mg[m] = 0.0f; e->redo[m] = 0; e->order[m] = nx->order[m]; }
        for (uint32_t i = 0; i < PL_FR; i++) l[i] = r[i] = in[i];
        e->fresh = 0;
        return;
    }
    for (uint32_t i = 0; i < PL_FR; i++) e->dryb[i] = l[i] = r[i] = in[i];
    for (uint32_t m = 0; m < PL_NMOD; m++) differs |= (uint32_t)(e->order[m] ^ nx->order[m]);
    if (differs && e->og == 0.0f) {                                 /* the chain is silent: the new order */
        for (uint32_t m = 0; m < PL_NMOD; m++) e->order[m] = nx->order[m];
        differs = 0;
    }
    for (uint32_t k = 0; k < PL_NMOD; k++) {                        /* the four modules, in the sound's order */
        const uint32_t m = e->order[k], want = nx->fx[m];
        if (e->run[m] != want) { e->redo[m] = 0; if (e->mg[m] == 0.0f) pl_swap(e, m, want); }
        else if (e->fresh && want != PL_OFF) {                      /* its knobs: a small move at once, a jump blended */
            if (e->redo[m] || (nx->hard && pl_moved(e, m))) e->redo[m] = 1; else pl_adopt(e, m);
        }
        if (e->redo[m] && e->mg[m] == 0.0f) { pl_adopt(e, m); e->redo[m] = 0; }
        if (e->run[m] == PL_OFF) continue;
        for (uint32_t i = 0; i + 1u < PL_FR; i += 2u) {             /* +-1e-15: silence, or the exact zeros of the module before */
            l[i] += 1e-15f; r[i] += 1e-15f; l[i + 1u] -= 1e-15f; r[i + 1u] -= 1e-15f;
        }
        const float tgt = e->run[m] == want && !e->redo[m] ? 1.0f : 0.0f;
        const int blend = e->mg[m] != 1.0f || tgt != 1.0f;
        if (blend) for (uint32_t i = 0; i < PL_FR; i++) { e->tl[i] = l[i]; e->tr[i] = r[i]; }
        if (e->fg[m] < 1.0f) {                                      /* it just started: what it takes in fades in */
            float fg = e->fg[m];
            for (uint32_t i = 0; i < PL_FR; i++) { fg += XF; if (fg > 1.0f) fg = 1.0f; l[i] *= fg; r[i] *= fg; }
            e->fg[m] = fg;
        }
        PL_FX[m][e->run[m]](e, l, r);
        pl_gain(l, r, &e->vol[m], nx->vol[m]);
        if (blend) {                                                /* between the module's input and its effect */
            float g = e->mg[m]; float st = tgt > 0.5f ? XF : -XF;
            if (e->lead[m]) { e->lead[m]--; if (st > 0.0f) st = 0.0f; }   /* (unheard still) */
            for (uint32_t i = 0; i < PL_FR; i++) {
                g += st; if (g > 1.0f) g = 1.0f; if (g < 0.0f) g = 0.0f;
                l[i] = e->tl[i] + g * (l[i] - e->tl[i]); r[i] = e->tr[i] + g * (r[i] - e->tr[i]);
            }
            e->mg[m] = g;
        }
    }
    e->fresh = 0;
    {   /* MIX, OUTPUT LEVEL, the chain's fade around a new order, bypass's crossfade to the guitar, the ceiling */
        const float w0 = e->wet * e->level, d0 = e->dry * e->level;
        float wet = e->wet + 0.25f * (nx->wet - e->wet), dry = e->dry + 0.25f * (nx->dry - e->dry), lv = e->level + 0.25f * (nx->level - e->level);
        if (f_abs(nx->wet - wet) < 0.0005f) { wet = nx->wet; dry = nx->dry; }
        if (f_abs(nx->level - lv) < 0.0005f) lv = nx->level;
        const float dw = (wet * lv - w0) * (1.0f / (float)PL_FR), ddry = (dry * lv - d0) * (1.0f / (float)PL_FR);
        const float ost = differs ? -XF : XF, bst = nx->bypass ? -XF : XF;
        float w = w0, d = d0, og = e->og, bg = e->bg;
        for (uint32_t i = 0; i < PL_FR; i++) {
            w += dw; d += ddry;
            og += ost; if (og > 1.0f) og = 1.0f; if (og < 0.0f) og = 0.0f;
            bg += bst; if (bg > 1.0f) bg = 1.0f; if (bg < 0.0f) bg = 0.0f;
            const float x = e->dryb[i], yl = l[i] * w * og + x * d, yr = r[i] * w * og + x * d;
            l[i] = f_ceil(x + bg * (yl - x)); r[i] = f_ceil(x + bg * (yr - x));
        }
        e->wet = wet; e->dry = dry; e->level = lv; e->og = og; e->bg = bg;
    }
}

void pl_commit(pl_engine_t *e, const pl_coef_t *c) { e->next = *c; e->fresh = 1; }

void pl_reset(pl_engine_t *e)
{
    const pl_coef_t c = e->next;
    memset(e, 0, sizeof *e);
    e->next = c; e->fresh = 1;
    e->rnd = 0x1234567u;
    for (uint32_t m = 0; m < PL_NMOD; m++) { e->run[m] = PL_OFF; e->order[m] = c.order[m]; e->vol[m] = c.vol[m]; }
    e->og = 1.0f; e->bg = c.bypass ? 0.0f : 1.0f;
    e->wet = c.wet; e->dry = c.dry; e->level = c.level;
}

/* ============================================================== set-up === */
void pl_default(pl_sound_t *s)
{
    memset(s, 0, sizeof *s);
    for (uint32_t k = 0; k < PL_NKNOB; k++) s->knob[k] = 50;
    s->knob[PL_K_MIX] = 100; s->knob[PL_K_DRIFT_MOVE] = s->knob[PL_K_DRIFT_DIFF] = 0;
    for (uint32_t m = 0; m < PL_NMOD; m++) s->order[m] = (uint8_t)m;
}
int pl_sound_ok(const pl_sound_t *s)
{
    uint32_t seen = 0;
    for (uint32_t k = 0; k < PL_NKNOB; k++) if (s->knob[k] > 100u) return 0;
    for (uint32_t m = 0; m < PL_NMOD; m++) {
        if (s->fx[m] >= PL_NFX || s->order[m] >= PL_NMOD) return 0;
        seen |= 1u << s->order[m];
    }
    if (seen != 15u || s->on > 15u || s->style >= PL_NSTYLE) return 0;
    return s->tempo == 0u || (s->tempo >= PL_TEMPO_MIN && s->tempo <= PL_TEMPO_MAX);
}
static float f01(uint8_t v) { return (float)v * 0.01f; }
static float db_lin(float db) { return powf(10.0f, db * 0.05f); }
static float k_hz(float hz, float sr) { return 1.0f - expf(-6.2831853f * hz / sr); }     /* a one-pole filter's step */
static float k_sec(float sec, float sr) { return 1.0f - expf(-1.0f / (sec * sr)); }
static float vol_db(uint8_t v) { return db_lin(((float)v - 50.0f) * 0.24f); }             /* 50 neutral, +-12 dB */
static void svf_set(float hz, float q, float sr, float *a1, float *a2, float *a3, float *kk)
{
    const float w = 3.14159265f * hz / sr, g = sinf(w) / cosf(w); *kk = 1.0f / q;
    *a1 = 1.0f / (1.0f + g * (g + *kk)); *a2 = g * *a1; *a3 = g * *a2;
}
/* a knob as one of n note values (with a tempo): the value's length in quarter notes */
static float note_of(uint8_t v, const float *div, uint32_t n) { return div[(uint32_t)v * n / 101u]; }

void pl_coefs(const pl_sound_t *s, uint32_t quarter_ms, int bypass, pl_coef_t *c)
{
    static const float LFO_DIV[8] = { 16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 0.5f, 1.0f / 3.0f, 0.25f };   /* a cycle, in quarter notes */
    static const float DLY_DIV[5] = { 0.25f, 1.0f / 3.0f, 0.5f, 0.75f, 1.0f };                     /* a repeat */
    static const float LOOP_DIV[5] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };                           /* collage's loop */
    const float sr = PL_SR, hr = PL_SR * 0.5f, q = (float)quarter_ms;
    const float tilt = f01(s->knob[PL_K_TILT]), rate = f01(s->knob[PL_K_RATE]), time = f01(s->knob[PL_K_TIME]);
    const float sens = vol_db(s->knob[PL_K_SENS]), dm = f01(s->knob[PL_K_DRIFT_MOVE]), dd = f01(s->knob[PL_K_DRIFT_DIFF]);
    memset(c, 0, sizeof *c);
    for (uint32_t m = 0; m < PL_NMOD; m++) {
        c->fx[m] = (uint8_t)((s->on >> m) & 1u ? s->fx[m] : PL_OFF); c->order[m] = s->order[m];
        c->vol[m] = m == PL_DIFF ? 1.0f : vol_db(s->knob[PL_K_VOL(m)]);     /* (Diffusion's is its wet part's, below) */
    }
    c->bypass = (uint8_t)(bypass != 0);
    c->wet = f01(s->knob[PL_K_MIX]); c->dry = 1.0f - c->wet; c->level = vol_db(s->knob[PL_K_LEVEL]);
    {   /* Character */
        pl_cc_t *k = &c->ch; const float am = f01(s->knob[PL_K_AMT_CHAR]);
        k->katt = k_sec(0.002f, sr); k->krel = k_sec(0.12f, sr);
        switch (s->fx[PL_CHAR]) {
        case PL_DRIVE:
            k->g = sens * db_lin(6.0f + am * 34.0f); k->khp = k_hz(30.0f + 280.0f * tilt, sr); k->kt = k_hz(700.0f, sr);
            k->gl = tilt > 0.5f ? 1.0f - (tilt - 0.5f) * 1.4f : 1.0f; k->gh = tilt < 0.5f ? 0.15f + 1.7f * tilt : 1.0f;
            k->bias = 0.12f; k->lvl = 0.75f / (1.0f + 5.22f * am + 2.6f * am * am); break;
        case PL_SWEETEN:
            k->kc = 24.0f * am * sens; k->make = 1.0f + 2.5f * am; k->sat = 1.5f * am; k->kt = k_hz(180.0f, sr);
            k->khp = k_hz(3500.0f, sr); k->gl = 0.5f * (1.0f - tilt) * am; k->gh = 0.8f * tilt * am; k->lvl = 1.0f; break;
        case PL_FUZZ: case PL_HOWL: {
            float kk;
            k->g = sens * db_lin(20.0f + am * 40.0f); k->g2 = 3.0f; k->khp = k_hz(40.0f + 360.0f * tilt, sr);
            k->kt = k_hz(1500.0f + 6500.0f * tilt, sr); k->bias = 0.05f + 0.35f * (1.0f - tilt); k->bk = 2.0f * tilt;
            k->kb = k_sec(0.03f, sr); k->sq = 1.5f;
            svf_set(300.0f * powf(2.0f, 3.5f * tilt), 2.0f + 8.0f * am, sr, &k->a1, &k->a2, &k->a3, &kk);
            k->lvl = s->fx[PL_CHAR] == PL_HOWL ? 0.09f / (1.0f + 1.1f * am) : 0.135f;
        } break;
        default:    /* PL_SWELL */
            k->katt = k_sec(0.001f, sr); k->krel = k_sec(0.03f, sr); k->thr = 0.02f / sens;
            k->ratio = 1.3f + 0.7f / sens; k->kup = k_sec(0.03f + 1.5f * am, sr); k->kdn = k_sec(0.05f + 0.5f * am, sr); break;
        }
    }
    {   /* Movement */
        pl_mc_t *k = &c->mv; const float am = f01(s->knob[PL_K_AMT_MOVE]);
        const float hz = quarter_ms ? 1000.0f / (q * note_of(s->knob[PL_K_RATE], LFO_DIV, 8)) : 0.2f * powf(2.0f, 5.6f * rate);
        k->inc = hz / sr; k->amt = am; k->dfk = dm; k->off = 0.25f * dm; k->lvl = 1.0f;
        switch (s->fx[PL_MOVE]) {
        case PL_DOUBLER:
            k->dlt = (12.0f + 100.0f * rate) * 0.001f * sr; k->drt = k->dlt * 1.31f + 0.004f * sr; k->dfk = 0.02f * dm;
            k->lvl = 1.0f / (1.0f + 0.45f * am); break;
        case PL_VIBRATO: k->depth = 60.0f * am * am + 1.0f; k->dfk = 0.5f * dm; break;
        case PL_PHASER: {
            const float pos = am * 4.999f; const int j = (int)pos;
            k->w6[j] = 1.0f - (pos - (float)j); k->w6[j + 1] = pos - (float)j;
            k->a0 = 0.55f; k->a1 = 0.97f; k->fb = 0.3f * am; k->dfk = 0.3f * dm; k->lvl = 1.2f + 0.25f * am;
        } break;
        case PL_TREMOLO:
            k->depth = am > 0.83f ? 1.0f : 1.2f * am; k->hard = 30.0f * am * am * am; k->dfk = 0.4f * dm;
            k->lvl = 1.0f + 0.5f * k->depth; break;
        default: {  /* PL_PITCH */
            const float ratio = powf(2.0f, ((float)s->knob[PL_K_RATE] - 50.0f) * 0.02f);
            k->W = 2048.0f; k->pinc = (1.0f - ratio) / k->W; k->hold = 1 + (int32_t)(dm * dm * 24.0f); k->dfk = 0.02f * dm;
            k->rest = s->knob[PL_K_RATE] == 50u ? 1.0f : 0.0f;
        } break;
        }
    }
    {   /* Diffusion (half rate) */
        pl_dc_t *k = &c->df; const float am = f01(s->knob[PL_K_AMT_DIFF]), vol = vol_db(s->knob[PL_K_VOL_DIFF]);
        k->wet = 0.5f * vol; k->dryg = 1.0f; k->dfk = dd; k->khp = k_hz(100.0f, hr); k->ks = 0.0006f;
        k->inc1 = 0.7f / hr; k->inc2 = 9.0f / hr; k->off = 0.007f * hr; k->spdt = 1.0f;
        for (int j = 0; j < 12; j++) k->n[j] = (int32_t)((float)SP_LEN[j] * (j < 4 ? 0.5f + 0.7f * time : 0.3f + 1.3f * time));
        switch (s->fx[PL_DIFF]) {
        case PL_CASCADE: case PL_REELS: {
            const int reels = s->fx[PL_DIFF] == PL_REELS;
            float ms = quarter_ms ? q * note_of(s->knob[PL_K_TIME], DLY_DIV, 5) : 40.0f + 960.0f * time * time;
            if (ms > 1900.0f) ms = 1900.0f;
            if (ms < 20.0f) ms = 20.0f;
            k->dt = ms * 0.001f * hr; k->fb = 1.15f * am; k->klp = k_hz(reels ? 6000.0f : 4500.0f - 3000.0f * time, hr);
            k->wow = (reels ? 4.0f : 1.5f) + 30.0f * dd; k->flut = reels ? 0.5f + 2.0f * dd : 0.0f;
            k->nz = reels ? 0.002f * dd : 0.0f; k->drop = 0.6f * dd;
        } break;
        case PL_SPACE:
            k->g = 0.55f + 0.43f * time; k->kd = k_hz(6000.0f - 3500.0f * time, hr); k->dg = 0.6f;
            k->md = 8.0f * dd; k->sinc = 0.6f / hr;
            k->wet = am * (2.0f - am) * vol * 4.0f * 0.96f * sqrtf(1.0f - k->g * k->g); k->dryg = 1.0f - am * am; break;
        case PL_COLLAGE: {
            float ms = quarter_ms ? q * note_of(s->knob[PL_K_TIME], LOOP_DIV, 5) : 60.0f + 1900.0f * time * time;
            if (ms > 1960.0f) ms = 1960.0f;
            if (ms < 30.0f) ms = 30.0f;
            k->len = ms * 0.001f * hr; k->fb = 1.05f * am;
        } break;
        default: {  /* PL_REVERSE: a head's segment is half a second, or a note of the tempo that fits the line at double speed */
            float seg = quarter_ms ? q * 0.001f : 0.5f;
            while (seg > 0.6f) seg *= 0.5f;
            if (seg < 0.1f) seg = 0.1f;
            k->pinc = 1.0f / (seg * hr); k->step = 1.0f + powf(2.0f, ((float)s->knob[PL_K_TIME] - 50.0f) * 0.02f); k->fb = 0.3f;
            k->wet = am * (2.0f - am) * vol * 1.5f; k->dryg = 1.0f - am * am; k->dfk = 0.03f * dd;
        } break;
        }
    }
    {   /* Texture */
        pl_tc_t *k = &c->tx; const float am = f01(s->knob[PL_K_AMT_TEXT]);
        k->katt = k_sec(0.003f, sr); k->krel = k_sec(0.1f, sr); k->amt = am; k->lvl = 1.0f;
        switch (s->fx[PL_TEXT]) {
        case PL_FILTER: {
            const int st = s->style; float hz, kk;
            const int low = st == PL_LP_STYLE || (st == PL_TILT_STYLE && am < 0.5f), flat = st == PL_TILT_STYLE && s->knob[PL_K_AMT_TEXT] == 50u;
            if (st == PL_LP_STYLE) hz = 20000.0f * powf(2.0f, -7.0f * (1.0f - am));
            else if (st == PL_HP_STYLE) hz = 20.0f * powf(2.0f, 8.0f * am);
            else hz = am < 0.5f ? 20000.0f * powf(2.0f, -12.0f * (0.5f - am)) : 20.0f * powf(2.0f, 14.0f * (am - 0.5f));
            if (hz > 0.45f * sr) hz = 0.45f * sr;
            svf_set(hz, 0.707f, sr, &k->a1, &k->a2, &k->a3, &kk);
            if (flat) { k->m0 = 1.0f; }
            else if (low) { k->m2 = 1.0f; }
            else { k->m0 = 1.0f; k->m1 = -kk; k->m2 = -1.0f; }
        } break;
        case PL_SQUASH:
            k->kc = 40.0f * am; k->make = 1.0f + 4.0f * am; k->drv = am > 0.7f ? 1.0f + 20.0f * (am - 0.7f) : 1.0f;
            k->lvl = (1.0f + 0.55f * am) / ((1.0f + 0.5f * am) * powf(k->drv, 0.75f)); break;
        case PL_CASSETTE:
            k->base = 300.0f; k->wow = 40.0f * am; k->flut = 4.0f * am; k->inc1 = 0.8f / sr; k->inc2 = 11.0f / sr;
            k->klp = k_hz(12000.0f - 9000.0f * am, sr); k->khp = k_hz(40.0f + 150.0f * am, sr); k->drv = 1.0f + 4.0f * am;
            k->kc = 8.0f * am; k->make = 1.0f + am; k->nz = 0.001f * am; k->drop = 0.5f * am; k->lvl = 1.12f / (1.0f + 3.17f * am - 1.74f * am * am); break;
        case PL_BROKEN:
            k->rate = 0.04f + 0.6f * am; k->am = 0.6f * am; k->fm = 6.0f * am; k->inc1 = 5.0f / sr; k->inc2 = 23.0f / sr;
            k->lvl = 1.0f + 0.3f * am; break;
        default:    /* PL_INTERF */
            k->cinc = powf(2.0f, -4.0f * am); k->lev = powf(2.0f, 12.0f - 8.0f * am); k->ilev = 1.0f / k->lev;
            k->rm = 0.5f * am; k->rinc = 700.0f / sr; k->nz = 0.02f * am; break;
        }
    }
}
