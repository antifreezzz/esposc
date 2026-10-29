#include "acid_fx.h"

#include <math.h>
#include <string.h>

#define PI_F 3.14159265f
#define PUMP_ATTACK_S 0.003f
#define SMOOTH_S 0.005f // сглаживание глубины, среза и резонанса
#define XFADE_S 0.005f  // кроссфейд смены режима фильтра
#define DEAD_LO 62      // мёртвая зона ручки фильтра 62..66
#define DEAD_HI 66
#define LP_LO_HZ 150.0f
#define LP_HI_HZ 18000.0f
#define HP_LO_HZ 20.0f
#define HP_HI_HZ 3000.0f
#define Q_MIN 0.7f
#define Q_MAX 8.0f
#define Q_RAMP 12.0f // щелчков от края мёртвой зоны до полного резонанса

static float one_pole(float sr, float s)
{
    return 1.0f - expf(-1.0f / (s * sr));
}

void acid_pump_init(acid_pump_t *p, float sr)
{
    p->sr = sr;
    p->e = p->target = 0.0f;
    p->depth = p->depth_s = 0.0f;
    p->atk_coef = 1.0f / (PUMP_ATTACK_S * sr); // шаг линейной атаки
    p->smooth = one_pole(sr, SMOOTH_S);
    acid_pump_set_release(p, 0.15f);
}

void acid_pump_set_depth(acid_pump_t *p, float depth)
{
    p->depth = depth;
}

void acid_pump_set_release(acid_pump_t *p, float time_s)
{
    // Время почти полного возврата: за 3.5 постоянных времени огибающая спадает до 3%.
    p->rel_coef = expf(-3.5f / (time_s * p->sr));
}

void acid_pump_hit(acid_pump_t *p, float vel)
{
    if (!acid_pump_active(p)) {
        return; // при глубине 0 удары не копятся: включили пампинг - присед только от нового удара
    }
    // Тихий удар во время возврата не поднимает присед: цель ниже e атаку не запускает.
    p->target = vel > p->target ? vel : p->target;
}

float acid_pump_next(acid_pump_t *p)
{
    if (!acid_pump_active(p)) {
        return 1.0f;
    }
    float d = p->depth - p->depth_s;
    p->depth_s = fabsf(d) < 1e-5f ? p->depth : p->depth_s + d * p->smooth;
    if (p->depth_s == 0.0f && p->depth == 0.0f) {
        p->e = p->target = 0.0f; // глубина доехала до нуля: старый присед забыт
        return 1.0f;
    }
    if (p->target > p->e) {
        p->e += p->atk_coef;
        if (p->e >= p->target) {
            p->e = p->target;
            p->target = 0.0f; // атака дошла: дальше возврат
        }
    } else {
        p->target = 0.0f;
        p->e *= p->rel_coef;
        if (p->e < 1e-6f) {
            p->e = 0.0f; // без хвоста в субнормалах
        }
    }
    return 1.0f - p->depth_s * p->e;
}

int acid_pump_active(const acid_pump_t *p)
{
    return p->depth != 0.0f || p->depth_s != 0.0f;
}

float acid_djf_cutoff_hz(int knob)
{
    if (knob < DEAD_LO) {
        return LP_LO_HZ * powf(LP_HI_HZ / LP_LO_HZ, (float)knob / (DEAD_LO - 1));
    }
    if (knob > DEAD_HI) {
        return HP_LO_HZ * powf(HP_HI_HZ / HP_LO_HZ, (float)(knob - DEAD_HI - 1) / (127 - DEAD_HI - 1));
    }
    return 0.0f;
}

float acid_djf_q(int knob, int res_knob)
{
    float d = knob < DEAD_LO ? (float)(DEAD_LO - 1 - knob) : (knob > DEAD_HI ? (float)(knob - DEAD_HI - 1) : 0.0f);
    float r = d / Q_RAMP < 1.0f ? d / Q_RAMP : 1.0f;
    float q = Q_MIN * powf(Q_MAX / Q_MIN, (float)res_knob / 127.0f);
    return Q_MIN + (q - Q_MIN) * r;
}

static void djf_coefs(acid_djf_t *f)
{
    f->a1 = 1.0f / (1.0f + f->g * (f->g + f->k));
    f->a2 = f->g * f->a1;
    f->a3 = f->g * f->a2;
}

