#include "acid_petri.h"

#include <math.h>
#include <string.h>

#include "acid_math.h"

// Голос чашки Петри. Коэффициенты под частоту sr: DC 0.9954 и уровень 0.000367 - пересчёт с 44.1 кГц прототипа.

#define XFADE_LIVE_S 0.05f // таблица с панели: кадр 20 Гц
#define XFADE_SLOW_S 0.5f  // зашитые: сняты раз в 0.5 с
#define QUIET_S 0.5f       // без таблиц снаружи - на зашитые
#define FREEZE_S 0.03f

void acid_petri_init(acid_petri_t *p, float sr)
{
    memset(p, 0, sizeof *p);
    p->sr = sr;
    p->a = 0;
    p->b = 1;
    p->x = 1.0f;
    p->dx_live = 1.0f / (XFADE_LIVE_S * sr);
    p->dx_slow = 1.0f / (XFADE_SLOW_S * sr);
    p->dx = p->dx_live;
    p->mc = 1.0f - expf(-1.0f / (FREEZE_S * sr));
    p->quiet_max = (uint32_t)(QUIET_S * sr);
    p->rnd = 0x2545F491u;
    p->ctrl = 1;
    acid_petri_set_env(p, 0.01f, 0.6f);
}

void acid_petri_wave_build(acid_petri_wave_t *w, const int16_t tab[ACID_PETRI_TABLE])
{
    const int N = ACID_PETRI_TABLE;
    memcpy(w->t[0], tab, N * sizeof *tab);
    w->t[0][N] = w->t[0][0];
    // Копия k - среднее двух точек копии k-1 через 2^(k-1): окно 2, 4, 8 точек.
    for (int k = 1; k < ACID_PETRI_LEVELS; k++) {
        const int d = 1 << (k - 1);
        const int16_t *a = w->t[k - 1];
        int16_t *o = w->t[k];
        for (int i = 0; i < N - d; i++) {
            o[i] = (int16_t)((a[i] + a[i + d]) / 2);
        }
        for (int i = N - d; i < N; i++) {
            o[i] = (int16_t)((a[i] + a[i + d - N]) / 2); // по кругу
        }
        o[N] = o[0];
    }
}

static void take_frame(acid_petri_t *p, const acid_petri_frame_t *f, int slow)
{
    int t = p->a;
    p->a = p->b;
    p->b = t;
    acid_petri_wave_build(&p->w[p->b], f->tab);
    memcpy(p->mods, f->mods, sizeof p->mods);
    p->jumps = f->jumps;
    p->x = 0.0f;
    p->dx = slow ? p->dx_slow : p->dx_live;
}

void acid_petri_use_baked(acid_petri_t *p, const acid_petri_frame_t *f, int n)
{
    p->baked = f;
    p->nbaked = f ? n : 0;
    p->bi = 0;
    if (p->nbaked) {
        // Зашитая звучит сразу: без кроссфейда из тишины.
        take_frame(p, &f[0], 1);
        p->x = 1.0f;
        p->jumps_seen = p->jumps;
        p->bi = 1 % p->nbaked;
        p->quiet = p->quiet_max;
    }
}

void acid_petri_post(acid_petri_t *p, const acid_petri_frame_t *f)
{
    p->inbox = *f;
    p->pending = 1;
    p->inbox_slow = 0;
    p->quiet = 0;
    p->dx = p->dx_live; // медленный переход по зашитым дотягивается быстро: живая таблица не ждёт 0.5 с
}

void acid_petri_take(acid_petri_t *p)
{
    take_frame(p, &p->inbox, p->inbox_slow);
    p->pending = 0;
}

void acid_petri_queue_baked(acid_petri_t *p)
{
    p->inbox = p->baked[p->bi];
    p->bi = (p->bi + 1) % p->nbaked;
    p->pending = 1;
    p->inbox_slow = 1;
}

void acid_petri_gains(acid_petri_t *p)
{
    for (int k = 0; k < ACID_PETRI_VOICES; k++) {
        acid_petri_voice_t *v = &p->v[k];
        if (v->on) {
            v->g = 0.3f * acid_rcp(v->lvl + 0.02f); // мягкое автоусиление: +0.02 не тянет тихий остаток
        }
    }
}

