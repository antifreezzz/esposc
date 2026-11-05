#include <math.h>

#include "petri.h"

// Жители чашки. Движение, еда и энергия - за кадр как в прототипе (радиусы x0.8 под сетку 96), таймеры
// пересчитаны на кадр 20 Гц: прыжки, затухание Δk, иммигрант.

#define N PETRI_N
#define TAU 6.2831853f

static const uint8_t MAX[PETRI_KINDS] = {16, 4, 4, 3};

static inline int wrapi(int a)
{
    a %= N;
    return a < 0 ? a + N : a;
}

static inline float noise(petri_t *p, float amp) // равномерно в [-amp, amp]
{
    return (petri_randf(&p->rng) * 2 - 1) * amp;
}

static inline int clampq(int x)
{
    return x < 0 ? 0 : x > PETRI_Q ? PETRI_Q : x;
}

static float bilin(const int16_t *g, float x, float y)
{
    x = petri_wrapf(x);
    y = petri_wrapf(y);
    int x0 = (int)x % N, y0 = (int)y % N;
    float fx = x - (int)x, fy = y - (int)y;
    int x1 = x0 + 1 < N ? x0 + 1 : 0, y1 = y0 + 1 < N ? y0 + 1 : 0;
    float a = g[y0 * N + x0], b = g[y0 * N + x1], c = g[y1 * N + x0], d = g[y1 * N + x1];
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}

int petri_count(const petri_t *p, int kind)
{
    int c = 0;
    for (int i = 0; i < p->n; i++) {
        c += p->a[i].kind == kind;
    }
    return c;
}

static petri_agent_t *spawn(petri_t *p, int kind, float x, float y)
{
    if (p->n >= PETRI_AGENTS) {
        return 0;
    }
    petri_agent_t *a = &p->a[p->n++];
    a->kind = (uint8_t)kind;
    a->x = x;
    a->y = y;
    a->h = petri_randf(&p->rng) * TAU;
    a->energy = 2;
    a->t = (uint16_t)(petri_rand(&p->rng) % 100);
    a->sign = 1;
    if (kind == PETRI_MUTAGEN || kind == PETRI_VORTEX) {
        p->flip = p->flip ? p->flip : 1;
        a->sign = p->flip;
        p->flip = (int8_t)-p->flip;
    }
    return a;
}

static void spawn_random(petri_t *p, int kind)
{
    float x = petri_randf(&p->rng) * N, y = petri_randf(&p->rng) * N;
    spawn(p, kind, x, y);
}

static void remove_at(petri_t *p, int i)
{
    for (int j = i; j < p->n - 1; j++) {
        p->a[j] = p->a[j + 1];
    }
    p->n--;
}

void petri_set_count(petri_t *p, int kind, int n)
{
    if (kind < 0 || kind >= PETRI_KINDS) {
        return;
    }
    n = n < 0 ? 0 : n > MAX[kind] ? MAX[kind] : n;
    p->want[kind] = (uint8_t)n;
    int c = petri_count(p, kind);
    for (int i = p->n - 1; i >= 0 && c > n; i--) {
        if (p->a[i].kind == kind) {
            remove_at(p, i);
            c--;
        }
    }
    for (; c < n; c++) {
        spawn_random(p, kind);
    }
}

static void walk(petri_agent_t *a, float speed)
{
    a->x = petri_wrapf(a->x + speed * cosf(a->h));
    a->y = petri_wrapf(a->y + speed * sinf(a->h));
}

static void grazer(petri_t *p, petri_agent_t *a, int pop, int *count)
{
    float best = -1, bd = a->h;
    for (int j = 0; j < 8; j++) {
        float ang = a->h + (j - 3.5f) * 0.45f;
        float s = petri_v(p, a->x + 4 * cosf(ang), a->y + 4 * sinf(ang));
        if (s > best) {
            best = s;
            bd = ang;
        }
    }
    if (best > 0.02f) {
        a->h += atan2f(sinf(bd - a->h), cosf(bd - a->h)) * 0.3f;
    }
    a->h += noise(p, 0.2f);
    walk(a, 0.4f);
    int cx = (int)lrintf(a->x), cy = (int)lrintf(a->y);
    float eaten = 0;
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 > 5) { // радиус 2.4
                continue;
            }
            int i = wrapi(cy + dy) * N + wrapi(cx + dx);
            int take = (int)(p->v[i] * p->appetite * (1 - sqrtf((float)d2) / 2.8f));
            p->v[i] = (int16_t)(p->v[i] - take);
            p->u[i] = (int16_t)clampq(p->u[i] + take);
            eaten += take * (1.0f / PETRI_Q);
        }
    }
    a->energy += eaten * 0.045f - (0.01f + 0.0012f * pop); // теснота: чем больше колония, тем дороже жизнь
    if (a->energy > 4.5f && *count < p->want[PETRI_GRAZER]) {
        a->energy *= 0.5f;
        float e = a->energy, h = a->h + TAU / 2, x = a->x, y = a->y;
        petri_agent_t *b = spawn(p, PETRI_GRAZER, x, y); // a может сдвинуться только при удалении, здесь нет
        if (b) {
            b->energy = e;
            b->h = h;
            (*count)++;
        }
    }
}

