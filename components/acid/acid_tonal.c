#include "acid_tonal.h"

#include <string.h>

#define KNOB_NONE 0xFF
#define STEP_ENC 8 // энкодеры 9-16 (индексы 8..15) - шаги

// Ступени ладов в полутонах от тоники; 0 - хроматика, ладов нет.
static const int8_t SCALES[8][8] = {
    {0},
    {0, 2, 4, 5, 7, 9, 11},
    {0, 2, 3, 5, 7, 8, 10},
    {0, 2, 3, 5, 7, 9, 10},
    {0, 1, 3, 5, 7, 8, 10},
    {0, 2, 4, 5, 7, 9, 10},
    {0, 2, 3, 5, 7, 8, 11},
    {0, 3, 5, 7, 10},
};
static const int SCALE_N[8] = {12, 7, 7, 7, 7, 7, 7, 5};

void acid_tonal_init(acid_tonal_t *t)
{
    memset(t->knob, KNOB_NONE, sizeof t->knob);
    t->len = 0;
    t->nsteps = 4;
    t->order = 0;
    t->target = 0;
    t->held_mode = 1;
    t->scale = 0;
    t->tonic = 0;
    memset(t->raw, 0, sizeof t->raw);
    t->bar_len = 0;
    t->ticks = 0;
    t->step = 0;
    t->dir = 1;
    t->rnd = 0x9E3779B9u;
}

// Выбор из списка: значение на экране в [lo, hi] - берём, иначе ручка не меняет ничего.
static int pick(int v, int lo, int hi, int *dst)
{
    if (v < lo || v > hi) {
        return 0;
    }
    *dst = v;
    return 1;
}

void acid_tonal_set(acid_tonal_t *t, int enc, int v)
{
    int ok = 0;
    switch (enc) {
    case 1:
        ok = pick(v, 0, 16, &t->len);
        if (ok && t->bar_len == 0) {
            t->bar_len = t->len * ACID_TONAL_BAR; // включили слой: первая граница через длину шага
        }
        break;
    case 2: ok = pick(v, 1, ACID_TONAL_STEPS, &t->nsteps); break;
    case 3: ok = pick(v, 0, 2, &t->order); break;
    case 4: ok = pick(v, 0, 2, &t->target); break;
    case 5: ok = pick(v, 0, 2, &t->held_mode); break;
    case 6: ok = pick(v, 0, 7, &t->scale); break;
    case 7: ok = pick(v, 0, 11, &t->tonic); break;
    default:
        if (enc >= STEP_ENC && enc < STEP_ENC + ACID_TONAL_STEPS && v >= 52 && v <= 76) {
            t->raw[enc - STEP_ENC] = (int8_t)(v - 64);
            ok = 1;
        }
        break;
    }
    if (ok) {
        t->knob[enc] = (uint8_t)v;
    }
}

void acid_tonal_start(acid_tonal_t *t)
{
    t->ticks = 0;
    t->step = 0;
    t->dir = 1;
    t->bar_len = t->len * ACID_TONAL_BAR;
}

static int next_step(acid_tonal_t *t)
{
    int n = t->nsteps;
    if (n <= 1) {
        return 0;
    }
    if (t->order == 2) {
        // Случайный шаг, не равный текущему (xorshift, детерминированно).
        t->rnd ^= t->rnd << 13;
        t->rnd ^= t->rnd >> 17;
        t->rnd ^= t->rnd << 5;
        int cur = t->step < n ? t->step : 0;
        int s = (int)(t->rnd % (uint32_t)(n - 1));
        return s >= cur ? s + 1 : s;
    }
    if (t->step >= n) {
        return 0; // круг укоротили: с начала
    }
    if (t->order == 1) {
        int s = t->step + t->dir;
        if (s >= n || s < 0) {
            t->dir = -t->dir;
            s = t->step + t->dir;
        }
        return s;
    }
    return t->step + 1 < n ? t->step + 1 : 0;
}

int acid_tonal_tick(acid_tonal_t *t)
{
    if (t->len == 0) {
        t->bar_len = 0;
        return 0;
    }
    if (++t->ticks < (uint32_t)t->bar_len) {
        return 0;
    }
    t->ticks = 0;
    t->bar_len = t->len * ACID_TONAL_BAR; // новая длина действует с этой границы
    t->step = next_step(t);
    return 1;
}

int acid_tonal_active(const acid_tonal_t *t)
{
    return t->len > 0;
}

int acid_tonal_step(const acid_tonal_t *t)
{
    return t->step;
}

static int floor_div(int a, int b)
{
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

int acid_tonal_note(const acid_tonal_t *t, int note)
{
    if (t->len == 0) {
        return note;
    }
    int sh = t->raw[t->step];
    if (t->scale == 0) {
        return note + sh;
    }
    sh = sh > 7 ? 7 : (sh < -7 ? -7 : sh);
    const int8_t *sc = SCALES[t->scale];
    int n = SCALE_N[t->scale];
    int rel = note - t->tonic;
    int oct = floor_div(rel, 12);
    int pc = rel - oct * 12;
    int deg = 0;
    while (deg + 1 < n && sc[deg + 1] <= pc) {
        deg++; // ближайшая ступень не выше ноты
    }
    int rest = pc - sc[deg];
    int d = deg + sh;
    int o = floor_div(d, n);
    d -= o * n;
    return t->tonic + (oct + o) * 12 + sc[d] + rest;
}

int acid_tonal_moves(const acid_tonal_t *t, int slot)
{
    return t->len > 0 && (t->target == 0 || t->target == slot + 1);
}

uint32_t acid_tonal_ticks_left(const acid_tonal_t *t)
{
    return (uint32_t)t->bar_len - t->ticks;
}
