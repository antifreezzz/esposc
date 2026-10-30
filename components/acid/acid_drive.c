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
#define HB0 0.30680044f
#define HB1 -0.07615580f
#define HB2 0.02337306f
#define HB3 -0.00401770f

static float expo(int v, float a, float b)
{
    return a * powf(b / a, (float)v / 127.0f);
}

// Коэффициенты TPT SVF: деление только тут, на управляющей частоте или при повороте ручки.
static void svf_set(acid_svf_t *f, float g, float k)
{
    f->g = g;
    f->k = k;
    f->a1 = 1.0f / (1.0f + g * (g + k));
    f->a2 = g * f->a1;
    f->a3 = g * f->a2;
}

// Сэмпл SVF с готовыми коэффициентами, на месте: вызов функции из другого файла на каждый сэмпл стоил дорого.
// Возвращает полосу, в *lp - ФНЧ; ФВЧ = in - k * полоса - ФНЧ.
static inline float svf_run(acid_svf_t *f, float in, float *lp)
{
    float v3 = in - f->ic2;
    float v1 = f->a1 * f->ic1 + f->a2 * v3;
    float v2 = f->ic2 + f->a2 * f->ic1 + f->a3 * v3;
    f->ic1 = 2.0f * v1 - f->ic1;
    f->ic2 = 2.0f * v2 - f->ic2;
    *lp = v2;
    return v1;
}

static inline float svf_hp(acid_svf_t *f, float in)
{
    float lp, bp = svf_run(f, in, &lp);
    return in - f->k * bp - lp;
}

// Мягкий ограничитель: x - 4/27 x^3, ровно +-1 за |x| >= 1.5, наклон 1 в нуле.
static inline float clip(float x)
{
    x = x > 1.5f ? 1.5f : (x < -1.5f ? -1.5f : x);
    return x - 0.14814815f * x * x * x;
}

void acid_drive_init(acid_drive_t *d, float sr)
{
    memset(d, 0, sizeof *d);
    d->sr = sr;
    float g_x = tanf(PI_F * XOVER_HZ / sr);
    svf_set(&d->xover, g_x, XOVER_K);
    svf_set(&d->sub_lp, g_x, XOVER_K);
    svf_set(&d->post_hp, g_x, XOVER_K);
    d->env_rel = expf(-1.0f / (ENV_S * sr));
    d->c_bias = clip(BIAS);
    acid_drive_set(d, 0, 64, 0);
    acid_drive_pitch(d, 110.0f);
}

void acid_drive_set(acid_drive_t *d, int drive, int tone, int howl)
{
    d->drive = drive;
    d->tone = tone;
    d->howl = howl;
    d->pre = expo(drive, 0.5f, 10.0f); // обертоны растут по всему ходу, справа почти меандр
    d->post = expo(drive, 1.0f, 6.0f);
    d->makeup = 0.35f + 1.5f * expf(-(float)drive / 20.0f); // громкость ровная по ходу ручки (замер на синусе)
    d->harm = expo(tone, 1.0f, 16.0f);
    // Усиление петли на частоте тона: howl_g / BP_K (пик полосового 1/k); правый край - LOOP_MAX.
    d->howl_g = (float)howl / 127.0f * LOOP_MAX * BP_K;
}

void acid_drive_pitch(acid_drive_t *d, float note_hz)
{
    float f = note_hz * d->harm, max_f = 0.45f * d->sr;
    f = f > max_f ? max_f : f;
    float g = acid_tan(PI_F * f / (2.0f * d->sr)); // полосовой работает на двойной частоте
    if (g != d->bp.g) {
        svf_set(&d->bp, g, BP_K);
    }
}

// Обе ступени на двойной частоте. Петля воя замкнута вокруг полосового через свой ограничитель, а не через
// вторую ступень: в насыщении у той наклон ~0 и свист бы не завёлся. Полосовой с петлёй - генератор на тоне,
// его раскачивает первая ступень, амплитуду держит ограничитель. Первая ступень - тот же кубический
// ограничитель со смещением (tanh через обратную величину стоил вдвое дороже).
static inline float stages(acid_drive_t *d, float u, float howl)
{
    float a = clip(d->pre * u + BIAS) - d->c_bias;
    float lp, bp = svf_run(&d->bp, a + howl * d->fb, &lp);
    d->fb = clip(bp);
    return clip((a + PEAK * bp) * d->post);
}

float acid_drive_run(acid_drive_t *d, float x)
{
    // Раздел Линквица-Райли 4-го порядка: саб - два ФНЧ, верх - ФВЧ до перегруза и ФВЧ после. Без перегруза
    // сумма ровная по громкости, а искажённые остатки саба срезает второй ФВЧ.
    float lp1, bp1 = svf_run(&d->xover, x, &lp1);
    float hi = x - XOVER_K * bp1 - lp1;
    float sub;
    svf_run(&d->sub_lp, lp1, &sub);

    float ax = x < 0.0f ? -x : x;
    d->env = ax > d->env ? ax : d->env * d->env_rel;
    float gate = d->env * ENV_K;
    gate = gate > 1.0f ? 1.0f : gate;
    float howl = d->howl_g * gate;

    // История кольцом удвоенной длины: окно из 8 подряд без сдвига массива.
    int i = d->pos;
    d->sub_d[i] = sub; // саб ждёт верх: оверсемплинг задерживает его на 7 сэмплов
    sub = d->sub_d[(i + 1) & (ACID_DRIVE_OS_HIST - 1)];
    d->pos = (i + 1) & (ACID_DRIVE_OS_HIST - 1);
    float *u = d->up + i + 1, *o = d->dn + i + 1; // u[0..7] - от старого к новому
    d->up[i] = d->up[i + ACID_DRIVE_OS_HIST] = hi;

    // Вверх полуполосным (задержка 4 сэмпла): чётный - сам сэмпл, нечётный - середина.
    float even = stages(d, u[3], howl);
    float mid = 2.0f * (HB0 * (u[3] + u[4]) + HB1 * (u[2] + u[5]) + HB2 * (u[1] + u[6]) + HB3 * (u[0] + u[7]));
    d->dn[i] = d->dn[i + ACID_DRIVE_OS_HIST] = stages(d, mid, howl);
    // Вниз полуполосным (ещё 3 сэмпла): чётный из линии задержки, нечётные соседи.
    float ec = d->dn_even[i & 3];
    d->dn_even[(i + 3) & 3] = even;
    float y = 0.5f * ec + HB0 * (o[4] + o[3]) + HB1 * (o[5] + o[2]) + HB2 * (o[6] + o[1]) + HB3 * (o[7] + o[0]);

    // Второй ФВЧ раздела: срезает то, что перегруз сделал из остатков саба, и постоянку асимметрии.
    return sub + d->makeup * svf_hp(&d->post_hp, y);
}