void acid_djf_init(acid_djf_t *f, float sr)
{
    f->sr = sr;
    f->ic1 = f->ic2 = 0.0f;
    f->g = f->g_target = 0.0f;
    f->k = f->k_target = 1.0f / Q_MIN;
    f->mode = f->next_mode = 0;
    f->wet = 0.0f;
    f->smooth = one_pole(sr, SMOOTH_S);
    f->xfade = 1.0f / (XFADE_S * sr);
    f->knob = 64;
    f->res_knob = 0;
    djf_coefs(f);
}

// Новый режим вступает: состояние с нуля, срез сразу в цель (сглаживать не от чего).
static void djf_enter(acid_djf_t *f)
{
    f->mode = f->next_mode;
    f->ic1 = f->ic2 = 0.0f;
    f->g = f->g_target;
    f->k = f->k_target;
    djf_coefs(f);
}

void acid_djf_set(acid_djf_t *f, int knob, int res_knob)
{
    f->knob = knob;
    f->res_knob = res_knob;
    float fc = acid_djf_cutoff_hz(knob);
    f->next_mode = fc == 0.0f ? 0 : (knob < DEAD_LO ? 1 : 2);
    if (fc != 0.0f) {
        f->g_target = tanf(PI_F * fc / f->sr);
        f->k_target = 1.0f / acid_djf_q(knob, res_knob);
    }
    if (f->mode == 0 && f->wet == 0.0f && f->next_mode != 0) {
        djf_enter(f); // из обхода: вход через нарастание wet
    }
}

float acid_djf_next(acid_djf_t *f, float in)
{
    float step = f->xfade;
    if (f->next_mode != f->mode) {
        // Смена режима: увести фильтр, переключить, вернуть.
        f->wet -= step;
        if (f->wet <= 0.0f) {
            f->wet = 0.0f;
            djf_enter(f);
        }
    } else if (f->mode != 0 && f->wet < 1.0f) {
        f->wet = f->wet + step < 1.0f ? f->wet + step : 1.0f;
    }
    if (f->mode == 0) {
        return in;
    }
    if (f->g != f->g_target || f->k != f->k_target) {
        // Срез едет к цели: коэффициенты заново (деление дорогое, на месте не считаем).
        float dg = f->g_target - f->g, dk = f->k_target - f->k;
        // Шаг меньше точности float - дальше не сдвинется: ставим цель ровно.
        float g = f->g + dg * f->smooth, k = f->k + dk * f->smooth;
        f->g = g == f->g || fabsf(dg) < 1e-4f * f->g_target ? f->g_target : g;
        f->k = k == f->k || fabsf(dk) < 1e-4f * f->k_target ? f->k_target : k;
        djf_coefs(f);
    }

    // TPT SVF, как acid_svf_bp_hp в acid_noise.c.
    float a1 = f->a1, a2 = f->a2, a3 = f->a3;
    float v3 = in - f->ic2;
    float v1 = a1 * f->ic1 + a2 * v3;
    float v2 = f->ic2 + a2 * f->ic1 + a3 * v3;
    f->ic1 = 2.0f * v1 - f->ic1;
    f->ic2 = 2.0f * v2 - f->ic2;
    float y = f->mode == 1 ? v2 : in - f->k * v1 - v2;
    return f->wet < 1.0f ? in + f->wet * (y - in) : y;
}

int acid_djf_active(const acid_djf_t *f)
{
    return f->mode != 0 || f->next_mode != 0;
}

void acid_crush_init(acid_crush_t *c)
{
    c->hold = 0.0f;
    c->cnt = 0;
    acid_crush_set(c, 0, 0);
}

void acid_crush_set(acid_crush_t *c, int bits_knob, int rate_knob)
{
    c->bits_knob = bits_knob;
    c->rate_knob = rate_knob;
    // 16 бит у первого щелчка, 3 бита на 127; шаг на диапазоне -1..1.
    int bits = 16 - bits_knob * 13 / 127;
    c->step = bits_knob ? 2.0f / (float)(1 << bits) : 0.0f;
    c->inv_step = bits_knob ? 1.0f / c->step : 0.0f;
    c->factor = rate_knob ? (int)(powf(32.0f, (float)rate_knob / 127.0f) + 0.5f) : 1;
}

