#include "acid_reverb.h"

#include <string.h>

#include "acid_math.h"

#define NF ACID_REVERB_FRAME
#define G ACID_REVERB_AP
#define QUIET_FRAMES 750  // 0.5 с тишины на входе и в петле - сон
#define QUIET_LEVEL 1e-6f // -120 дБ: в петле и на входе
#define CLEAR_SLAB 1024   // float за вызов: чистка памяти после засыпания размазана

static const int16_t LEN[10] = {ACID_REVERB_IN1, ACID_REVERB_IN2, ACID_REVERB_IN3, ACID_REVERB_IN4, ACID_REVERB_A1,
                                ACID_REVERB_A2,  ACID_REVERB_D1,  ACID_REVERB_B1,  ACID_REVERB_B2,  ACID_REVERB_D2};

float acid_reverb_krt(int k)
{
    // RT60 = 0.3 * 100^(k/127) с; пол-петли 0.16 с: krt = 10^(-3 * 0.16 / RT60).
    float inv_rt = 3.3333333f * acid_exp2(-0.052314f * (float)k);
    return acid_exp2(-1.592f * inv_rt);
}

float acid_reverb_damp(int k)
{
    // Срез 10 кГц * 2^(-4.32 k/127), коэффициент 1 - exp(-2 pi f / 24000).
    return 1.0f - acid_exp2(-3.777f * acid_exp2(-0.034016f * (float)k));
}

// Всё, кроме памяти линий: с этого состояния ревёрб звучит одинаково после включения и после сна.
static void reset(acid_reverb_t *rv)
{
    memset(rv->p, 0, sizeof rv->p);
    rv->hp = rv->lp1 = rv->lp2 = 0.0f;
    rv->ph1 = rv->ph2 = 0.0f;
    memset(rv->y, 0, sizeof rv->y);
    rv->ysrc[0] = rv->y[0];
    rv->ysrc[1] = rv->y[1];
    rv->yp[0] = rv->yp[1] = 0.0f;
    memset(rv->in, 0, sizeof rv->in);
    rv->hold = 0.0f;
    rv->quiet = 0;
}

void acid_reverb_init(acid_reverb_t *rv, float *mem)
{
    memset(mem, 0, ACID_REVERB_MEM * sizeof *mem);
    for (int i = 0; i < 10; i++) {
        rv->line[i] = mem;
        mem += LEN[i];
    }
    reset(rv);
    rv->pos = 0;
    rv->state = 0;
    rv->clr = 0;
    acid_reverb_set(rv, 64, 50);
}

void acid_reverb_set(acid_reverb_t *rv, int decay_knob, int damp_knob)
{
    rv->decay_knob = decay_knob < 0 ? 0 : decay_knob > 127 ? 127 : decay_knob;
    rv->damp_knob = damp_knob < 0 ? 0 : damp_knob > 127 ? 127 : damp_knob;
    rv->krt = acid_reverb_krt(rv->decay_knob);
    rv->damp = acid_reverb_damp(rv->damp_knob);
}

int acid_reverb_active(const acid_reverb_t *rv)
{
    return rv->state == 1;
}

