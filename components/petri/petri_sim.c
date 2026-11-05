#include <math.h>
#include <string.h>

#include "petri.h"

#define N PETRI_N
#define R15 (1 << 14) // округление перед >> 15

static const float PRESETS[PETRI_PRESETS][2] = {
    {0.0367f, 0.0649f}, // Митоз
    {0.0545f, 0.0620f}, // Кораллы
    {0.0290f, 0.0570f}, // Лабиринт
    {0.0650f, 0.0630f}, // Черви (в исходной спеке 0.078/0.061 - там всё гаснет даже во float)
    {0.0300f, 0.0620f}, // Солитоны
    {0.0620f, 0.0609f}, // Коньки
};

uint32_t petri_rand(uint32_t *s)
{
    uint32_t x = *s ? *s : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}

float petri_randf(uint32_t *s)
{
    return (petri_rand(s) >> 8) * (1.0f / 16777216.0f);
}

static inline int wrapi(int a)
{
    a %= N;
    return a < 0 ? a + N : a;
}

static inline float wrapf(float a)
{
    return a - N * floorf(a * (1.0f / N));
}

static inline int16_t q15(float x)
{
    return (int16_t)lrintf(x * PETRI_Q);
}

void petri_preset(int i, float *f, float *k)
{
    if (i < 0 || i >= PETRI_PRESETS) {
        i = 0;
    }
    *f = PRESETS[i][0];
    *k = PRESETS[i][1];
}

void petri_set_fk(petri_t *p, float f, float k)
{
    p->f_q = q15(f);
    p->k_q = q15(k);
}

void petri_drop(petri_t *p, float cx, float cy, float r)
{
    int ri = (int)r;
    float r2 = r * r;
    for (int dy = -ri; dy <= ri; dy++) {
        for (int dx = -ri; dx <= ri; dx++) {
            if (dx * dx + dy * dy > r2) {
                continue;
            }
            int i = wrapi((int)floorf(cy + dy)) * N + wrapi((int)floorf(cx + dx));
            p->u[i] = q15(0.5f);
            p->v[i] = q15(0.25f + 0.05f * petri_randf(&p->rng));
        }
    }
}

void petri_reseed(petri_t *p)
{
    for (int i = 0; i < N * N; i++) {
        p->u[i] = PETRI_Q;
        p->v[i] = 0;
    }
    for (int k = 0; k < 12; k++) {
        float x = petri_randf(&p->rng) * N, y = petri_randf(&p->rng) * N;
        petri_drop(p, x, y, 2 + 3 * petri_randf(&p->rng));
    }
}

void petri_init(petri_t *p, uint32_t seed)
{
    memset(p, 0, sizeof *p);
    p->rng = seed;
    float f, k;
    petri_preset(0, &f, &k);
    petri_set_fk(p, f, k);
    petri_reseed(p);
    p->appetite = 0.15f;
    p->mutation = 1;
    p->flip = 1;
    static const uint8_t START[PETRI_KINDS] = {3, 1, 2, 1};
    for (int i = 0; i < PETRI_KINDS; i++) {
        petri_set_count(p, i, START[i]);
    }
}

static inline int clampq(int32_t x)
{
    return x < 0 ? 0 : x > PETRI_Q ? PETRI_Q : x;
}

// Шаг на месте: строка y-1 уже перезаписана, поэтому её старая копия в row[0]; текущая строка перезаписывается
// слева направо - копия в row[1]; для последней строки нижний сосед - строка 0, её старая копия в row[2].
void petri_step(petri_t *p)
{
    int16_t(*prev)[N] = p->row[0], (*cur)[N] = p->row[1], (*first)[N] = p->row[2];
    memcpy(first[0], p->u, sizeof first[0]);
    memcpy(first[1], p->v, sizeof first[1]);
    const int32_t f = p->f_q, k = f + p->k_q;
    for (int y = 0; y < N; y++) {
        int16_t *gu = p->u + y * N, *gv = p->v + y * N;
        memcpy(cur[0], gu, sizeof cur[0]);
        memcpy(cur[1], gv, sizeof cur[1]);
        const int16_t *uu = y ? prev[0] : p->u + (N - 1) * N, *uv = y ? prev[1] : p->v + (N - 1) * N;
        const int16_t *du = y < N - 1 ? p->u + (y + 1) * N : first[0], *dv = y < N - 1 ? p->v + (y + 1) * N : first[1];
        const int16_t *mu = cur[0], *mv = cur[1];
        const int8_t *dk = p->dk + y * N;
        for (int x = 0; x < N; x++) {
            int xm = x ? x - 1 : N - 1, xp = x < N - 1 ? x + 1 : 0;
            int32_t u = mu[x], v = mv[x];
            int32_t s4u = mu[xm] + mu[xp] + uu[x] + du[x], sdu = uu[xm] + uu[xp] + du[xm] + du[xp];
            int32_t s4v = mv[xm] + mv[xp] + uv[x] + dv[x], sdv = uv[xm] + uv[xp] + dv[xm] + dv[xp];
            int32_t lu = ((4 * s4u + sdu - 20 * u) * 1638 + R15) >> 15; // * 0.05
            int32_t lv = ((4 * s4v + sdv - 20 * v) * 1638 + R15) >> 15;
            int32_t uv2 = (u * v + R15) >> 15, uvv = (uv2 * v + R15) >> 15;
            int32_t kk = k + ((dk[x] * 99) >> 5); // dk * 0.012/127 в Q15
            gu[x] = (int16_t)clampq(u + lu - uvv + ((f * (PETRI_Q - u) + R15) >> 15));
            gv[x] = (int16_t)clampq(v + (lv >> 1) + uvv - ((kk * v + R15) >> 15));
        }
        int16_t(*t)[N] = prev;
        prev = cur;
        cur = t;
    }
}

float petri_v(const petri_t *p, float x, float y)
{
    x = wrapf(x);
    y = wrapf(y);
    int x0 = (int)x, y0 = (int)y;
    float fx = x - x0, fy = y - y0;
    int x1 = x0 + 1 < N ? x0 + 1 : 0, y1 = y0 + 1 < N ? y0 + 1 : 0;
    x0 = x0 < N ? x0 : 0;
    y0 = y0 < N ? y0 : 0;
    float a = p->v[y0 * N + x0], b = p->v[y0 * N + x1], c = p->v[y1 * N + x0], d = p->v[y1 * N + x1];
    return (a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy) * (1.0f / PETRI_Q);
}
