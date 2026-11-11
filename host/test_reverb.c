#include "acid_reverb.h"
#include "test.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SR 48000
#define FR (2 * ACID_REVERB_FRAME)

static float s_mem[ACID_REVERB_MEM];

static uint32_t s_rnd = 1;
static float noise(void)
{
    s_rnd = s_rnd * 1664525u + 1013904223u;
    return (float)(int32_t)s_rnd * (1.0f / 2147483648.0f);
}

// Прогон: вход in (n сэмплов), нарезка по chunk, спящие куски - нули.
static void run(acid_reverb_t *rv, const float *in, float *l, float *r, int n, int chunk)
{
    for (int i = 0; i < n; i += chunk) {
        int m = n - i < chunk ? n - i : chunk;
        if (!acid_reverb_run(rv, in + i, l + i, r + i, m)) {
            memset(l + i, 0, (size_t)m * sizeof *l);
            memset(r + i, 0, (size_t)m * sizeof *r);
        }
    }
}

static float db(double e)
{
    return (float)(10.0 * log10(e + 1e-30));
}

static double energy(const float *x, int from, int to)
{
    double e = 0;
    for (int i = from; i < to; i++) {
        e += (double)x[i] * x[i];
    }
    return e / (to - from);
}

// --- Эталон: та же схема по одному тику, без кадров и развёрток ---

typedef struct {
    float *b;
    int len, p;
} ref_line_t;

typedef struct {
    ref_line_t ln[10];
    float mem[ACID_REVERB_MEM];
    float hp, lp1, lp2, ph1, ph2, krt, damp;
    float f1, f2;
    int d1, d2, tick;
} ref_t;

static const int REF_LEN[10] = {ACID_REVERB_IN1, ACID_REVERB_IN2, ACID_REVERB_IN3, ACID_REVERB_IN4, ACID_REVERB_A1,
                                ACID_REVERB_A2,  ACID_REVERB_D1,  ACID_REVERB_B1,  ACID_REVERB_B2,  ACID_REVERB_D2};

static void ref_init(ref_t *q, int decay, int damp)
{
    memset(q, 0, sizeof *q);
    float *m = q->mem;
    for (int i = 0; i < 10; i++) {
        q->ln[i].b = m;
        q->ln[i].len = REF_LEN[i];
        m += REF_LEN[i];
    }
    q->krt = acid_reverb_krt(decay);
    q->damp = acid_reverb_damp(damp);
}

static float ref_ap(ref_line_t *a, float x)
{
    float d = a->b[a->p];
    float t = x - ACID_REVERB_AP * d;
    a->b[a->p] = t;
    a->p = (a->p + 1) % a->len;
    return d + ACID_REVERB_AP * t;
}

static void ref_mod(float *ph, float rate, int len, int *di, float *fr)
{
    float tri = *ph < 0.5f ? 2.0f * *ph : 2.0f - 2.0f * *ph;
    float s = tri * tri * (3.0f - 2.0f * tri);
    float d = (float)(len - 18) - ACID_REVERB_MOD * s;
    *di = (int)d;
    *fr = d - (float)*di;
    *ph += rate;
    *ph -= *ph >= 1.0f ? 1.0f : 0.0f;
}

static float ref_tap(const ref_line_t *a, int di, float fr)
{
    float na = a->b[((a->p - di) % a->len + a->len) % a->len];
    float nb = a->b[((a->p - di - 1) % a->len + a->len) % a->len];
    return na + fr * (nb - na);
}