// Allpass на кадр: ячейка читается и тут же пишется, линия длиной ровно в задержку. Кадр короче любой линии,
// так что тики кадра друг от друга не зависят: считаются по 4, зависимые операции FPU не стоят друг за другом
// (замер на плате: 6.7 такта на тик против 13.4 у цикла по одному; слитые в один цикл 4 allpass - хуже).
// Вход x, выход o (можно тот же массив).
static inline __attribute__((always_inline)) void ap_frame(float *buf, int len, int *pp, const float *x, float *o)
{
    int p = *pp;
    if (p + NF <= len) {
        float *b = buf + p;
        for (int i = 0; i < NF; i += 4) {
            float d0 = b[i], d1 = b[i + 1], d2 = b[i + 2], d3 = b[i + 3];
            float t0 = x[i] - G * d0, t1 = x[i + 1] - G * d1, t2 = x[i + 2] - G * d2, t3 = x[i + 3] - G * d3;
            b[i] = t0, b[i + 1] = t1, b[i + 2] = t2, b[i + 3] = t3;
            o[i] = d0 + G * t0, o[i + 1] = d1 + G * t1, o[i + 2] = d2 + G * t2, o[i + 3] = d3 + G * t3;
        }
        p += NF;
        *pp = p == len ? 0 : p;
        return;
    }
    for (int i = 0; i < NF; i += 4) { // кадр на шве линии: по одному идёт только четвёрка на самом шве
        if (p + 4 <= len) {
            float *b = buf + p;
            float d0 = b[0], d1 = b[1], d2 = b[2], d3 = b[3];
            float t0 = x[i] - G * d0, t1 = x[i + 1] - G * d1, t2 = x[i + 2] - G * d2, t3 = x[i + 3] - G * d3;
            b[0] = t0, b[1] = t1, b[2] = t2, b[3] = t3;
            o[i] = d0 + G * t0, o[i + 1] = d1 + G * t1, o[i + 2] = d2 + G * t2, o[i + 3] = d3 + G * t3;
            p = p + 4 == len ? 0 : p + 4;
            continue;
        }
        for (int k = i; k < i + 4; k++) {
            float d = buf[p];
            float t = x[k] - G * d;
            buf[p] = t;
            o[k] = d + G * t;
            p = p + 1 == len ? 0 : p + 1;
        }
    }
    *pp = p;
}

// Плавание задержки: треугольник со сглаженными вершинами, раз на кадр.
static inline void mod_step(float *ph, float rate, int len, int *di, float *fr)
{
    float tri = *ph < 0.5f ? 2.0f * *ph : 2.0f - 2.0f * *ph;
    float s = tri * tri * (3.0f - 2.0f * tri);
    float d = (float)(len - 18) - ACID_REVERB_MOD * s;
    *di = (int)d;
    *fr = d - (float)*di;
    *ph += rate;
    *ph -= *ph >= 1.0f ? 1.0f : 0.0f;
}

// Чтение линии на кадр с задержкой di + fr тиков (до записи кадра): 16 <= di, di + 1 <= len - 16.
static inline __attribute__((always_inline)) void tap_frame(const float *buf, int len, int p, int di, float fr,
                                                            float *r)
{
    int i0 = p - di - 1;
    i0 += i0 < 0 ? len : 0;
    if (i0 + NF + 1 <= len) {
        const float *b = buf + i0;
        float n0 = b[0];
        for (int i = 0; i < NF; i += 4) {
            float n1 = b[i + 1], n2 = b[i + 2], n3 = b[i + 3], n4 = b[i + 4];
            float e0 = n0 - n1, e1 = n1 - n2, e2 = n2 - n3, e3 = n3 - n4;
            r[i] = n1 + fr * e0, r[i + 1] = n2 + fr * e1, r[i + 2] = n3 + fr * e2, r[i + 3] = n4 + fr * e3;
            n0 = n4;
        }
        return;
    }
    for (int i = 0; i < NF; i++) {
        int ib = i0 + i, ia = ib + 1;
        ib -= ib >= len ? len : 0;
        ia -= ia >= len ? len : 0;
        r[i] = buf[ia] + fr * (buf[ib] - buf[ia]);
    }
}

// Последний allpass ветки и запись в линию: вне шва выход пишется прямо в линию и оттуда же потом читается
// как выход кадра - без копий. Возврат - где лежит выход кадра.
static inline __attribute__((always_inline)) const float *ap_put_frame(float *ap, int ap_len, int *ap_p, float *dl,
                                                                       int dl_len, int *dl_p, const float *x,
                                                                       float *spare)
{
    int p = *dl_p;
    if (p + NF <= dl_len) {
        ap_frame(ap, ap_len, ap_p, x, dl + p);
        *dl_p = p + NF == dl_len ? 0 : p + NF;
        return dl + p;
    }
    ap_frame(ap, ap_len, ap_p, x, spare);
    for (int i = 0; i < NF; i++) {
        dl[p] = spare[i];
        p = p + 1 == dl_len ? 0 : p + 1;
    }
    *dl_p = p;
    return spare;
}

