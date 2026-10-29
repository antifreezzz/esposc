#include "acid_fx.h"

#include <math.h>

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
    p->atk_coef = one_pole(sr, PUMP_ATTACK_S);
    p->smooth = one_pole(sr, SMOOTH_S);
    acid_pump_set_release(p, 0.15f);
}

void acid_pump_set_depth(acid_pump_t *p, float depth)
{
    p->depth = depth;
}

void acid_pump_set_release(acid_pump_t *p, float tau_s)
{
    p->rel_coef = expf(-1.0f / (tau_s * p->sr));
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
        p->e += (p->target - p->e) * p->atk_coef;
        if (p->e >= 0.98f * p->target) {
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
}

// Новый режим вступает: состояние с нуля, срез сразу в цель (сглаживать не от чего).
static void djf_enter(acid_djf_t *f)
{
    f->mode = f->next_mode;
    f->ic1 = f->ic2 = 0.0f;
    f->g = f->g_target;
    f->k = f->k_target;
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
    f->g += (f->g_target - f->g) * f->smooth;
    f->k += (f->k_target - f->k) * f->smooth;

    // TPT SVF, как acid_svf_bp_hp в acid_noise.c.
    float a1 = 1.0f / (1.0f + f->g * (f->g + f->k));
    float a2 = f->g * a1;
    float a3 = f->g * a2;
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