static void ref_tick(ref_t *q, float in, float *yl, float *yr)
{
    if (q->tick % ACID_REVERB_FRAME == 0) { // плавание пересчитывается раз в кадр
        ref_mod(&q->ph1, ACID_REVERB_LFO1, ACID_REVERB_D1, &q->d1, &q->f1);
        ref_mod(&q->ph2, ACID_REVERB_LFO2, ACID_REVERB_D2, &q->d2, &q->f2);
    }
    q->tick++;
    float x = in - q->hp;
    q->hp = q->hp * (1.0f - ACID_REVERB_LOWCUT) + ACID_REVERB_LOWCUT * in;
    for (int i = 0; i < 4; i++) {
        x = ref_ap(&q->ln[i], x);
    }
    float r1 = ref_tap(&q->ln[6], q->d1, q->f1), r2 = ref_tap(&q->ln[9], q->d2, q->f2);
    q->lp1 = q->lp1 * (1.0f - q->damp) + q->damp * (x + q->krt * r2);
    q->lp2 = q->lp2 * (1.0f - q->damp) + q->damp * (x + q->krt * r1);
    float v1 = ref_ap(&q->ln[5], ref_ap(&q->ln[4], q->lp1));
    float v2 = ref_ap(&q->ln[8], ref_ap(&q->ln[7], q->lp2));
    q->ln[6].b[q->ln[6].p] = v1;
    q->ln[6].p = (q->ln[6].p + 1) % q->ln[6].len;
    q->ln[9].b[q->ln[9].p] = v2;
    q->ln[9].p = (q->ln[9].p + 1) % q->ln[9].len;
    *yl = v1 * ACID_REVERB_GAIN;
    *yr = v2 * ACID_REVERB_GAIN;
}

TEST(test_silence_sleeps)
{
    acid_reverb_t rv;
    acid_reverb_init(&rv, s_mem);
    float z[96] = {0}, l[96], r[96];
    CHECK(!acid_reverb_active(&rv));
    CHECK(acid_reverb_run(&rv, z, l, r, 96) == 0);
    CHECK(!acid_reverb_active(&rv));
}

TEST(test_matches_reference)
{
    enum { N = SR * 3 };
    float *in = calloc(N, sizeof *in), *l = malloc(N * sizeof *l), *r = malloc(N * sizeof *r);
    for (int i = 0; i < SR / 2; i++) {
        in[i] = noise() * 0.5f;
    }
    acid_reverb_t rv;
    acid_reverb_init(&rv, s_mem);
    acid_reverb_set(&rv, 90, 30);
    run(&rv, in, l, r, N, 32);
    static ref_t q;
    ref_init(&q, 90, 30);
    float pl = 0, pr = 0;
    double err = 0, sig = 0;
    for (int k = 0; k + 1 < (N - FR) / 2; k++) {
        float yl, yr;
        ref_tick(&q, 0.5f * (in[2 * k] + in[2 * k + 1]), &yl, &yr);
        int s = FR + 2 * k; // опоздание на кадр
        double e0 = l[s] - 0.5f * (pl + yl), e1 = l[s + 1] - yl, e2 = r[s] - 0.5f * (pr + yr), e3 = r[s + 1] - yr;
        err += e0 * e0 + e1 * e1 + e2 * e2 + e3 * e3;
        sig += 2.0 * ((double)yl * yl + (double)yr * yr);
        pl = yl;
        pr = yr;
    }
    CHECK(sig > 1.0);
    CHECK(db(err / sig) < -100.0f);
    for (int i = 0; i < FR; i++) {
        CHECK(l[i] == 0.0f && r[i] == 0.0f);
    }
    free(in), free(l), free(r);
}

TEST(test_chunking_bitexact)
{
    enum { N = SR };
    static float in[N], l[N], r[N], l2[N], r2[N];
    for (int i = 0; i < N; i++) {
        in[i] = i < 9000 ? noise() : 0.0f;
    }
    static const int chunks[] = {1, 7, 31, 33, 96, 500};
    acid_reverb_t rv;
    acid_reverb_init(&rv, s_mem);
    run(&rv, in, l, r, N, 32);
    CHECK(energy(l, 20000, N) > 1e-6);
    for (unsigned c = 0; c < sizeof chunks / sizeof chunks[0]; c++) {
        acid_reverb_init(&rv, s_mem);
        run(&rv, in, l2, r2, N, chunks[c]);
        CHECK(memcmp(l, l2, sizeof l) == 0);
        CHECK(memcmp(r, r2, sizeof r) == 0);
    }
}

