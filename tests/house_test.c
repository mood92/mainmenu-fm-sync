/* FMBASS (dm_fmbass.c) and EPIANO (dm_epiano.c): the pitch, the sub, INDEX / MDEC, DRIVE, DECAY, the chord notes,
 * the tine, TREM, the extremes, determinism. Each test prints one line; any failure exits 1. */
#include "drum_host.h"

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}
#define SECS(s) ((uint32_t)((s) * FS) / CTL * CTL)
static int32_t wl[SECS(8)];

static double goertzel(const int32_t *x, uint32_t n, double f)   /* magnitude of f in x[0..n), Hann-windowed (no
                                                                     * leakage from the decaying envelope) */
{
    double w = 2 * M_PI * f / FS, c = 2 * cos(w), s0, s1 = 0, s2 = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        s0 = x[i] * (0.5 - 0.5 * cos(2 * M_PI * i / n)) + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) / n;
}

static double peak_near(const int32_t *x, uint32_t n, double f, double span)   /* the strongest frequency near f */
{
    double best = f, bv = -1, g;
    for (g = f * (1 - span); g <= f * (1 + span); g += 0.25) {
        double m = goertzel(x, n, g);
        if (m > bv) {
            bv = m;
            best = g;
        }
    }
    return best;
}

static double zc_freq(const int32_t *x, uint32_t a, uint32_t b)   /* rising zero crossings / time */
{
    uint32_t i, n = 0;
    for (i = a + 1; i < b; i++)
        n += x[i - 1] < 0 && x[i] >= 0;
    return n * (double)FS / (b - a);
}

static double hf_ratio(const int32_t *x, uint32_t a, uint32_t b)  /* derivative energy / energy: brightness */
{
    double e = 1e-9, d = 0;
    uint32_t i;
    for (i = a + 1; i < b; i++) {
        e += (double)x[i] * x[i];
        d += (double)(x[i] - x[i - 1]) * (x[i] - x[i - 1]);
    }
    return d / e;
}


static double peak_abs(const int32_t *x, uint32_t a, uint32_t b)
{
    double p = 0;
    uint32_t i;
    for (i = a; i < b; i++)
        if (fabs((double)x[i]) > p)
            p = fabs((double)x[i]);
    return p;
}

static double freq_range(const int32_t *x, uint32_t a, uint32_t b)   /* max - min of the pitch over [a, b): successive
                                                                        interpolated rising zero crossings */
{
    double last = -1, lo = 1e30, hi = 0;
    uint32_t i;
    for (i = a + 1; i < b; i++)
        if (x[i - 1] < 0 && x[i] >= 0) {
            double t = i - 1 + (double)-x[i - 1] / (double)(x[i] - x[i - 1]);
            if (last >= 0) {
                double f = FS / (t - last);
                if (f < lo)
                    lo = f;
                if (f > hi)
                    hi = f;
            }
            last = t;
        }
    return hi - lo;
}
static double semi(double root, double s) { return root * pow(2.0, s / 12.0); }
#define C4 261.63
#define WIN 16384u

static void hit(uint32_t model, const int16_t *k, uint32_t vel, uint32_t frames)
{
    uint32_t i;
    host_init();
    old_set_model(&trk[0], model);
    for (i = 0; i < 8u; i++)
        trk[0].p[P_E0 + i] = k[i];
    drum_hit(&trk[0], vel);
    render_track(&trk[0], wl, frames);
}
#define C2 65.41
/* FMBASS knobs: TUNE DECAY INDEX RATIO MDEC SUB DRIVE VEL ; a plain sine: INDEX 0, SUB 0, DRIVE 0, long decay */
static const int16_t BSINE[8] = {0, 100, 0, 1, 0, 0, 0, 0};

static void test_bass_pitch_sub(void)
{
    int16_t k[8];
    double f1, sub0, sub1;
    memcpy(k, BSINE, sizeof k);
    hit(DM_FMBASS, k, 127, SECS(1.2));
    f1 = peak_near(wl + SECS(0.1), 32768, C2, 0.05);
    sub0 = goertzel(wl + SECS(0.1), 32768, C2 / 2);
    check("FMBASS: TUNE 0 is C2 (65.4 Hz within 0.5 %)", fabs(f1 / C2 - 1) < 0.005);
    k[5] = 127;
    hit(DM_FMBASS, k, 127, SECS(1.2));
    sub1 = goertzel(wl + SECS(0.1), 32768, C2 / 2);
    printf("     sub at C1: SUB 0 %.1f, SUB 127 %.1f (root %.1f)\n", sub0, sub1, goertzel(wl + SECS(0.1), 32768, C2));
    check("FMBASS: SUB 127 adds the octave below (> 10x SUB 0, and a good part of the root)",
          sub1 > 10 * (sub0 + 1) && sub1 > goertzel(wl + SECS(0.1), 32768, C2) / 4);
}

