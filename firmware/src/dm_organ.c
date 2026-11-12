/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 MOODHAY (group-sync branch) */
/* ORGAN: a house organ stab (the M1-organ-on-a-chord sound). Up to 4 chord notes, each a stack of 4 sine "drawbars"
 * at 1x 2x 3x 4x the note (8' 4' 2 2/3' 2'); DRAW morphs the drawbar mix from a soft flute to a bright full organ.
 * A key click (CLICK: a short high-passed noise tick), a slow rotary-style pitch wobble (VIB), a fast attack ramp
 * (ATK), the notes spread by a few cents so the chord is wide, an exponential DECAY (the stab). VEL: how much the
 * hit's velocity brightens DRAW. 2 voices. State: 118 bytes of the voice's model state (fits the union with room). */
static const int8_t ORG_CHORD[8][ORG_NN + 1] = {        /* semitones above the root; the last column = the note count */
    {0, 0, 0, 0, 1},    /* 1    a single note */
    {0, 12, 0, 0, 2},   /* OCT  root and octave */
    {0, 3, 7, 0, 3},    /* MIN  minor triad */
    {0, 4, 7, 0, 3},    /* MAJ  major triad */
    {0, 3, 7, 10, 4},   /* MIN7 */
    {0, 3, 10, 14, 4},  /* MIN9 the jazzy rootless-ish stab: root, b3, b7, 9 */
    {0, 4, 7, 11, 4},   /* MAJ7 */
    {0, 5, 7, 12, 4},   /* SUS4 add octave */
};
static const char *const N_ORGCHORD[8] = {"1", "OCT", "MIN", "MAJ", "MIN7", "MIN9", "MAJ7", "SUS4"};
/* drawbar levels (Q15) at DRAW 0, 42, 85, 127 */
static const int16_t ORG_DRAW[4][ORG_NP] = {{32767, 6000, 0, 0}, {32767, 20000, 9000, 4000},
                                           {32767, 26000, 20000, 14000}, {32767, 32767, 28000, 26000}};
static const int16_t ORG_DET[ORG_NN] = {-110, 85, -60, 120};   /* per-note detune, 1/65536 of the pitch (about 2 cents) */
#define ORG_C4 HZ(261.63)
#define ORG_LFO HZ(5.8)                                 /* the rotary wobble's rate */
#define ORG_VIB_MAX 1325                                /* the wobble's depth at VIB 127: +-0.35 semitone, 1/65536 of the pitch */


static uint32_t org_isqrt(uint32_t x)
{
    uint32_t r = 0, b = 1u << 30;
    while (b > x)
        b >>= 2;
    while (b) {
        if (x >= r + b) {
            x -= r + b;
            r = (r >> 1) + b;
        } else
            r >>= 1;
        b >>= 2;
    }
    return r;
}

/* ORGAN: TUNE DECAY CHORD DRAW, CLICK VIB ATK VEL */
static void org_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    organ_t *o = (organ_t *)&v->ms;
    const int8_t *ch = ORG_CHORD[clamp(p[2], 0, 7)];
    uint32_t k, j, root = inc_tune(ORG_C4, dm_tune(t)), atk, sq = 0, pw;
    int32_t d = clamp(p[3] + p[7] * ((int32_t)v->vel - 96) / 127, 0, 127), pos = d * 3, seg = pos / 127, fr;
    if (seg > 2) {
        seg = 2;
        fr = 127;
    } else
        fr = pos - seg * 127;
    o->nn = (uint8_t)ch[ORG_NN];
    for (j = 0; j < ORG_NP; j++) {
        o->g[j] = ORG_DRAW[seg][j] + (ORG_DRAW[seg + 1][j] - ORG_DRAW[seg][j]) * fr / 127;
        sq += (uint32_t)(((int64_t)o->g[j] * o->g[j]) >> 15);     /* the drawbars' power, Q15 */
    }
    for (k = 0; k < o->nn; k++) {                       /* each note: the root x 2^(semis/12), a few cents apart */
        uint32_t ik = inc_tune(root, ch[k]);
        o->inc[k] = (uint32_t)(((uint64_t)ik * (uint32_t)(65536 + ORG_DET[k])) >> 16);
        for (j = 0; j < ORG_NP; j++)
            o->ph[k][j] = 0;
    }
    pw = o->nn * sq;                                    /* Q15 power of the sum; level: 0.6 / sqrt(power) */
    o->scale = (int32_t)(19660u * 181u / (org_isqrt(pw ? pw : 1) + 1u));   /* sqrt(Q15) = Q7.5 -> 181 = sqrt(32768) */
    atk = 64u + (uint32_t)clamp(p[6], 0, 127) * (uint32_t)clamp(p[6], 0, 127) * 1700u / 16129u;
    o->ramp = 0;
    o->rinc = (int32_t)(65536u / atk);                  /* the ramp (Q16, 65536 = full) per sample */
    o->click = clamp(p[4], 0, 127) * 258;               /* the click's gain, Q15 */
    o->cprev = 0;
    o->vib = clamp(p[5], 0, 127) * ORG_VIB_MAX / 127;
    o->lfo = 0;
    v->env[0] = ENV1;
    v->env[1] = ENV1;
    v->k[0] = dk(p[1]);                                 /* DECAY: 5 ms .. 4 s */
    v->k[1] = dk(24);                                   /* the click: about 25 ms */
    v->rng = (int32_t)(v->age * 2654435761u + 12345u);  /* the click's noise */
}

static void org_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    organ_t *o = (organ_t *)&v->ms;
    uint32_t i, k, j, nn = o->nn, inc[ORG_NN], mult;
    (void)t;
    o->lfo += ORG_LFO * n;
    mult = (uint32_t)(65536 + ((sine_i(o->lfo) * o->vib) >> 15));
    for (k = 0; k < nn; k++)
        inc[k] = (uint32_t)(((uint64_t)o->inc[k] * mult) >> 16);
    for (i = 0; i < n; i++) {
        int32_t s = 0, nz, ck, g = o->ramp >> 1;           /* the attack gain, Q15 */
        for (k = 0; k < nn; k++)
            for (j = 0; j < ORG_NP; j++) {
                s += mulq15(sine_i(o->ph[k][j]), o->g[j]);
                o->ph[k][j] += inc[k] * (j + 1u);
            }
        s = mulq15(s, o->scale);                         /* chord level */
        nz = dnoise(v);                                  /* the key click: noise, high-passed by a first difference */
        ck = mulq15(nz - o->cprev, mulq15(o->click, env_q15(v->env[1])));
        o->cprev = nz;
        s = clamp(s + ck, -32767, 32767);
        if (o->ramp < 65536) {                           /* the attack ramp */
            o->ramp += o->rinc;
            if (o->ramp > 65536)
                o->ramp = 65536;
        }
        dm_put(v, out, i, mulq15(mulq15(s, env_q15(v->env[0])), clamp(g, 0, 32767)));
        env_step(v, 0);
        env_step(v, 1);
    }
    if (v->env[0] < ENV_END)
        dm_end(v);
}

#define DM_ORGAN_DEF {"ORGAN", 2, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 66), \
    PE("CHORD", N_ORGCHORD, 4), PD("DRAW", F_INT, 0, 127, 80), PD("CLICK", F_INT, 0, 127, 40), \
    PD("VIB", F_INT, 0, 127, 20), PD("ATK", F_INT, 0, 127, 8), PD("VEL", F_INT, 0, 127, 40)}, org_trigger, org_render, 2}
