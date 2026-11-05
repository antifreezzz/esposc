#include <math.h>

#include "petri.h"

// Снятие волны: оборот орбиты по рельефу V в 256 точек и модуляции для фильтра синта.

#define N PETRI_N
#define TAU 6.2831853f

static const float MODE_GAIN[PETRI_MODES] = {1, 2.5f, 1, 2.5f}; // Край и Пустота тише Высоты (подобрать на слух)

// Кратчайшее смещение на торе.
static inline float wd(float d)
{
    return d - N * floorf(d * (1.0f / N) + 0.5f);
}

static inline float clampf(float x, float lo, float hi)
{
    return x < lo ? lo : x > hi ? hi : x;
}

void petri_orbit_point(const petri_t *p, float a, float *x, float *y)
{
    float R = p->radius, ox, oy;
    switch (p->shape) {
    case PETRI_EIGHT: ox = R * cosf(a); oy = R * 0.7f * sinf(2 * a); break;
    case PETRI_LISS32: ox = R * cosf(2 * a); oy = R * sinf(3 * a); break;
    case PETRI_ROSE: {
        float r = R * cosf(3 * a);
        ox = r * cosf(a);
        oy = r * sinf(a);
        break;
    }
    case PETRI_EPI: ox = R * (cosf(a) + 0.35f * cosf(6 * a)); oy = R * (sinf(a) + 0.35f * sinf(6 * a)); break;
    default: ox = R * cosf(a); oy = R * sinf(a); break;
    }
    *x = p->cx + ox;
    *y = p->cy + oy;
}

static const petri_agent_t *first(const petri_t *p, int kind)
{
    for (int i = 0; i < p->n; i++) {
        if (p->a[i].kind == kind) {
            return &p->a[i];
        }
    }
    return 0;
}

void petri_move_center(petri_t *p, float dt)
{
    const petri_agent_t *g = p->follow ? first(p, PETRI_GRAZER) : 0;
    float tx, ty, k;
    if (g) {
        tx = g->x;
        ty = g->y;
        k = 1 - expf(-dt * 3);
    } else {
        const float r = N * 0.28f;
        p->ang += p->drift / r * dt;
        tx = N / 2 + r * cosf(p->ang);
        ty = N / 2 + r * sinf(p->ang * 0.77f);
        k = 1 - expf(-dt * 2);
    }
    p->cx = petri_wrapf(p->cx + wd(tx - p->cx) * k);
    p->cy = petri_wrapf(p->cy + wd(ty - p->cy) * k);
}

// Статический шум для Пустоты: хеш клетки в -1..1, без массива.
static float cell_noise(int x, int y)
{
    uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return (h & 0xFFFF) * (2.0f / 65535) - 1;
}

static float noise_at(float x, float y)
{
    x = petri_wrapf(x);
    y = petri_wrapf(y);
    int x0 = (int)x % N, y0 = (int)y % N, x1 = (x0 + 1) % N, y1 = (y0 + 1) % N;
    float fx = x - (int)x, fy = y - (int)y;
    float a = cell_noise(x0, y0), b = cell_noise(x1, y0), c = cell_noise(x0, y1), d = cell_noise(x1, y1);
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}

static float dk_at(const petri_t *p, float x, float y)
{
    x = petri_wrapf(x);
    y = petri_wrapf(y);
    int x0 = (int)x % N, y0 = (int)y % N, x1 = (x0 + 1) % N, y1 = (y0 + 1) % N;
    float fx = x - (int)x, fy = y - (int)y;
    float a = p->dk[y0 * N + x0], b = p->dk[y0 * N + x1], c = p->dk[y1 * N + x0], d = p->dk[y1 * N + x1];
    return (a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy) * (0.012f / 127);
}

static float relief(const petri_t *p, float x, float y, float c)
{
    float v = petri_v(p, x, y);
    switch (p->mode) {
    case PETRI_EDGE: {
        float gx = petri_v(p, x + 1, y) - petri_v(p, x - 1, y), gy = petri_v(p, x, y + 1) - petri_v(p, x, y - 1);
        return tanhf(4 * c * sqrtf(gx * gx + gy * gy));
    }
    case PETRI_FOLD: return sinf(4 * c * (v - 0.12f));
    case PETRI_VOID: {
        float o = clampf((v - 0.04f) / 0.14f, 0, 1);
        return (1 - o) * tanhf(c * noise_at(x, y));
    }
    default: return tanhf(c * (v - 0.12f));
    }
}

static int8_t mod8(float x)
{
    return (int8_t)lrintf(clampf(x, -1, 1) * 127);
}

void petri_capture(const petri_t *p, int16_t table[PETRI_TABLE], int8_t mods[PETRI_MODS])
{
    const float c = powf(40, clampf(p->contrast, 0, 1));
    float h[PETRI_TABLE], mean = 0;
    for (int j = 0; j < PETRI_TABLE; j++) {
        float x, y;
        petri_orbit_point(p, TAU * j / PETRI_TABLE, &x, &y);
        h[j] = relief(p, x, y, c);
        mean += h[j];
    }
    mean *= 1.0f / PETRI_TABLE;
    const float g = MODE_GAIN[p->mode < PETRI_MODES ? p->mode : 0] * 16000;
    for (int j = 0; j < PETRI_TABLE; j++) {
        table[j] = (int16_t)lrintf(clampf((h[j] - mean) * g, -32767, 32767));
    }

    float dk = 0, dens = 0;
    for (int j = 0; j < 24; j++) {
        float x, y;
        petri_orbit_point(p, TAU * j / 24, &x, &y);
        dk += dk_at(p, x, y);
        dens += petri_v(p, x, y);
    }
    float vd = 1e9f;
    for (int i = 0; i < p->n; i++) {
        if (p->a[i].kind == PETRI_VORTEX) {
            vd = fminf(vd, hypotf(wd(p->a[i].x - p->cx), wd(p->a[i].y - p->cy)));
        }
    }
    const petri_agent_t *g0 = first(p, PETRI_GRAZER);
    mods[0] = mod8(petri_count(p, PETRI_GRAZER) / 16.0f);
    mods[1] = mod8(dk / 24 / 0.008f);
    mods[2] = mod8(fmaxf(0, 1 - vd / 32));
    mods[3] = mod8(g0 ? g0->energy / 5 : 0);
    mods[4] = mod8(p->jump);
    mods[5] = mod8(dens / 24 * 3);
}