float acid_crush_next(acid_crush_t *c, float x)
{
    if (c->factor > 1) {
        if (c->cnt == 0) {
            c->hold = x;
        }
        c->cnt = c->cnt + 1 < c->factor ? c->cnt + 1 : 0;
        x = c->hold;
    }
    if (c->step > 0.0f) {
        float v = x * c->inv_step;
        x = (float)(int)(v >= 0.0f ? v + 0.5f : v - 0.5f) * c->step; // без floorf: он из libm и медленный
    }
    return x;
}

int acid_crush_active(const acid_crush_t *c)
{
    return c->bits_knob != 0 || c->rate_knob != 0;
}

#define DELAY_SCALE 16384.0f // запас +-2 в 16 битах
#define DELAY_SLEW 0.5f      // задержка меняется не быстрее 0.5 сэмпла за сэмпл: высота плывёт до x1.5
#define DELAY_TONE_LO 500.0f
#define DELAY_TONE_HI 12000.0f

// Доли четверти по зонам ручки времени: 1/16, 1/8, 3/16, 1/4, 3/8.
static const float DELAY_DIVS[5] = {0.25f, 0.5f, 0.75f, 1.0f, 1.5f};

static void delay_target(acid_delay_t *d)
{
    float t = d->spb * DELAY_DIVS[d->time_knob * 5 / 128];
    while (t > (float)(ACID_DELAY_LEN - 2)) {
        t *= 0.5f; // при медленном темпе доля не влезает: вдвое короче
    }
    d->d_target = t;
}

void acid_delay_init(acid_delay_t *d, float sr)
{
    d->sr = sr;
    memset(d->buf, 0, sizeof d->buf);
    d->w = 0;
    d->lp = 0.0f;
    d->zero_run = ACID_DELAY_LEN;
    d->spb = sr * 0.5f;
    acid_delay_set(d, 2 * 128 / 5 + 1, 53, 83); // 3/16, fb ~0.4, тон ~4 кГц
    d->d = d->d_target;
}

void acid_delay_set(acid_delay_t *d, int time_knob, int fb_knob, int tone_knob)
{
    d->time_knob = time_knob;
    d->fb = (float)fb_knob * 0.95f / 127.0f;
    float fc = DELAY_TONE_LO * powf(DELAY_TONE_HI / DELAY_TONE_LO, (float)tone_knob / 127.0f);
    d->tone = 1.0f - expf(-2.0f * PI_F * fc / d->sr);
    delay_target(d);
}

void acid_delay_set_spb(acid_delay_t *d, float spb)
{
    d->spb = spb;
    delay_target(d);
}

float acid_delay_next(acid_delay_t *d, float in)
{
    float dd = d->d_target - d->d;
    d->d += dd > DELAY_SLEW ? DELAY_SLEW : (dd < -DELAY_SLEW ? -DELAY_SLEW : dd);

    // Дробное чтение назад от точки записи, линейная интерполяция.
    float pos = (float)d->w - d->d;
    pos += pos < 0.0f ? (float)ACID_DELAY_LEN : 0.0f;
    int i0 = (int)pos;
    i0 = i0 < ACID_DELAY_LEN ? i0 : i0 - ACID_DELAY_LEN; // pos чуть меньше 0 округляется ровно в LEN
    float f = pos - (float)i0;
    int i1 = i0 + 1 < ACID_DELAY_LEN ? i0 + 1 : 0;
    float r = ((float)d->buf[i0] + f * (float)(d->buf[i1] - d->buf[i0])) * (1.0f / DELAY_SCALE);

    d->lp += (r - d->lp) * d->tone;
    if (fabsf(d->lp) < 1e-7f) {
        d->lp = 0.0f; // без хвоста в субнормалах
    }
    float x = (in + d->fb * d->lp) * DELAY_SCALE;
    x = x > 32767.0f ? 32767.0f : (x < -32767.0f ? -32767.0f : x);
    int16_t q = (int16_t)x; // к нулю: тихий хвост сам уходит в 0, без зацикливания на младшем бите
    d->buf[d->w] = q;
    d->w = d->w + 1 < ACID_DELAY_LEN ? d->w + 1 : 0;
    d->zero_run = q ? 0 : (d->zero_run < ACID_DELAY_LEN ? d->zero_run + 1 : d->zero_run);
    return d->lp;
}