static void sleep_now(acid_reverb_t *rv)
{
    reset(rv);
    rv->state = 2;
    rv->clr = 0;
}

// Кадр: rv->in (16 тиков) -> rv->ysrc.
static __attribute__((noinline)) void frame(acid_reverb_t *rv)
{
    float x[NF], a[NF], b[NF], r1[NF], r2[NF];
    float **ln = rv->line;
    int *p = rv->p;
    const float *x0 = rv->in; // уже после ФВЧ
    ap_frame(ln[0], ACID_REVERB_IN1, &p[0], x0, x);
    ap_frame(ln[1], ACID_REVERB_IN2, &p[1], x, x);
    ap_frame(ln[2], ACID_REVERB_IN3, &p[2], x, x);
    ap_frame(ln[3], ACID_REVERB_IN4, &p[3], x, x);
    int di;
    float fr;
    mod_step(&rv->ph1, ACID_REVERB_LFO1, ACID_REVERB_D1, &di, &fr);
    tap_frame(ln[6], ACID_REVERB_D1, p[6], di, fr, r1);
    mod_step(&rv->ph2, ACID_REVERB_LFO2, ACID_REVERB_D2, &di, &fr);
    tap_frame(ln[9], ACID_REVERB_D2, p[9], di, fr, r2);
    {
        // Восьмёрка: в ветку 1 идёт хвост линии 2 и наоборот. ФНЧ lp = lp * c1 + c * (x + krt * r) развёрнут на
        // 2 тика: по цепочке от кадра к кадру идёт одно умножение-сложение на пару тиков, остальное в стороне.
        float lp1 = rv->lp1, lp2 = rv->lp2;
        const float c = rv->damp, ck = rv->damp * rv->krt, c1 = 1.0f - rv->damp, c2 = c1 * c1;
        for (int i = 0; i < NF; i += 2) {
            float cx0 = c * x[i], cx1 = c * x[i + 1];
            float v10 = cx0 + ck * r2[i], v20 = cx0 + ck * r1[i];
            float v11 = cx1 + ck * r2[i + 1], v21 = cx1 + ck * r1[i + 1];
            float u1 = v11 + c1 * v10, u2 = v21 + c1 * v20;
            float a0 = v10 + c1 * lp1, b0 = v20 + c1 * lp2;
            lp1 = u1 + c2 * lp1, lp2 = u2 + c2 * lp2;
            a[i] = a0, b[i] = b0;
            a[i + 1] = lp1, b[i + 1] = lp2;
        }
        rv->lp1 = lp1;
        rv->lp2 = lp2;
    }
    ap_frame(ln[4], ACID_REVERB_A1, &p[4], a, a);
    ap_frame(ln[7], ACID_REVERB_B1, &p[7], b, b);
    const float *ya = ap_put_frame(ln[5], ACID_REVERB_A2, &p[5], ln[6], ACID_REVERB_D1, &p[6], a, rv->y[0]);
    const float *yb = ap_put_frame(ln[8], ACID_REVERB_B2, &p[8], ln[9], ACID_REVERB_D2, &p[9], b, rv->y[1]);
    rv->ysrc[0] = ya;
    rv->ysrc[1] = yb;
    float ta = ya[NF - 1], tb = yb[NF - 1];
    float tail = (ta < 0.0f ? -ta : ta) + (tb < 0.0f ? -tb : tb);
    if (tail >= QUIET_LEVEL) {
        rv->quiet = 0;
        return;
    }
    for (int i = 0; i < NF; i++) { // вход смотрим, только когда петля уже тихая
        if (rv->in[i] >= QUIET_LEVEL || rv->in[i] <= -QUIET_LEVEL) {
            rv->quiet = 0;
            return;
        }
    }
    if (++rv->quiet >= QUIET_FRAMES) {
        sleep_now(rv);
    }
}

// Чистка памяти линий после засыпания: всё оставшееся или один ломоть.
static void clear_some(acid_reverb_t *rv, int all)
{
    int n = ACID_REVERB_MEM - rv->clr;
    n = !all && n > CLEAR_SLAB ? CLEAR_SLAB : n;
    memset(rv->line[0] + rv->clr, 0, (size_t)n * sizeof(float));
    rv->clr += n;
    if (rv->clr == ACID_REVERB_MEM) {
        rv->state = 0;
    }
}

