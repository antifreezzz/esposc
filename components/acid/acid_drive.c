#include "acid_drive.h"

#include <math.h>
#include <string.h>

#include "acid_math.h"

#define PI_F 3.14159265f
#define XOVER_HZ 120.0f  // ниже - чистый саб
#define XOVER_K 1.41421356f
#define BIAS 0.3f        // асимметрия первой ступени: чётные обертоны, «тёплый» край
#define BP_K 0.35f       // 1/Q полосового тона
#define PEAK 0.5f        // подъём полосы тона в сигнале второй ступени
#define LOOP_MAX 1.6f    // усиление петли воя на правом краю: выше 1 заводится сам
#define ENV_K 20.0f      // вход тише 1/ENV_K - петля воя слабеет
#define ENV_S 0.1f       // спад огибающей входа

// Полуполосный фильтр 2x (15 точек, Кайзер beta 4): нечётные отводы, центр 0.5.
static const float HB[4] = {0.30680044f, -0.07615580f, 0.02337306f, -0.00401770f};

static float expo(int v, float a, float b)
{
    return a * powf(b / a, (float)v / 127.0f);
}

void acid_drive_init(acid_drive_t *d, float sr)
{
    memset(d, 0, sizeof *d);
    d->sr = sr;
    d->g_x = tanf(PI_F * XOVER_HZ / sr);
    d->env_rel = expf(-1.0f / (ENV_S * sr));
    d->t_bias = acid_tanh(BIAS);
    acid_drive_set(d, 0, 64, 0);
    acid_drive_pitch(d, 110.0f);
}

void acid_drive_set(acid_drive_t *d, int drive, int tone, int howl)
{
    d->drive = drive;
    d->tone = tone;
    d->howl = howl;
    d->pre = expo(drive, 1.5f, 40.0f);
    d->post = expo(drive, 1.0f, 8.0f);
    d->makeup = expo(drive, 1.0f, 0.45f);
    d->harm = expo(tone, 1.0f, 16.0f);
    // Усиление петли на частоте тона: howl_g / BP_K (пик полосового 1/k); правый край - LOOP_MAX.
    d->howl_g = (float)howl / 127.0f * LOOP_MAX * BP_K;
}

void acid_drive_pitch(acid_drive_t *d, float note_hz)
{
    float f = note_hz * d->harm, max_f = 0.45f * d->sr;
    f = f > max_f ? max_f : f;
    d->g_bp = acid_tan(PI_F * f / (2.0f * d->sr)); // полосовой работает на двойной частоте
}

// Мягкий ограничитель: x - 4/27 x^3, ровно +-1 за |x| >= 1.5, наклон 1 в нуле.
static inline float clip(float x)
{
    x = x > 1.5f ? 1.5f : (x < -1.5f ? -1.5f : x);
    return x - 0.14814815f * x * x * x;
}

// Обе ступени на двойной частоте. Петля воя замкнута вокруг полосового через свой ограничитель, а не через
// вторую ступень: в насыщении у той наклон ~0 и свист бы не завёлся. Полосовой с петлёй - генератор на тоне,
// его раскачивает первая ступень, амплитуду держит ограничитель.
static inline float stages(acid_drive_t *d, float u, float gate)
{
    float a = acid_tanh(d->pre * u + BIAS) - d->t_bias;
    float bp, bp_hp;
    acid_svf_bp_hp(&d->bp, a + d->howl_g * gate * d->fb, d->g_bp, BP_K, &bp, &bp_hp);
    d->fb = clip(bp);
    return clip((a + PEAK * bp) * d->post);
}

float acid_drive_run(acid_drive_t *d, float x)
{
    float lo_bp, hi;
    acid_svf_bp_hp(&d->xover, x, d->g_x, XOVER_K, &lo_bp, &hi);
    float sub = x - hi;

    float ax = x < 0.0f ? -x : x;
    d->env = ax > d->env ? ax : d->env * d->env_rel;
    float gate = d->env * ENV_K;
    gate = gate > 1.0f ? 1.0f : gate;

    // Вверх полуполосным (задержка 4 сэмпла): чётный - сам сэмпл, нечётный - середина.
    float *u = d->up, *o = d->dn;
    for (int i = 0; i < ACID_DRIVE_OS_HIST - 1; i++) {
        u[i] = u[i + 1];
        o[i] = o[i + 1];
    }
    u[7] = hi;
    float even = stages(d, u[3], gate);
    float mid = 2.0f * (HB[0] * (u[3] + u[4]) + HB[1] * (u[2] + u[5]) + HB[2] * (u[1] + u[6]) + HB[3] * (u[0] + u[7]));
    o[7] = stages(d, mid, gate);
    // Вниз полуполосным (ещё 3 сэмпла): чётный из линии задержки, нечётные соседи.
    float *e = d->dn_even;
    float ec = e[0];
    e[0] = e[1];
    e[1] = e[2];
    e[2] = even;
    float y = 0.5f * ec + HB[0] * (o[4] + o[3]) + HB[1] * (o[5] + o[2]) + HB[2] * (o[6] + o[1]) + HB[3] * (o[7] + o[0]);

    // Срезаем снизу то, что перегруз сделал из остатков саба (и постоянку асимметрии): саб остаётся чистым.
    float post_bp, post_hi;
    acid_svf_bp_hp(&d->post_hp, y, d->g_x, XOVER_K, &post_bp, &post_hi);
    return sub + d->makeup * post_hi;
}