static void test_bass_index(void)
{
    int16_t k[8];
    double lo, hi, early, late;
    memcpy(k, BSINE, sizeof k);
    k[4] = 110;                                    /* the index stays up while we measure */
    k[2] = 20;
    hit(DM_FMBASS, k, 127, SECS(0.6));
    lo = hf_ratio(wl, SECS(0.02), SECS(0.3));
    k[2] = 100;
    hit(DM_FMBASS, k, 127, SECS(0.6));
    hi = hf_ratio(wl, SECS(0.02), SECS(0.3));
    check("FMBASS: more INDEX is brighter (> 2x)", hi > 2 * lo);
    k[1] = 127;                                    /* the amplitude barely falls: no quantisation noise in the late window */
    k[2] = 100;
    k[4] = 90;
    hit(DM_FMBASS, k, 127, SECS(1.0));
    early = hf_ratio(wl, SECS(0.005), SECS(0.06));
    late = hf_ratio(wl, SECS(0.4), SECS(0.55));
    printf("     MDEC 90: brightness early %.4f, late %.4f\n", early, late);
    check("FMBASS: the index closes over MDEC: bright at the hit, a round sine later (early > 3x late)", early > 3 * late);
}

static void test_bass_drive_decay(void)
{
    int16_t k[8];
    double a, b;
    memcpy(k, BSINE, sizeof k);
    k[2] = 40;
    k[1] = 100;
    hit(DM_FMBASS, k, 127, SECS(0.6));
    a = hf_ratio(wl, SECS(0.05), SECS(0.3));
    k[6] = 127;
    hit(DM_FMBASS, k, 127, SECS(0.6));
    b = hf_ratio(wl, SECS(0.05), SECS(0.3));
    check("FMBASS: DRIVE 127 adds harmonics (brighter than DRIVE 0, > 1.3x)", b > 1.3 * a);
    memcpy(k, BSINE, sizeof k);
    k[1] = 20;
    hit(DM_FMBASS, k, 127, SECS(1.0));
    a = peak_abs(wl, SECS(0.5), SECS(0.6));
    k[1] = 110;
    hit(DM_FMBASS, k, 127, SECS(1.0));
    b = peak_abs(wl, SECS(0.5), SECS(0.6));
    check("FMBASS: DECAY 20 is a pluck, 110 rings (level at 0.5 s: > 20x)", b > 20 * (a + 1));
}

/* EPIANO knobs: TUNE DECAY CHORD BODY TINE TDEC TREM VEL ; a plain sine: BODY 0, TINE 0, TREM 0 */
static const int16_t ESINE[8] = {0, 100, 0, 0, 0, 0, 0, 0};

static void test_ep_pitch_chords(void)
{
    static const int8_t IV[8][4] = {{0, -1, -1, -1}, {0, 12, -1, -1}, {0, 3, 7, -1}, {0, 4, 7, -1}, {0, 3, 7, 10},
                                    {0, 3, 10, 14}, {0, 4, 7, 11}, {0, 5, 7, 12}};
    int c, j, ok = 1;
    int16_t k[8];
    memcpy(k, ESINE, sizeof k);
    hit(DM_EPIANO, k, 127, SECS(0.8));
    check("EPIANO: a single sine at TUNE 0 is C4 (261.6 Hz within 0.5 %)",
          fabs(peak_near(wl + SECS(0.1), WIN, C4, 0.05) / C4 - 1) < 0.005);
    for (c = 0; c < 8; c++) {
        double ref, lo = 1e30, off;
        memcpy(k, ESINE, sizeof k);
        k[2] = (int16_t)c;
        hit(DM_EPIANO, k, 127, SECS(0.8));
        ref = goertzel(wl + SECS(0.1), WIN, C4);
        for (j = 0; j < 4; j++)
            if (IV[c][j] >= 0) {
                double m = goertzel(wl + SECS(0.1), WIN, semi(C4, IV[c][j]));
                if (m < lo)
                    lo = m;
            }
        off = goertzel(wl + SECS(0.1), WIN, semi(C4, 1.5));
        ok &= lo > ref / 20 && off < lo / 5;
    }
    check("EPIANO: every CHORD sounds exactly its notes", ok);
}