int acid_reverb_run(acid_reverb_t *rv, const float *send, float *wl, float *wr, int n)
{
    int i = 0, any = rv->state == 1;
    if (rv->state == 2) {
        clear_some(rv, 0);
    }
    while (i < n) {
        if (rv->state != 1) {
            int j = i;
            while (j < n && send[j] < QUIET_LEVEL && send[j] > -QUIET_LEVEL) { // тише -120 дБ не будит
                j++;
            }
            if (j == n && !any) {
                rv->pos = (rv->pos + n) & (2 * NF - 1);
                return 0;
            }
            // тишина до j: сетка кадров идёт дальше, выход - нули
            memset(wl + i, 0, (size_t)(j - i) * sizeof *wl);
            memset(wr + i, 0, (size_t)(j - i) * sizeof *wr);
            rv->pos = (rv->pos + (j - i)) & (2 * NF - 1);
            i = j;
            if (j == n) {
                break;
            }
            if (rv->state == 2) {
                clear_some(rv, 1);
            }
            rv->state = 1;
            any = 1;
        }
        // Громкость wet и половина пары сложены во вход: нечётный сэмпл выхода - копия тика без умножения.
        const float gi = 0.5f * ACID_REVERB_GAIN, c = ACID_REVERB_LOWCUT, c1 = 1.0f - ACID_REVERB_LOWCUT;
        if (rv->pos == 0 && n - i >= 2 * NF) {
            // целый кадр: выход прошлого кадра с серединами, вход парами и через ФВЧ; по 2 тика, сначала чтения
            const float *sl = rv->ysrc[0], *sr = rv->ysrc[1], *s = send + i;
            float *ol = wl + i, *or_ = wr + i, *in = rv->in;
            float pl = rv->yp[0], pr = rv->yp[1], hp = rv->hp;
            for (int k = 0; k < NF; k += 2) {
                float l0 = sl[k], l1 = sl[k + 1], r0 = sr[k], r1 = sr[k + 1];
                float s0 = s[2 * k], s1 = s[2 * k + 1], s2 = s[2 * k + 2], s3 = s[2 * k + 3];
                float ml0 = pl + l0, mr0 = pr + r0, ml1 = l0 + l1, mr1 = r0 + r1;
                s0 += s1, s2 += s3;
                ol[2 * k + 1] = l0, or_[2 * k + 1] = r0;
                ol[2 * k + 3] = l1, or_[2 * k + 3] = r1;
                s0 *= gi, s2 *= gi;
                ol[2 * k] = 0.5f * ml0, or_[2 * k] = 0.5f * mr0;
                ol[2 * k + 2] = 0.5f * ml1, or_[2 * k + 2] = 0.5f * mr1;
                float h1 = hp * c1 + c * s0;
                in[k] = s0 - hp;
                hp = h1 * c1 + c * s2;
                in[k + 1] = s2 - h1;
                pl = l1, pr = r1;
            }
            rv->yp[0] = pl;
            rv->yp[1] = pr;
            rv->hp = hp;
            i += 2 * NF;
            frame(rv);
            continue;
        }
        int k = rv->pos >> 1;
        float yl = rv->ysrc[0][k], yr = rv->ysrc[1][k];
        if (rv->pos & 1) {
            wl[i] = yl;
            wr[i] = yr;
            rv->yp[0] = yl;
            rv->yp[1] = yr;
            float t = gi * (rv->hold + send[i]);
            rv->in[k] = t - rv->hp;
            rv->hp = rv->hp * c1 + c * t;
        } else {
            wl[i] = 0.5f * (rv->yp[0] + yl);
            wr[i] = 0.5f * (rv->yp[1] + yr);
            rv->hold = send[i];
        }
        i++;
        if (++rv->pos == 2 * NF) {
            rv->pos = 0;
            frame(rv);
        }
    }
    return 1;
}
