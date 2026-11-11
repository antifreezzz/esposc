#include "acid_fx.h"
#include "acid_math.h"

#include <math.h>
#include <string.h>

#define PI_F 3.14159265f
#define PUMP_ATTACK_S 0.003f
#define SMOOTH_S 0.005f // сглаживание глубины, среза и резонанса
#define XFADE_S 0.005f  // кроссфейд смены режима фильтра
#define DEAD_LO 64      // обход только ровно на 64: щелчки BSP чёткие, мёртвая зона не нужна
#define DEAD_HI 64
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
    f->ic1 = f->ic2 = f->ic1r = f->ic2r = 0.0f;
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
    f->ic1 = f->ic2 = f->ic1r = f->ic2r = 0.0f;
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

// Кроссфейд режимов и сглаживание среза, раз на сэмпл для обоих каналов. 0 - обход.
static int djf_control(acid_djf_t *f)
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
        return 0;
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
    return 1;
}

// TPT SVF, как acid_svf_bp_hp в acid_noise.c.
static float djf_tap(const acid_djf_t *f, float in, float *ic1, float *ic2)
{
    float a1 = f->a1, a2 = f->a2, a3 = f->a3;
    float v3 = in - *ic2;
    float v1 = a1 * *ic1 + a2 * v3;
    float v2 = *ic2 + a2 * *ic1 + a3 * v3;
    *ic1 = 2.0f * v1 - *ic1;
    *ic2 = 2.0f * v2 - *ic2;
    float y = f->mode == 1 ? v2 : in - f->k * v1 - v2;
    return f->wet < 1.0f ? in + f->wet * (y - in) : y;
}

float acid_djf_next(acid_djf_t *f, float in)
{
    return djf_control(f) ? djf_tap(f, in, &f->ic1, &f->ic2) : in;
}

void acid_djf_next2(acid_djf_t *f, float *l, float *r)
{
    if (djf_control(f)) {
        *l = djf_tap(f, *l, &f->ic1, &f->ic2);
        *r = djf_tap(f, *r, &f->ic1r, &f->ic2r);
    }
}

int acid_djf_active(const acid_djf_t *f)
{
    return f->mode != 0 || f->next_mode != 0;
}