static void test_ep_tine_trem(void)
{
    int16_t k[8];
    double e0, l0, e1, l1, mn, mx;
    uint32_t i;
    memcpy(k, ESINE, sizeof k);
    k[4] = 127;
    k[5] = 30;
    hit(DM_EPIANO, k, 127, SECS(0.6));
    e1 = hf_ratio(wl, 0, SECS(0.02));
    l1 = hf_ratio(wl, SECS(0.3), SECS(0.5));
    k[4] = 0;
    hit(DM_EPIANO, k, 127, SECS(0.6));
    e0 = hf_ratio(wl, 0, SECS(0.02));
    l0 = hf_ratio(wl, SECS(0.3), SECS(0.5));
    printf("     brightness: TINE 127 early %.3f late %.3f, TINE 0 early %.3f late %.3f\n", e1, l1, e0, l0);
    check("EPIANO: TINE adds a bell at the hit that falls away (early > 5x TINE 0's, and > 3x its own late)",
          e1 > 5 * e0 && e1 > 3 * l1);
    memcpy(k, ESINE, sizeof k);
    k[1] = 120;
    hit(DM_EPIANO, k, 127, SECS(1.6));
    mn = 1e30;
    mx = 0;
    for (i = SECS(0.2); i + 1500 < SECS(0.9); i += 1500) {   /* peak per ~34 ms window, TREM 0: steady */
        double p = peak_abs(wl, i, i + 1500) / exp(-(double)i / FS * 0.4);
        if (p < mn) mn = p;
        if (p > mx) mx = p;
    }
    k[6] = 127;
    hit(DM_EPIANO, k, 127, SECS(1.6));
    {
        double mn2 = 1e30, mx2 = 0;
        for (i = SECS(0.2); i + 1500 < SECS(0.9); i += 1500) {
            double p = peak_abs(wl, i, i + 1500);
            if (p < mn2) mn2 = p;
            if (p > mx2) mx2 = p;
        }
        printf("     level swing over 0.7 s with TREM 127: %.2f : 1 (TREM 0: %.2f : 1 on a decaying note)\n", mx2 / (mn2 + 1), mx / (mn + 1));
        check("EPIANO: TREM 127 wobbles the level (max/min > 2 within 0.7 s)", mx2 / (mn2 + 1) > 2);
    }
}

static void test_extremes_det(void)
{
    static const int16_t E[6][8] = {{0, 127, 127, 7, 127, 127, 127, 127}, {-24, 0, 0, 0, 0, 0, 0, 0},
                                    {24, 127, 127, 3, 100, 127, 127, 0}, {0, 64, 64, 4, 64, 64, 64, 64},
                                    {0, 127, 0, 7, 0, 127, 127, 127}, {12, 90, 100, 5, 0, 0, 0, 100}};
    static const int16_t F[6][8] = {{0, 127, 7, 127, 127, 127, 127, 127}, {-24, 0, 0, 0, 0, 0, 0, 0},
                                    {24, 127, 5, 127, 127, 127, 127, 0}, {0, 64, 4, 64, 64, 64, 64, 64},
                                    {0, 127, 7, 0, 0, 127, 127, 127}, {12, 90, 6, 100, 0, 0, 0, 100}};
    int i, ok = 1;
    double lo = 1e30, hi = 0;
    for (i = 0; i < 6; i++) {
        double p;
        hit(DM_FMBASS, E[i], 127, SECS(7.0));
        p = peak_abs(wl, 0, SECS(7.0));
        ok &= p < 60000;
        lo = p < lo ? p : lo;
        hi = p > hi ? p : hi;
        hit(DM_EPIANO, F[i], 127, SECS(7.0));
        p = peak_abs(wl, 0, SECS(7.0));
        ok &= p < 60000;
        lo = p < lo ? p : lo;
        hi = p > hi ? p : hi;
    }
    printf("     extreme settings (both models): peak %.0f .. %.0f\n", lo, hi);
    check("FMBASS / EPIANO: every extreme setting stays bounded (peak < 60000)", ok);
    {
        static int32_t a[SECS(0.5)];
        hit(DM_FMBASS, E[3], 100, SECS(0.5));
        memcpy(a, wl, sizeof a);
        hit(DM_FMBASS, E[3], 100, SECS(0.5));
        ok = memcmp(a, wl, sizeof a) == 0;
        hit(DM_EPIANO, F[3], 100, SECS(0.5));
        memcpy(a, wl, sizeof a);
        hit(DM_EPIANO, F[3], 100, SECS(0.5));
        check("FMBASS / EPIANO: the same hit twice renders bit for bit the same", ok && memcmp(a, wl, sizeof a) == 0);
    }
}

int main(void)
{
    test_bass_pitch_sub();
    test_bass_index();
    test_bass_drive_decay();
    test_ep_pitch_chords();
    test_ep_tine_trem();
    test_extremes_det();
    printf("house_test: %s\n", fails ? "FAILED" : "all passed");
    return fails != 0;
}