int acid_delay_active(const acid_delay_t *d)
{
    return d->zero_run < ACID_DELAY_LEN || d->lp != 0.0f;
}

float acid_delay_time_samples(const acid_delay_t *d)
{
    return d->d_target;
}

#define STUTTER_SCALE 16384.0f

// Доли четверти по зонам: 1/4, 1/8, 1/16, 1/32.
static const float STUTTER_DIVS[4] = {1.0f, 0.5f, 0.25f, 0.125f};

static int stutter_len(const acid_stutter_t *st)
{
    float t = st->spb * STUTTER_DIVS[(st->knob - 1) * 4 / 127];
    while (t > (float)(ACID_STUTTER_LEN - ACID_STUTTER_SEAM)) {
        t *= 0.5f;
    }
    return (int)t;
}

void acid_stutter_init(acid_stutter_t *st, float sr)
{
    st->spb = sr * 0.5f;
    st->knob = 0;
    st->on = st->looping = 0;
    st->len = st->rec = st->pos = 0;
    st->restart = st->new_len = st->opos = st->xf = 0;
    st->wet = 0.0f;
    st->xstep = 1.0f / (float)ACID_STUTTER_SEAM;
}

void acid_stutter_set(acid_stutter_t *st, int knob)
{
    st->knob = knob;
    if (!knob) {
        st->on = 0; // петля доигрывает, пока wet уходит в 0
        st->restart = 0;
        return;
    }
    int len = stutter_len(st);
    if (!st->looping) {
        // Включили или ещё пишем: пишем заново, на выходе пока живой звук - щелчка нет.
        st->rec = st->pos = 0;
        st->len = len;
        st->on = 1;
        st->restart = 0;
        return;
    }
    if (st->on && !st->restart && len == st->len) {
        return;
    }
    if (st->on && !st->restart && len < st->len && len + ACID_STUTTER_SEAM <= st->rec) {
        // Короче: крутится начало куска. Старая головка доигрывает 2 мс по записанному продолжению.
        st->opos = st->pos;
        st->xf = ACID_STUTTER_SEAM;
        st->pos %= len;
        st->len = len;
        return;
    }
    // Длиннее или включили во время ухода: петля уходит в 0, затем запись заново.
    st->restart = 1;
    st->new_len = len;
    st->on = 1;
}

void acid_stutter_set_spb(acid_stutter_t *st, float spb)
{
    st->spb = spb; // длина петли меняется со следующим включением
}

float acid_stutter_next(acid_stutter_t *st, float in)
{
    float loop = in;
    if (st->rec < st->len + ACID_STUTTER_SEAM) {
        // Пишем кусок и ещё шов: продолжение звука нужно для кроссфейда на стыке петли.
        float x = in * STUTTER_SCALE;
        x = x > 32767.0f ? 32767.0f : (x < -32767.0f ? -32767.0f : x);
        st->buf[st->rec++] = (int16_t)x;
        st->looping |= st->rec > st->len;
    }
    if (st->looping) {
        int k = st->pos;
        float y = (float)st->buf[k];
        if (k < ACID_STUTTER_SEAM) {
            // Начало прохода: от продолжения записи (стык непрерывен) к началу куска.
            float a = (float)k * (1.0f / ACID_STUTTER_SEAM);
            y = a * y + (1.0f - a) * (float)st->buf[st->len + k];
        }
        if (st->xf > 0) {
            float a = 1.0f - (float)st->xf * (1.0f / ACID_STUTTER_SEAM);
            y = a * y + (1.0f - a) * (float)st->buf[st->opos++];
            st->xf--;
        }
        loop = y * (1.0f / STUTTER_SCALE);
        st->pos = k + 1 < st->len ? k + 1 : 0;
    }
    if (st->on && !st->restart) {
        st->wet = st->wet + st->xstep < 1.0f ? st->wet + st->xstep : 1.0f;
    } else {
        st->wet -= st->xstep;
        if (st->wet <= 0.0f) {
            st->wet = 0.0f;
            st->looping = 0;
            st->rec = st->pos = st->xf = 0;
            st->len = st->restart ? st->new_len : 0;
            st->restart = 0;
            return in;
        }
    }
    return st->wet == 1.0f ? loop : in + st->wet * (loop - in);
}

int acid_stutter_active(const acid_stutter_t *st)
{
    return st->on || st->wet > 0.0f;
}