void acid_crush_init(acid_crush_t *c)
{
    c->hold = c->hold_r = 0.0f;
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

static float crush_quant(const acid_crush_t *c, float x)
{
    float v = x * c->inv_step;
    return (float)(int)(v >= 0.0f ? v + 0.5f : v - 0.5f) * c->step; // без floorf: он из libm и медленный
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
    return c->step > 0.0f ? crush_quant(c, x) : x;
}

void acid_crush_next2(acid_crush_t *c, float *l, float *r)
{
    if (c->factor > 1) {
        if (c->cnt == 0) {
            c->hold = *l;
            c->hold_r = *r;
        }
        c->cnt = c->cnt + 1 < c->factor ? c->cnt + 1 : 0;
        *l = c->hold;
        *r = c->hold_r;
    }
    if (c->step > 0.0f) {
        *l = crush_quant(c, *l);
        *r = crush_quant(c, *r);
    }
}

int acid_crush_active(const acid_crush_t *c)
{
    return c->bits_knob != 0 || c->rate_knob != 0;
}

#define DELAY_SCALE 16384.0f // запас +-2 в 16 битах
#define DELAY_SLEW 0.5f      // задержка меняется не быстрее 0.5 сэмпла за сэмпл: высота плывёт до x1.5
#define DELAY_TONE_LO 500.0f
#define DELAY_TONE_HI 12000.0f
#define DELAY_MOVE_STEP (1.0f / 240.0f)  // смена движения за 5 мс
#define DELAY_PAN_STEP (127.0f / 240.0f) // точка панорамы через всю ширину за 5 мс

// Доли четверти по значению ручки времени 0..4: 1/16, 1/8, 3/16, 1/4, 3/8.
static const float DELAY_DIVS[5] = {0.25f, 0.5f, 0.75f, 1.0f, 1.5f};

static void delay_target(acid_delay_t *d)
{
    float t = d->spb * DELAY_DIVS[d->time_knob];
    while (t > (float)(ACID_DELAY_LEN - 2)) {
        t *= 0.5f; // при медленном темпе доля не влезает: вдвое короче
    }
    d->d_target = t;
}

void acid_delay_init(acid_delay_t *d, float sr, int16_t *buf)
{
    d->sr = sr;
    d->buf = buf;
    memset(d->buf, 0, 2 * ACID_DELAY_LEN * sizeof *d->buf);
    d->w = 0;
    d->lp = 0.0f;
    d->zero_run = ACID_DELAY_LEN;
    d->lp_b = 0.0f;
    d->motion = 0;
    d->width = 127;
    d->m = 0.0f;
    d->pos_a = d->pos_b = d->tgt_a = d->tgt_b = 64.0f;
    d->rnd_left = 0;
    d->rnd = 0x2545F491u;
    d->spb = sr * 0.5f;
    acid_delay_set(d, 2, 53, 83); // 3/16, fb ~0.4, тон ~4 кГц
    d->d = d->d_target;
}

void acid_delay_set(acid_delay_t *d, int time_knob, int fb_knob, int tone_knob)
{
    if (time_knob <= 4) {
        d->time_knob = time_knob; // вне списка долей: время прежнее
    }
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

void acid_delay_set_motion(acid_delay_t *d, int motion, int width)
{
    if (motion <= 2) {
        d->motion = motion; // вне списка: движение прежнее
    }
    d->width = width;
}

static float delay_lp(float lp, float r, float tone)
{
    lp += (r - lp) * tone;
    return fabsf(lp) < 1e-7f ? 0.0f : lp; // без хвоста в субнормалах
}

static int16_t delay_q(float in)
{
    // Выше 1 - мягкое насыщение к 2 (как лента): громкий посыл и разгон обратной связи без среза.
    if (in > 1.0f) {
        in = 1.0f + acid_tanh(in - 1.0f);
    } else if (in < -1.0f) {
        in = -1.0f - acid_tanh(-in - 1.0f);
    }
    float x = in * DELAY_SCALE;
    x = x > 32767.0f ? 32767.0f : (x < -32767.0f ? -32767.0f : x);
    return (int16_t)x; // к нулю: тихий хвост сам уходит в 0, без зацикливания на младшем бите
}

static float slew(float x, float target, float step)
{
    float dx = target - x;
    return dx > step ? x + step : (dx < -step ? x - step : target);
}

// Цели панорамы выходов линий по движению и ширине.
static void delay_pan_targets(acid_delay_t *d)
{
    float lo = 64.0f - (float)d->width * (64.0f / 127.0f); // на 127 ровно 0
    float hi = 64.0f + (float)d->width * (63.0f / 127.0f); // на 127 ровно 127
    if (d->motion == 1) {
        d->tgt_a = lo;
        d->tgt_b = hi;
    } else if (d->motion == 2) {
        if (--d->rnd_left <= 0) {
            // Новая точка на каждый повтор (раз во время дилэя).
            d->rnd ^= d->rnd << 13;
            d->rnd ^= d->rnd >> 17;
            d->rnd ^= d->rnd << 5;
            float u = (float)(d->rnd >> 8) * (1.0f / 16777216.0f);
            d->tgt_a = lo + u * (hi - lo);
            d->rnd_left = (int)d->d;
        }
        d->tgt_b = 64.0f;
    } else {
        d->tgt_a = d->tgt_b = 64.0f;
    }
}

void acid_delay_next2(acid_delay_t *d, float in, float *l, float *r)
{
    float dd = d->d_target - d->d;
    d->d += dd > DELAY_SLEW ? DELAY_SLEW : (dd < -DELAY_SLEW ? -DELAY_SLEW : dd);

    // Дробное чтение назад от точки записи, линейная интерполяция; линия B той же задержкой.
    const int16_t *ba = d->buf, *bb = d->buf + ACID_DELAY_LEN;
    float pos = (float)d->w - d->d;
    pos += pos < 0.0f ? (float)ACID_DELAY_LEN : 0.0f;
    int i0 = (int)pos;
    i0 = i0 < ACID_DELAY_LEN ? i0 : i0 - ACID_DELAY_LEN; // pos чуть меньше 0 округляется ровно в LEN
    float f = pos - (float)i0;
    int i1 = i0 + 1 < ACID_DELAY_LEN ? i0 + 1 : 0;
    float ra = ((float)ba[i0] + f * (float)(ba[i1] - ba[i0])) * (1.0f / DELAY_SCALE);
    float rb = ((float)bb[i0] + f * (float)(bb[i1] - bb[i0])) * (1.0f / DELAY_SCALE);
    d->lp = delay_lp(d->lp, ra, d->tone);
    d->lp_b = delay_lp(d->lp_b, rb, d->tone);

    // Пинг-понг: A -> B -> A, каждый переход с обратной связью. m плавно переключает петлю.
    d->m = slew(d->m, d->motion == 1 ? 1.0f : 0.0f, DELAY_MOVE_STEP);
    int16_t qa = delay_q(in + d->fb * ((1.0f - d->m) * d->lp + d->m * d->lp_b));
    int16_t qb = delay_q(d->m * d->fb * d->lp);
    d->buf[d->w] = qa;
    d->buf[ACID_DELAY_LEN + d->w] = qb;
    d->w = d->w + 1 < ACID_DELAY_LEN ? d->w + 1 : 0;
    d->zero_run = (qa | qb) ? 0 : (d->zero_run < ACID_DELAY_LEN ? d->zero_run + 1 : d->zero_run);

    delay_pan_targets(d);
    d->pos_a = slew(d->pos_a, d->tgt_a, DELAY_PAN_STEP);
    d->pos_b = slew(d->pos_b, d->tgt_b, DELAY_PAN_STEP);
    float al, ar, bl, br;
    acid_pan(d->pos_a, &al, &ar);
    acid_pan(d->pos_b, &bl, &br);
    *l = al * d->lp + bl * d->lp_b;
    *r = ar * d->lp + br * d->lp_b;
}

float acid_delay_next(acid_delay_t *d, float in)
{
    float l, r;
    acid_delay_next2(d, in, &l, &r);
    return 0.5f * (l + r);
}

int acid_delay_active(const acid_delay_t *d)
{
    return d->zero_run < ACID_DELAY_LEN || d->lp != 0.0f || d->lp_b != 0.0f;
}

float acid_delay_time_samples(const acid_delay_t *d)
{
    return d->d_target;
}

// Доли четверти по значению ручки 1..4: 1/4, 1/8, 1/16, 1/32.
static const float STUTTER_DIVS[4] = {1.0f, 0.5f, 0.25f, 0.125f};

static int stutter_len(const acid_stutter_t *st)
{
    float t = st->spb * STUTTER_DIVS[st->knob - 1];
    while (t > (float)(ACID_STUTTER_LEN - ACID_STUTTER_SEAM)) {
        t *= 0.5f;
    }
    return (int)t;
}

void acid_stutter_init(acid_stutter_t *st, float sr, float *buf)
{
    st->buf = buf;
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
    if (knob > 4) {
        return; // вне списка долей: игнор
    }
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

void acid_stutter_next2(acid_stutter_t *st, float *l, float *r)
{
    float *bl = st->buf, *br = st->buf + ACID_STUTTER_LEN;
    float in_l = *l, in_r = *r;
    float loop_l = in_l, loop_r = in_r;
    if (st->rec < st->len + ACID_STUTTER_SEAM) {
        // Пишем кусок и ещё шов: продолжение звука нужно для кроссфейда на стыке петли.
        bl[st->rec] = in_l;
        br[st->rec] = in_r;
        st->rec++;
        st->looping |= st->rec > st->len;
    }
    if (st->looping) {
        int k = st->pos;
        float yl = bl[k], yr = br[k];
        if (k < ACID_STUTTER_SEAM) {
            // Начало прохода: от продолжения записи (стык непрерывен) к началу куска.
            float a = (float)k * (1.0f / ACID_STUTTER_SEAM);
            yl = a * yl + (1.0f - a) * bl[st->len + k];
            yr = a * yr + (1.0f - a) * br[st->len + k];
        }
        if (st->xf > 0) {
            float a = 1.0f - (float)st->xf * (1.0f / ACID_STUTTER_SEAM);
            yl = a * yl + (1.0f - a) * bl[st->opos];
            yr = a * yr + (1.0f - a) * br[st->opos];
            st->opos++;
            st->xf--;
        }
        loop_l = yl;
        loop_r = yr;
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
            return; // живой звук как есть
        }
    }
    if (st->wet == 1.0f) {
        *l = loop_l;
        *r = loop_r;
    } else {
        *l = in_l + st->wet * (loop_l - in_l);
        *r = in_r + st->wet * (loop_r - in_r);
    }
}

float acid_stutter_next(acid_stutter_t *st, float in)
{
    float l = in, r = in;
    acid_stutter_next2(st, &l, &r);
    return l;
}

int acid_stutter_active(const acid_stutter_t *st)
{
    return st->on || st->wet > 0.0f;
}

void acid_pan(float pos, float *gl, float *gr)
{
    if (pos <= 64.0f) {
        *gl = 1.0f;
        *gr = pos > 0.0f ? pos * (1.0f / 64.0f) : 0.0f;
    } else {
        *gr = 1.0f;
        *gl = pos < 127.0f ? (127.0f - pos) * (1.0f / 63.0f) : 0.0f;
    }
}

// Эффекты кусками: те же сэмплы, что вызовы next на каждом, но вызов из другого файла - раз на кусок, а не
// на сэмпл (на плате это заметная доля цены цепочки). flatten встраивает сюда тела next целиком.
#define FX_RUN __attribute__((flatten))

FX_RUN void acid_pump_run(acid_pump_t *p, float *gain, int n)
{
    for (int i = 0; i < n; i++) {
        gain[i] = acid_pump_next(p);
    }
}

FX_RUN void acid_delay_mix2(acid_delay_t *d, const float *send, float *l, float *r, int n)
{
    for (int i = 0; i < n; i++) {
        if (send[i] != 0.0f || acid_delay_active(d)) {
            float wl, wr;
            acid_delay_next2(d, send[i], &wl, &wr);
            l[i] += wl;
            r[i] += wr;
        }
    }
}

FX_RUN void acid_stutter_run2(acid_stutter_t *st, float *l, float *r, int n)
{
    for (int i = 0; i < n; i++) {
        if (acid_stutter_active(st)) {
            acid_stutter_next2(st, &l[i], &r[i]);
        }
    }
}

FX_RUN void acid_crush_run2(acid_crush_t *c, float *l, float *r, int n)
{
    for (int i = 0; i < n; i++) {
        if (acid_crush_active(c)) {
            acid_crush_next2(c, &l[i], &r[i]);
        }
    }
}

FX_RUN void acid_djf_run2(acid_djf_t *f, float *l, float *r, int n)
{
    for (int i = 0; i < n; i++) {
        acid_djf_next2(f, &l[i], &r[i]);
    }
}