static void seeder(petri_t *p, petri_agent_t *a)
{
    if (petri_randf(&p->rng) < 0.045f) { // полёт Леви
        float u = petri_randf(&p->rng) + 1e-3f;
        float len = fminf(40, 2.4f * powf(u, -1 / 1.5f));
        a->h = petri_randf(&p->rng) * TAU;
        walk(a, len);
        petri_drop(p, a->x, a->y, 2);
        p->jump = 1;
        p->jumps++;
    } else {
        a->h += noise(p, 0.3f);
        walk(a, 0.24f);
    }
    if (a->t % 10 == 0) {
        petri_drop(p, a->x, a->y, 1.6f);
    }
}

static void mutagen(petri_t *p, petri_agent_t *a)
{
    a->h += noise(p, 0.15f);
    walk(a, 0.28f);
    int cx = (int)lrintf(a->x), cy = (int)lrintf(a->y);
    float amt = a->sign * 5.29f * p->mutation; // 0.0005 в шагах Δk (0.012/127)
    for (int dy = -5; dy <= 5; dy++) {
        for (int dx = -5; dx <= 5; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 > 31) { // радиус 5.6
                continue;
            }
            int i = wrapi(cy + dy) * N + wrapi(cx + dx);
            int d = p->dk[i] + (int)lrintf(amt * (1 - d2 / 31.36f));
            p->dk[i] = (int8_t)(d < -127 ? -127 : d > 127 ? 127 : d);
        }
    }
}

static void vortex(petri_t *p, petri_agent_t *a)
{
    a->h += noise(p, 0.075f);
    walk(a, 0.16f);
    int cx = (int)lrintf(a->x), cy = (int)lrintf(a->y);
    for (int pass = 0; pass < 2; pass++) {
        int k = 0;
        for (int dy = -PETRI_VR; dy <= PETRI_VR; dy++) {
            for (int dx = -PETRI_VR; dx <= PETRI_VR; dx++, k++) {
                float r = sqrtf((float)(dx * dx + dy * dy));
                if (r > PETRI_VR) {
                    continue;
                }
                if (pass == 0) {
                    float f = 1 - r / PETRI_VR, th = -0.09f * a->sign * f * f, c = cosf(th), s = sinf(th);
                    float sx = cx + dx * c - dy * s, sy = cy + dx * s + dy * c;
                    p->patch[0][k] = bilin(p->u, sx, sy);
                    p->patch[1][k] = bilin(p->v, sx, sy);
                } else {
                    int i = wrapi(cy + dy) * N + wrapi(cx + dx);
                    p->u[i] = (int16_t)clampq((int)lrintf(p->patch[0][k]));
                    p->v[i] = (int16_t)clampq((int)lrintf(p->patch[1][k]));
                }
            }
        }
    }
}

// Δk x0.997 за кадр: в int8 простое округление либо замораживает малые значения, либо гасит всё за 127 кадров,
// поэтому шаг к нулю с вероятностью 0.003 * |dk|.
static void decay_dk(petri_t *p)
{
    for (int i = 0; i < N * N; i++) {
        int d = p->dk[i];
        if (d && (petri_rand(&p->rng) & 0xFFFF) < (uint32_t)(d < 0 ? -d : d) * 197) { // 0.003 * 65536
            p->dk[i] = (int8_t)(d > 0 ? d - 1 : d + 1);
        }
    }
}

void petri_frame(petri_t *p, int steps)
{
    for (int s = 0; s < steps; s++) {
        petri_step(p);
    }
    p->jump *= 0.68f; // 0.88 за кадр 60 Гц; до жителей, чтобы прыжок этого кадра был виден как 1
    int pop = petri_count(p, PETRI_GRAZER), count = pop;
    int n0 = p->n;
    for (int i = 0; i < n0; i++) {
        petri_agent_t *a = &p->a[i];
        a->t++;
        switch (a->kind) {
        case PETRI_GRAZER: grazer(p, a, pop, &count); break;
        case PETRI_SEEDER: seeder(p, a); break;
        case PETRI_MUTAGEN: mutagen(p, a); break;
        default: vortex(p, a); break;
        }
    }
    for (int i = p->n - 1; i >= 0; i--) {
        if (p->a[i].kind == PETRI_GRAZER && p->a[i].energy < 0) {
            remove_at(p, i);
        }
    }
    decay_dk(p);
    if (p->want[PETRI_GRAZER] && petri_count(p, PETRI_GRAZER) == 0) {
        if (++p->immigrant_t >= 100) { // 5 с без пожирателей
            p->immigrant_t = 0;
            spawn_random(p, PETRI_GRAZER);
        }
    } else {
        p->immigrant_t = 0;
    }
    petri_move_center(p, 0.05f);
}
