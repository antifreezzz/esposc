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

void petri_bind(petri_t *p, int16_t *u, int16_t *v, int8_t *dk)
{
    p->u = u;
    p->v = v;
    p->dk = dk;
}

void petri_init(petri_t *p, uint32_t seed)
{
    int16_t *u = p->u, *v = p->v;
    int8_t *dk = p->dk;
    memset(p, 0, sizeof *p);
    petri_bind(p, u, v, dk);
    memset(dk, 0, PETRI_N * PETRI_N);
    p->rng = seed;
    float f, k;
    petri_preset(0, &f, &k);
    petri_set_fk(p, f, k);
    petri_reseed(p);
    p->appetite = 0.15f;
    p->mutation = 1;
    p->flip = 1;
    p->radius = 18;
    p->drift = 2;
    p->cx = p->cy = PETRI_N / 2;
    static const uint8_t START[PETRI_KINDS] = {3, 1, 2, 1};
    for (int i = 0; i < PETRI_KINDS; i++) {
        petri_set_count(p, i, START[i]);
    }
}

static inline int clampq(int32_t x)
{
    return x < 0 ? 0 : x > PETRI_Q ? PETRI_Q : x;
}

// Лапласиан строки в Q15, уже умноженный на 0.05: 4*s4 + sd - 20c == 3*s4 + S - 21c, где S - сумма окна 3x3.
static void row_lap(const int16_t *up, const int16_t *m, const int16_t *dn, int32_t *col, int32_t *lap)
{
    for (int x = 0; x < N; x++) {
        col[x + 1] = up[x] + m[x + 1] + dn[x];
    }
    col[0] = col[N];
    col[N + 1] = col[1];
    for (int x = 1; x <= N; x++) {
        int32_t c = m[x], s4 = m[x - 1] + m[x + 1] + col[x] - c, S = col[x - 1] + col[x] + col[x + 1];
        lap[x - 1] = ((3 * s4 + S - 21 * c) * 1638 + R15) >> 15;
    }
}

// Шаг на месте: строка y-1 уже перезаписана, поэтому её старая копия в prev; текущая строка перезаписывается -
// копия в mid (с краями тора); для последней строки нижний сосед - строка 0, её старая копия в first.
void petri_step(petri_t *p)
{
    memcpy(p->first[0], p->u, sizeof p->first[0]); // строка 0
    memcpy(p->first[1], p->v, sizeof p->first[1]);
    const int32_t f = p->f_q, k = f + p->k_q;
    for (int y = 0; y < N; y++) {
        int16_t *gu = p->u + y * N, *gv = p->v + y * N;
        int16_t *mu = p->mid[0], *mv = p->mid[1];
        memcpy(mu + 1, gu, N * sizeof *gu);
        memcpy(mv + 1, gv, N * sizeof *gv);
        mu[0] = mu[N];
        mu[N + 1] = mu[1];
        mv[0] = mv[N];
        mv[N + 1] = mv[1];
        const int16_t *uu = y ? p->prev[0] : p->u + (N - 1) * N, *uv = y ? p->prev[1] : p->v + (N - 1) * N;
        const int16_t *du = y < N - 1 ? gu + N : p->first[0], *dv = y < N - 1 ? gv + N : p->first[1];
        row_lap(uu, mu, du, p->col[0], p->lap[0]);
        row_lap(uv, mv, dv, p->col[1], p->lap[1]);
        const int32_t *lu = p->lap[0], *lv = p->lap[1];
        const int8_t *dk = p->dk + y * N;
        for (int x = 0; x < N; x++) {
            int32_t u = mu[x + 1], v = mv[x + 1];
            int32_t uv2 = (u * v + R15) >> 15, uvv = (uv2 * v + R15) >> 15;
            int32_t kk = k + ((dk[x] * 99) >> 5); // dk * 0.012/127 в Q15
            gu[x] = (int16_t)clampq(u + lu[x] - uvv + ((f * (PETRI_Q - u) + R15) >> 15));
            gv[x] = (int16_t)clampq(v + (lv[x] >> 1) + uvv - ((kk * v + R15) >> 15));
        }
        memcpy(p->prev[0], mu + 1, N * sizeof *mu);
        memcpy(p->prev[1], mv + 1, N * sizeof *mv);
    }
}

float petri_v(const petri_t *p, float x, float y)
{
    x = petri_wrapf(x);
    y = petri_wrapf(y);
    int x0 = (int)x, y0 = (int)y;
    float fx = x - x0, fy = y - y0;
    int x1 = x0 + 1 < N ? x0 + 1 : 0, y1 = y0 + 1 < N ? y0 + 1 : 0;
    x0 = x0 < N ? x0 : 0;
    y0 = y0 < N ? y0 : 0;
    float a = p->v[y0 * N + x0], b = p->v[y0 * N + x1], c = p->v[y1 * N + x0], d = p->v[y1 * N + x1];
    return (a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy) * (1.0f / PETRI_Q);
}
