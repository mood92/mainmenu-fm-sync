/* ORGAN (dm_organ.c): the pitch, the chord notes, the drawbars, DECAY, ATK, VIB, CLICK, the level, the extremes,
 * determinism. Each test prints one line; any failure exits 1. */
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


/* track 0 = ORGAN with knobs k (TUNE DECAY CHORD DRAW CLICK VIB ATK VEL), one hit at vel, frames rendered into wl */
static void org_hit(const int16_t *k, uint32_t vel, uint32_t frames)
{
    uint32_t i;
    host_init();
    old_set_model(&trk[0], DM_ORGAN);
    for (i = 0; i < 8u; i++)
        trk[0].p[P_E0 + i] = k[i];
    drum_hit(&trk[0], vel);
    render_track(&trk[0], wl, frames);
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
/* a pure single note: CHORD 1, DRAW 0, no click, no wobble, a long ring */
static const int16_t SOLO[8] = {0, 100, 0, 0, 0, 0, 0, 0};

static void test_pitch(void)
{
    int16_t k[8];
    memcpy(k, SOLO, sizeof k);
    org_hit(k, 127, SECS(0.8));
    check("ORGAN: a single note at TUNE 0 is C4 (261.6 Hz within 0.5 %)",
          fabs(peak_near(wl + SECS(0.1), WIN, C4, 0.05) / C4 - 1) < 0.005);
    k[0] = 12;
    org_hit(k, 127, SECS(0.8));
    check("ORGAN: TUNE +12 is an octave up", fabs(peak_near(wl + SECS(0.1), WIN, 2 * C4, 0.05) / (2 * C4) - 1) < 0.005);
}

static void test_chords(void)
{
    static const int8_t IV[8][4] = {{0, -1, -1, -1}, {0, 12, -1, -1}, {0, 3, 7, -1}, {0, 4, 7, -1}, {0, 3, 7, 10},
                                    {0, 3, 10, 14}, {0, 4, 7, 11}, {0, 5, 7, 12}};
    int c, j, ok = 1;
    for (c = 0; c < 8; c++) {
        int16_t k[8];
        double ref, lo = 1e30, off;
        memcpy(k, SOLO, sizeof k);
        k[2] = (int16_t)c;
        org_hit(k, 127, SECS(0.8));
        ref = goertzel(wl + SECS(0.1), WIN, C4);
        for (j = 0; j < 4; j++)
            if (IV[c][j] >= 0) {
                double m = goertzel(wl + SECS(0.1), WIN, semi(C4, IV[c][j]));
                if (m < lo)
                    lo = m;
            }
        off = goertzel(wl + SECS(0.1), WIN, semi(C4, 1.5));           /* a quarter-tone-ish spot between notes */
        ok &= lo > ref / 20 && off < lo / 5;
        printf("     CHORD %d: weakest note / root %.2f, off-note / weakest %.3f\n", c, lo / ref, off / lo);
    }
    check("ORGAN: every CHORD sounds exactly its notes (each present, nothing between)", ok);
}

static void test_draw(void)
{
    int16_t k[8];
    double r0, r1;
    memcpy(k, SOLO, sizeof k);
    org_hit(k, 127, SECS(0.8));
    r0 = goertzel(wl + SECS(0.1), WIN, 3 * C4) / goertzel(wl + SECS(0.1), WIN, C4);
    k[3] = 127;
    org_hit(k, 127, SECS(0.8));
    r1 = goertzel(wl + SECS(0.1), WIN, 3 * C4) / goertzel(wl + SECS(0.1), WIN, C4);
    printf("     3rd / 1st: DRAW 0 %.3f, DRAW 127 %.3f\n", r0, r1);
    check("ORGAN: DRAW 127 is far brighter than DRAW 0 (the 3rd partial rises, > 10x)", r1 > 10 * (r0 + 1e-6) && r1 > 0.5);
}

static void test_decay_atk_vib_click(void)
{
    int16_t k[8];
    double a, b;
    memcpy(k, SOLO, sizeof k);
    k[1] = 20;
    org_hit(k, 127, SECS(1.0));
    a = peak_abs(wl, SECS(0.5), SECS(0.6));
    k[1] = 110;
    org_hit(k, 127, SECS(1.0));
    b = peak_abs(wl, SECS(0.5), SECS(0.6));
    check("ORGAN: DECAY 20 is a short stab, DECAY 110 rings on (level at 0.5 s: > 20x)", b > 20 * (a + 1));
    memcpy(k, SOLO, sizeof k);
    org_hit(k, 127, SECS(0.1));
    a = peak_abs(wl, 0, 64);
    k[6] = 127;
    org_hit(k, 127, SECS(0.1));
    b = peak_abs(wl, 0, 64);
    check("ORGAN: ATK 127 fades in (the first 64 samples < 1/4 of ATK 0's)", b * 4 < a && a > 100);
    memcpy(k, SOLO, sizeof k);
    org_hit(k, 127, SECS(0.8));
    a = freq_range(wl, SECS(0.1), SECS(0.7));
    k[5] = 127;
    org_hit(k, 127, SECS(0.8));
    b = freq_range(wl, SECS(0.1), SECS(0.7));
    printf("     pitch range over 0.6 s: VIB 0 %.2f Hz, VIB 127 %.2f Hz (+-0.35 semitone = about 10.6 Hz)\n", a, b);
    check("ORGAN: VIB 0 holds the pitch, VIB 127 wobbles it (+-0.35 semitone)", a < 2 && b > 8 && b < 14 && b > 5 * a);
    memcpy(k, SOLO, sizeof k);
    org_hit(k, 127, SECS(0.05));
    a = hf_ratio(wl, 0, 220);
    k[4] = 127;
    org_hit(k, 127, SECS(0.05));
    b = hf_ratio(wl, 0, 220);
    check("ORGAN: CLICK 127 adds a bright tick at the hit (brightness > 3x)", b > 3 * a);
}

static void test_level_extremes(void)
{
    static const int16_t E[8][8] = {{0, 127, 7, 127, 127, 127, 0, 127}, {-24, 0, 0, 0, 0, 0, 0, 0},
                                    {24, 127, 5, 127, 127, 127, 127, 0}, {0, 64, 4, 64, 64, 64, 64, 64},
                                    {0, 127, 0, 127, 0, 0, 0, 127}, {12, 90, 6, 100, 0, 127, 0, 100},
                                    {-24, 127, 7, 127, 127, 0, 0, 127}, {0, 50, 2, 40, 20, 20, 8, 40}};
    int i, ok = 1, ends = 1;
    double lo = 1e30, hi = 0;
    for (i = 0; i < 8; i++) {
        double p;
        org_hit(E[i], 127, SECS(7.0));
        p = peak_abs(wl, 0, SECS(7.0));
        if (p > hi)
            hi = p;
        if (p < lo)
            lo = p;
        ok &= p < 60000 && !(E[i][1] == 127 && E[i][2] == 7 && p == 0);
        ends &= peak_abs(wl, SECS(6.5), SECS(7.0)) == 0 || E[i][1] > 100;   /* a DECAY past 100 is allowed to still ring */
    }
    printf("     extreme settings: peak %.0f .. %.0f (a K909 hit is about 15000)\n", lo, hi);
    check("ORGAN: every extreme setting stays bounded (peak < 60000) and a short DECAY ends silent", ok && ends);
}

static void test_deterministic(void)
{
    static const int16_t K[8] = {0, 60, 5, 90, 60, 40, 10, 40};
    static int32_t a[SECS(0.5)];
    org_hit(K, 100, SECS(0.5));
    memcpy(a, wl, sizeof a);
    org_hit(K, 100, SECS(0.5));
    check("ORGAN: the same hit twice renders bit for bit the same", memcmp(a, wl, sizeof a) == 0);
}

int main(void)
{
    test_pitch();
    test_chords();
    test_draw();
    test_decay_atk_vib_click();
    test_level_extremes();
    test_deterministic();
    printf("organ_test: %s\n", fails ? "FAILED" : "all passed");
    return fails != 0;
}