// Спад хвоста: шум 50 мс ниже ~1 кГц (верха петля гасит быстрее при любом демпфере: ФНЧ и дробное чтение),
// энергия окон 0.3-0.5 с и 1.3-1.5 с.
static float tail_drop_db(int decay, int damp)
{
    enum { N = SR * 2 };
    static float in[N], l[N], r[N];
    float lp = 0;
    for (int i = 0; i < N; i++) {
        lp += 0.12f * (noise() - lp);
        in[i] = i < 2400 ? lp : 0.0f;
    }
    acid_reverb_t rv;
    acid_reverb_init(&rv, s_mem);
    acid_reverb_set(&rv, decay, damp);
    run(&rv, in, l, r, N, 32);
    return db(energy(l, 14400, 24000)) - db(energy(l, 62400, 72000));
}

TEST(test_decay_knob_sets_rt60)
{
    // RT60 = 0.3 * 100^(knob/127): спад за секунду 60 / RT60 дБ.
    float d64 = tail_drop_db(64, 0), d96 = tail_drop_db(96, 0), d127 = tail_drop_db(127, 0);
    CHECK_NEAR(d64, 60.0f / 3.05f, 7.0f);
    CHECK_NEAR(d96, 60.0f / 9.75f, 2.5f);
    CHECK_NEAR(d127, 2.0f, 1.5f);
    CHECK(d127 > 0.3f); // и на максимуме гаснет
}

TEST(test_stereo_decorrelated_and_balanced)
{
    enum { N = SR };
    static float in[N], l[N], r[N];
    for (int i = 0; i < N; i++) {
        in[i] = i < 4800 ? noise() : 0.0f;
    }
    acid_reverb_t rv;
    acid_reverb_init(&rv, s_mem);
    run(&rv, in, l, r, N, 32);
    double el = energy(l, 9600, N), er = energy(r, 9600, N), x = 0;
    for (int i = 9600; i < N; i++) {
        x += (double)l[i] * r[i];
    }
    x /= (N - 9600);
    CHECK_NEAR(db(el) - db(er), 0.0f, 3.0f);
    CHECK(fabs(x) / sqrt(el * er) < 0.2);
}

// Доля энергии разностей соседних сэмплов: мера верхов.
static float brightness(int damp)
{
    enum { N = SR };
    static float in[N], l[N], r[N];
    for (int i = 0; i < N; i++) {
        in[i] = i < 2400 ? noise() : 0.0f;
    }
    acid_reverb_t rv;
    acid_reverb_init(&rv, s_mem);
    acid_reverb_set(&rv, 100, damp);
    run(&rv, in, l, r, N, 32);
    double d = 0, e = 0;
    for (int i = 14400; i < N; i++) {
        d += (double)(l[i] - l[i - 1]) * (l[i] - l[i - 1]);
        e += (double)l[i] * l[i];
    }
    return db(d / e);
}

TEST(test_damp_darkens_tail)
{
    float b0 = brightness(0), b64 = brightness(64), b127 = brightness(127);
    CHECK(b0 > b64 + 3.0f);
    CHECK(b64 > b127 + 3.0f);
}

TEST(test_stable_on_full_scale_at_max_decay)
{
    enum { N = SR * 10 };
    float *in = malloc(N * sizeof *in), *l = malloc(N * sizeof *l), *r = malloc(N * sizeof *r);
    for (int i = 0; i < N; i++) {
        in[i] = noise();
    }
    acid_reverb_t rv;
    acid_reverb_init(&rv, s_mem);
    acid_reverb_set(&rv, 127, 0);
    run(&rv, in, l, r, N, 32);
    float peak = 0;
    for (int i = 0; i < N; i++) {
        CHECK(l[i] == l[i]);
        peak = fabsf(l[i]) > peak ? fabsf(l[i]) : peak;
        peak = fabsf(r[i]) > peak ? fabsf(r[i]) : peak;
    }
    CHECK(peak < 30.0f);
    CHECK(peak > 0.5f);
    free(in), free(l), free(r);
}

