/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 MOODHAY (group-sync branch) */
/* EPIANO: a DX-style electric piano (E.PIANO 1 / Rhodes family) on up to 4 chord notes. Each note: one carrier
 * phase-modulated by two modulators: the BODY (ratio 1, the warm bark; louder with velocity, as the DX7's) and the TINE
 * (ratio 14, the bell of the hammer: a very short envelope TDEC). DECAY: the amplitude and the body's fall; TREM: a slow
 * amplitude wobble (the suitcase's tremolo); CHORD as the ORGAN's table (dm_organ.c). TUNE 0 = C4. 2 voices. */
#define EP_PM 1300u
#define EP_LFO HZ(4.6)

static const int16_t EP_NORM[ORG_NN + 1] = {0, 22900, 16200, 13200, 11450};   /* .7 / sqrt(notes), Q15 */

/* EPIANO: TUNE DECAY CHORD BODY, TINE TDEC TREM VEL */
static void ep_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    epiano_t *e = (epiano_t *)&v->ms;
    const int8_t *ch = ORG_CHORD[clamp(p[2], 0, 7)];
    uint32_t k, root = inc_tune(ORG_C4, dm_tune(t));
    e->nn = (uint8_t)ch[ORG_NN];
    for (k = 0; k < e->nn; k++) {
        uint32_t ik = inc_tune(root, ch[k]);
        e->inc[k] = (uint32_t)(((uint64_t)ik * (uint32_t)(65536 + ORG_DET[k])) >> 16);
        e->ph[k] = 0;
    }
    e->scale = EP_NORM[e->nn];
    e->body = clamp(p[3] + p[7] * ((int32_t)v->vel - 96) / 127, 0, 127);
    e->tine = clamp(p[4], 0, 127);
    e->trem = clamp(p[6], 0, 127);
    e->lfo = 0;
    v->env[0] = v->env[1] = v->env[2] = ENV1;
    v->k[0] = dk(p[1]);                                 /* DECAY: the note */
    v->k[1] = dk(clamp(p[1] - 6, 0, 127));              /* the body's index falls a little faster than the amplitude */
    v->k[2] = dk(clamp(p[5], 0, 127) * 70 / 127);       /* TDEC: the tine, 5 ms .. 120 ms-ish */
}

static void ep_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    epiano_t *e = (epiano_t *)&v->ms;
    uint32_t i, k, nn = e->nn;
    int32_t tg;
    (void)t;
    e->lfo += EP_LFO * n;
    tg = 32767 - (((sine_i(e->lfo) + 32767) >> 1) * e->trem >> 7);   /* tremolo: 1 .. 1 - TREM/127 */
    for (i = 0; i < n; i++) {
        int32_t s = 0, bi = (e->body * env_q15(v->env[1])) >> 15, ti = (e->tine * env_q15(v->env[2])) >> 15;
        for (k = 0; k < nn; k++) {
            uint32_t ph = e->ph[k];
            int32_t mb = sine_i(ph), mt = sine_i(ph * 14u);
            s += sine_i(ph + (uint32_t)(mb * bi + mt * ti) * EP_PM);
            e->ph[k] = ph + e->inc[k];
        }
        s = mulq15(mulq15(s, e->scale), tg);
        dm_put(v, out, i, mulq15(clamp(s, -32767, 32767), env_q15(v->env[0])));
        env_step(v, 0);
        env_step(v, 1);
        env_step(v, 2);
    }
    if (v->env[0] < ENV_END)
        dm_end(v);
}

#define DM_EPIANO_DEF {"EPIAN", 2, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 88), \
    PE("CHORD", N_ORGCHORD, 0), PD("BODY", F_INT, 0, 127, 40), PD("TINE", F_INT, 0, 127, 60), \
    PD("TDEC", F_INT, 0, 127, 30), PD("TREM", F_INT, 0, 127, 25), PD("VEL", F_INT, 0, 127, 60)}, ep_trigger, ep_render, 2}