static int level_of(float hz)
{
    return hz < 1000.0f ? 0 : hz < 2000.0f ? 1 : hz < 4000.0f ? 2 : 3;
}

static void set_hz(const acid_petri_t *p, acid_petri_voice_t *v, float hz)
{
    v->hz = hz;
    v->inc = (uint32_t)(hz * (4294967296.0f / p->sr));
    v->level = (uint8_t)level_of(hz);
}

void acid_petri_retune(acid_petri_t *p, int note, float hz)
{
    for (int k = 0; k < ACID_PETRI_VOICES; k++) {
        if (p->v[k].gate && p->v[k].note == note) {
            set_hz(p, &p->v[k], hz);
        }
    }
}

void acid_petri_move(acid_petri_t *p, int from_note, int to_note, float hz)
{
    for (int k = 0; k < ACID_PETRI_VOICES; k++) {
        if (p->v[k].gate && p->v[k].note == from_note) {
            p->v[k].note = (int8_t)to_note;
            set_hz(p, &p->v[k], hz);
            return;
        }
    }
}

void acid_petri_note_on(acid_petri_t *p, int note, float hz, int vel)
{
    (void)vel;
    acid_petri_voice_t *v = NULL;
    for (int k = 0; k < ACID_PETRI_VOICES && !v; k++) {
        if (p->v[k].on && p->v[k].note == note) {
            v = &p->v[k]; // та же нота: перезапуск
        }
    }
    for (int k = 0; k < ACID_PETRI_VOICES && !v; k++) {
        if (!p->v[k].on) {
            v = &p->v[k];
        }
    }
    if (!v) {
        v = &p->v[0]; // самый старый
        for (int k = 1; k < ACID_PETRI_VOICES; k++) {
            v = p->v[k].age < v->age ? &p->v[k] : v;
        }
    }
    if (!v->on) {
        p->rnd = p->rnd * 1664525u + 1013904223u;
        v->ph = p->rnd; // случайная фаза: аккорд не складывается в фазе
        v->env = v->x1 = v->y1 = 0.0f;
        v->lvl = 0.05f;
        v->g = 0.3f * acid_rcp(v->lvl + 0.02f);
    }
    set_hz(p, v, hz);
    v->note = (int8_t)note;
    v->gate = v->on = 1;
    v->age = ++p->age;
}

void acid_petri_note_off(acid_petri_t *p, int note)
{
    for (int k = 0; k < ACID_PETRI_VOICES; k++) {
        if (p->v[k].gate && p->v[k].note == note) {
            p->v[k].gate = 0;
        }
    }
}

void acid_petri_notes_off(acid_petri_t *p)
{
    for (int k = 0; k < ACID_PETRI_VOICES; k++) {
        p->v[k].gate = 0;
    }
}

void acid_petri_kill(acid_petri_t *p)
{
    for (int k = 0; k < ACID_PETRI_VOICES; k++) {
        p->v[k].gate = p->v[k].on = 0;
    }
}

void acid_petri_set_env(acid_petri_t *p, float attack_s, float release_s)
{
    p->ea = 1.0f - expf(-1.0f / (attack_s * p->sr));
    p->er = 1.0f - expf(-1.0f / (release_s * p->sr));
}

void acid_petri_freeze(acid_petri_t *p, int on)
{
    // Переснимаем, только когда прежний снимок не слышен: ушёл в ноль или голоса молчат (тогда и доля стоит).
    if (on && p->mt == 0.0f && (p->m == 0.0f || !acid_petri_active(p))) {
        p->fz = p->w[p->b];
    }
    p->mt = on ? 1.0f : 0.0f;
}

int acid_petri_jumped(acid_petri_t *p)
{
    if (p->jumps != p->jumps_seen) {
        p->jumps_seen = p->jumps;
        return 1;
    }
    return 0;
}

float acid_petri_mod(const acid_petri_t *p, int i)
{
    return i >= 0 && i < ACID_PETRI_MODS ? p->mods[i] * (1.0f / 127.0f) : 0.0f;
}

int acid_petri_active(const acid_petri_t *p)
{
    for (int k = 0; k < ACID_PETRI_VOICES; k++) {
        if (p->v[k].on) {
            return 1;
        }
    }
    return 0;
}
