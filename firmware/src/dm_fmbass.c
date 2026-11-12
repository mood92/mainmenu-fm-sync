/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 MOODHAY (group-sync branch) */
/* FMBASS: a house FM bass (the DX / M1 bass family). A sine carrier phase-modulated by a sine at RATIO x the pitch;
 * the depth INDEX x an envelope that falls over MDEC, so the note starts bright and plucky and closes to a round sine
 * (the "wub" of an FM bass); a sine SUB an octave below; DRIVE: a soft saturator (cubic) on the sum; DECAY: the
 * amplitude. TUNE 0 = C2 (65.4 Hz); a step's note moves it. VEL: how much the hit's velocity adds to INDEX.
 * One voice (a new note cuts the last: a bass line). No state beyond the voice's own phases and scratch. */
#define FMB_PM 1300u                                    /* phase modulation, as FM_PM (dm_fm.c) */
static const uint16_t FMB_RATIO_Q8[8] = {128, 256, 384, 512, 768, 1024, 1280, 1792};   /* .5 1 1.5 2 3 4 5 7 */
static const char *const N_FMBRATIO[8] = {"0.5", "1", "1.5", "2", "3", "4", "5", "7"};

/* FMBASS: TUNE DECAY INDEX RATIO, MDEC SUB DRIVE VEL */
static void fmb_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    v->inc[0] = inc_tune(HZ(65.41), dm_tune(t));
    v->inc[1] = (uint32_t)(((uint64_t)v->inc[0] * FMB_RATIO_Q8[(uint32_t)clamp(p[3], 0, 7)]) >> 8);
    v->inc[2] = v->inc[0] >> 1;                         /* SUB: an octave down */
    v->ph[0] = v->ph[1] = v->ph[2] = 0;
    v->env[0] = v->env[1] = ENV1;
    v->k[0] = dk(p[1]);                                 /* DECAY: 5 ms .. 4 s */
    v->k[1] = dk(clamp(p[4], 0, 127) * 100 / 127);      /* MDEC: 5 ms .. 1 s */
    v->x[0] = clamp(p[2] + p[7] * ((int32_t)v->vel - 96) / 127, 0, 127);   /* INDEX + VEL x the velocity over a plain step */
    v->x[1] = 32768 + clamp(p[6], 0, 127) * 32768 * 6 / 127;   /* DRIVE: the pre-gain, 1x .. 7x (Q15) */
    v->x[2] = clamp(p[5], 0, 127) * 258;                /* SUB level, Q15 */
    v->x[3] = p[6] > 0;                                 /* the saturator is on */
}

static void fmb_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    uint32_t i;
    int32_t idx = v->x[0], gain = v->x[1], sub = v->x[2];
    (void)t;
    for (i = 0; i < n; i++) {
        int32_t fe = env_q15(v->env[1]);
        int32_t m = sine_i(v->ph[1]);
        int32_t c = sine_i(v->ph[0] + (uint32_t)(m * ((idx * fe) >> 15)) * FMB_PM);
        int32_t s = mulq15(c, 22900) + mulq15(mulq15(sine_i(v->ph[2]), sub), 11500);   /* carrier .7 + sub up to .35 */
        if (v->x[3]) {                                  /* soft clip: y = s - s^3 / 3 on |s| <= 1, held at 2/3, then x1.5 */
            int32_t d = clamp(mulq15(s, gain), -32767, 32767);
            d = d - (int32_t)(((int64_t)d * d >> 15) * d >> 15) / 3;
            s = d + (d >> 1);
        }
        v->ph[0] += v->inc[0];
        v->ph[1] += v->inc[1];
        v->ph[2] += v->inc[2];
        dm_put(v, out, i, mulq15(clamp(s, -32767, 32767), env_q15(v->env[0])));
        env_step(v, 0);
        env_step(v, 1);
    }
    if (v->env[0] < ENV_END)
        dm_end(v);
}

#define DM_FMBASS_DEF {"FMBAS", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 58), \
    PD("INDEX", F_INT, 0, 127, 50), PE("RATIO", N_FMBRATIO, 1), PD("MDEC", F_INT, 0, 127, 45), \
    PD("SUB", F_INT, 0, 127, 50), PD("DRIVE", F_INT, 0, 127, 20), PD("VEL", F_INT, 0, 127, 50)}, fmb_trigger, fmb_render}