TEST(test_dc_does_not_pass)
{
    enum { N = SR * 2 };
    static float in[N], l[N], r[N];
    for (int i = 0; i < N; i++) {
        in[i] = 0.5f;
    }
    acid_reverb_t rv;
    acid_reverb_init(&rv, s_mem);
    acid_reverb_set(&rv, 40, 64);
    run(&rv, in, l, r, N, 32);
    CHECK(db(energy(l, N - 4800, N)) < -80.0f);
}

TEST(test_sleeps_after_tail_and_wakes_clean)
{
    enum { N = SR * 6 };
    float *in = calloc(N, sizeof *in), *l = malloc(N * sizeof *l), *r = malloc(N * sizeof *r);
    float *l2 = malloc(N * sizeof *l), *r2 = malloc(N * sizeof *r);
    for (int i = 0; i < 2400; i++) {
        in[i] = noise();
    }
    acid_reverb_t rv;
    acid_reverb_init(&rv, s_mem);
    acid_reverb_set(&rv, 20, 64); // RT60 ~0.6 с
    run(&rv, in, l, r, N, 32);
    CHECK(energy(l, 4800, 9600) > 1e-5);
    CHECK(!acid_reverb_active(&rv));
    // перед сном хвост уже не слышен: ниже -100 дБ
    float last = 0;
    for (int i = N - 1; i > 0 && l[i] == 0.0f; i--) {
        last = fabsf(l[i - 1]);
    }
    CHECK(last < 1e-5f);
    // второй удар сразу после сна звучит как первый: память чистая, фазы с нуля
    run(&rv, in, l2, r2, N, 32);
    CHECK(memcmp(l, l2, N * sizeof *l) == 0);
    CHECK(memcmp(r, r2, N * sizeof *r) == 0);
    // и удар в момент засыпания (чистка не закончена) - тоже
    acid_reverb_init(&rv, s_mem);
    acid_reverb_set(&rv, 20, 64);
    int at = 0;
    static float z[32], tl[32], tr[32];
    run(&rv, in, l2, r2, 4800, 32);
    while (acid_reverb_run(&rv, z, tl, tr, 32)) {
        at++;
    }
    CHECK(at > 100);
    run(&rv, in, l2, r2, N, 32);
    CHECK(memcmp(l, l2, N * sizeof *l) == 0);
    free(in), free(l), free(r), free(l2), free(r2);
}

TEST(test_negative_zero_is_silence)
{
    acid_reverb_t rv;
    acid_reverb_init(&rv, s_mem);
    float z[64], l[64], r[64];
    for (int i = 0; i < 64; i++) {
        z[i] = -0.0f;
    }
    CHECK(acid_reverb_run(&rv, z, l, r, 64) == 0);
}

TEST(test_knob_curves)
{
    CHECK(acid_reverb_krt(0) > 0.0f && acid_reverb_krt(0) < 0.05f);
    CHECK(acid_reverb_krt(127) < 0.995f && acid_reverb_krt(127) > 0.95f);
    for (int k = 1; k < 128; k++) {
        CHECK(acid_reverb_krt(k) > acid_reverb_krt(k - 1));
        CHECK(acid_reverb_damp(k) < acid_reverb_damp(k - 1));
    }
    CHECK(acid_reverb_damp(0) < 1.0f && acid_reverb_damp(127) > 0.05f);
}

int main(void)
{
    RUN(test_silence_sleeps);
    RUN(test_matches_reference);
    RUN(test_chunking_bitexact);
    RUN(test_decay_knob_sets_rt60);
    RUN(test_stereo_decorrelated_and_balanced);
    RUN(test_damp_darkens_tail);
    RUN(test_stable_on_full_scale_at_max_decay);
    RUN(test_dc_does_not_pass);
    RUN(test_sleeps_after_tail_and_wakes_clean);
    RUN(test_negative_zero_is_silence);
    RUN(test_knob_curves);
    TEST_MAIN_END();
}
